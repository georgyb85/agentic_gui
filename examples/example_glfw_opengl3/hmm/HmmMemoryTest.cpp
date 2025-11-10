#include "HmmMemoryTest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <numeric>
#include <thread>
#include <functional>

#include "HmmGpu.h"

namespace hmm {

HmmMemoryAnalyzer::HmmMemoryAnalyzer(HmmMemoryConfig config)
    : m_config(std::move(config)) {
    if (m_config.numStates < 2) {
        m_config.numStates = 2;
    }
    if (m_config.maxThreads <= 0) {
        m_config.maxThreads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    }
    if (m_config.mcptReplications < 1) {
        m_config.mcptReplications = 1;
    }
}

HmmMemoryResult HmmMemoryAnalyzer::analyze(const Eigen::MatrixXd& observations,
                                           std::mt19937_64& rng,
                                           std::function<void(double)> progressCallback) {
    if (observations.rows() < 3) {
        throw std::invalid_argument("HMM memory analysis requires at least 3 observations");
    }

    Eigen::MatrixXd data = observations;
    if (m_config.standardize) {
        for (int col = 0; col < data.cols(); ++col) {
            double mean = data.col(col).mean();
            Eigen::VectorXd centered = data.col(col).array() - mean;
            double stddev = std::sqrt(centered.array().square().mean());
            if (stddev < 1e-12) {
                stddev = 1.0;
            }
            data.col(col) = centered / stddev;
        }
    }

    HmmModelConfig modelConfig;
    modelConfig.numStates = m_config.numStates;
    modelConfig.numFeatures = static_cast<int>(data.cols());
    modelConfig.maxIterations = m_config.maxIterations;
    modelConfig.numRestarts = m_config.numRestarts;
    modelConfig.tolerance = m_config.tolerance;
    modelConfig.regularization = m_config.regularization;

    bool canUseGpu = m_config.useGpu && HmmGpuAvailable() && HmmGpuSupports(modelConfig.numStates, modelConfig.numFeatures);

    HmmFitResult originalFit;
    auto fitCallback = [&progressCallback](int iteration, double /*loglik*/) {
        if (progressCallback) {
            progressCallback(static_cast<double>(iteration));
        }
    };
#if defined(HMM_WITH_CUDA)
    if (canUseGpu) {
        try {
            originalFit = FitHmmGpu(data, modelConfig, rng, fitCallback);
        } catch (const std::exception&) {
            canUseGpu = false;
        }
    }
    if (!canUseGpu) {
        HmmModel model(modelConfig);
        originalFit = model.fit(data, rng, fitCallback);
    }
#else
    HmmModel model(modelConfig);
    originalFit = model.fit(data, rng, fitCallback);
#endif
    HmmMemoryResult result;
    result.originalFit = originalFit;
    result.originalLogLikelihood = originalFit.logLikelihood;

    const int totalRuns = m_config.mcptReplications;
    if (totalRuns <= 1) {
        result.pValue = 1.0;
        result.permutationLogLikelihoods.clear();
        if (progressCallback) {
            progressCallback(1.0);
        }
        return result;
    }

    const int permutations = totalRuns - 1;
    result.permutationLogLikelihoods.resize(permutations);

    std::vector<uint64_t> seeds(permutations);
    for (int i = 0; i < permutations; ++i) {
        seeds[i] = rng();
    }

    std::vector<int> baseIndex(data.rows());
    std::iota(baseIndex.begin(), baseIndex.end(), 0);

    const int concurrency = std::max(1, std::min(m_config.maxThreads, permutations));
    std::atomic<int> completed{0};
    auto updateProgress = [&](int localCompleted) {
        if (progressCallback) {
            double fraction = static_cast<double>(localCompleted) / static_cast<double>(permutations);
            progressCallback(std::min(1.0, fraction));
        }
    };

    if (canUseGpu) {
        bool gpuHealthy = true;
        int idx = 0;
        for (; idx < permutations; ++idx) {
            std::mt19937_64 localRng(seeds[idx]);
            std::vector<int> permutedIndex = baseIndex;
            std::shuffle(permutedIndex.begin(), permutedIndex.end(), localRng);

            Eigen::MatrixXd permuted(data.rows(), data.cols());
            for (int row = 0; row < permuted.rows(); ++row) {
                permuted.row(row) = data.row(permutedIndex[row]);
            }

            try {
                HmmFitResult fit = FitHmmGpu(permuted, modelConfig, localRng, std::function<void(int, double)>());
                result.permutationLogLikelihoods[idx] = fit.logLikelihood;
            } catch (const std::exception&) {
                gpuHealthy = false;
                break;
            }

            completed.fetch_add(1);
            updateProgress(completed.load());
        }

        if (!gpuHealthy) {
            canUseGpu = false;
            // fall through to CPU path for remaining permutations
            for (int cpuIdx = idx; cpuIdx < permutations; ++cpuIdx) {
                std::mt19937_64 localRng(seeds[cpuIdx]);
                std::vector<int> permutedIndex = baseIndex;
                std::shuffle(permutedIndex.begin(), permutedIndex.end(), localRng);

                Eigen::MatrixXd permuted(data.rows(), data.cols());
                for (int row = 0; row < permuted.rows(); ++row) {
                    permuted.row(row) = data.row(permutedIndex[row]);
                }

                HmmModel localModel(modelConfig);
                HmmFitResult fit = localModel.fit(permuted, localRng);
                result.permutationLogLikelihoods[cpuIdx] = fit.logLikelihood;
                completed.fetch_add(1);
                updateProgress(completed.load());
            }
        }
    }

    if (completed.load() < permutations) {
        std::vector<std::future<std::pair<int, double>>> tasks;
        tasks.reserve(permutations);

        for (int idx = 0; idx < permutations; ++idx) {
            if (static_cast<int>(tasks.size()) >= concurrency) {
                auto finished = tasks.front().get();
                result.permutationLogLikelihoods[finished.first] = finished.second;
                tasks.erase(tasks.begin());
                completed.fetch_add(1);
                updateProgress(completed.load());
            }

            tasks.emplace_back(std::async(std::launch::async, [&, idx]() -> std::pair<int, double> {
                std::mt19937_64 localRng(seeds[idx]);
                std::vector<int> permutedIndex = baseIndex;
                std::shuffle(permutedIndex.begin(), permutedIndex.end(), localRng);

                Eigen::MatrixXd permuted(data.rows(), data.cols());
                for (int row = 0; row < permuted.rows(); ++row) {
                    permuted.row(row) = data.row(permutedIndex[row]);
                }

                HmmModel localModel(modelConfig);
                HmmFitResult fit = localModel.fit(permuted, localRng);
                return std::make_pair(idx, fit.logLikelihood);
            }));
        }

        for (auto& task : tasks) {
            auto finished = task.get();
            result.permutationLogLikelihoods[finished.first] = finished.second;
            completed.fetch_add(1);
            updateProgress(completed.load());
        }
    }

    int greaterOrEqual = 1; // original run
    for (double loglike : result.permutationLogLikelihoods) {
        if (loglike >= result.originalLogLikelihood) {
            ++greaterOrEqual;
        }
    }
    result.pValue = static_cast<double>(greaterOrEqual) / static_cast<double>(totalRuns);

    double sum = std::accumulate(result.permutationLogLikelihoods.begin(),
                                 result.permutationLogLikelihoods.end(), 0.0);
    result.meanPermutationLogLikelihood = sum / static_cast<double>(permutations);

    double sqSum = 0.0;
    for (double loglike : result.permutationLogLikelihoods) {
        double diff = loglike - result.meanPermutationLogLikelihood;
        sqSum += diff * diff;
    }
    result.stdPermutationLogLikelihood = std::sqrt(sqSum / std::max(1, permutations - 1));

    if (progressCallback) {
        progressCallback(1.0);
    }

    return result;
}

} // namespace hmm

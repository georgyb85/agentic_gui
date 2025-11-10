#include "HmmTargetCorrelation.h"

#include <algorithm>
#include <atomic>
#include <future>
#include <functional>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <cmath>
#include <Eigen/Cholesky>
#include <Eigen/QR>

#include "HmmGpu.h"

namespace hmm {

TargetCorrelationAnalyzer::TargetCorrelationAnalyzer(TargetCorrelationConfig config)
    : m_config(std::move(config)) {
    if (m_config.combinationSize < 1 || m_config.combinationSize > 3) {
        throw std::invalid_argument("TargetCorrelationAnalyzer supports combination sizes between 1 and 3");
    }
    if (m_config.numStates < 2) {
        throw std::invalid_argument("TargetCorrelationAnalyzer requires at least two HMM states");
    }
    if (m_config.maxThreads <= 0) {
        m_config.maxThreads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    }
}

TargetCorrelationResult TargetCorrelationAnalyzer::analyze(const Eigen::MatrixXd& candidateFeatures,
                                                           const std::vector<std::string>& featureNames,
                                                           const Eigen::VectorXd& target,
                                                           std::mt19937_64& rng,
                                                           std::function<void(double)> progressCallback) {
    if (candidateFeatures.rows() != target.size()) {
        throw std::invalid_argument("Feature matrix row count must match target length");
    }
    if (static_cast<int>(featureNames.size()) != candidateFeatures.cols()) {
        throw std::invalid_argument("Feature name count must equal number of feature columns");
    }
    if (candidateFeatures.cols() < m_config.combinationSize) {
        throw std::invalid_argument("Not enough features to build requested combination size");
    }

    Eigen::MatrixXd processed = candidateFeatures;
    if (m_config.standardize) {
        for (int col = 0; col < processed.cols(); ++col) {
            double mean = processed.col(col).mean();
            Eigen::VectorXd centered = processed.col(col).array() - mean;
            double stddev = std::sqrt(centered.array().square().mean());
            if (stddev < 1e-12) {
                stddev = 1.0;
            }
            processed.col(col) = centered / stddev;
        }
    }

    auto combinations = generateCombinations(processed.cols(), m_config.combinationSize);
    if (combinations.empty()) {
        throw std::runtime_error("Failed to generate predictor combinations for HMM analysis");
    }

    TargetCorrelationResult result;
    result.combinations.reserve(combinations.size());

    const int totalCombos = static_cast<int>(combinations.size());
    const int concurrency = std::max(1, std::min(m_config.maxThreads, totalCombos));
    std::mutex resultMutex;
    std::atomic<int> completed{0};

    std::vector<uint64_t> seeds(totalCombos);
    for (int i = 0; i < totalCombos; ++i) {
        seeds[i] = rng();
    }

    auto updateProgress = [&](int localCompleted) {
        if (progressCallback) {
            double fraction = static_cast<double>(localCompleted) / static_cast<double>(totalCombos);
            progressCallback(fraction);
        }
    };

    std::vector<std::future<void>> tasks;
    tasks.reserve(totalCombos);

    for (int idx = 0; idx < totalCombos; ++idx) {
        const auto& combo = combinations[idx];
        if (static_cast<int>(tasks.size()) >= concurrency) {
            tasks.front().wait();
            tasks.erase(tasks.begin());
        }

        tasks.emplace_back(std::async(std::launch::async, [&, idx, combo]() {
            std::mt19937_64 localRng(seeds[idx]);

            Eigen::MatrixXd subset(processed.rows(), combo.size());
            for (std::size_t c = 0; c < combo.size(); ++c) {
                subset.col(static_cast<int>(c)) = processed.col(combo[c]);
            }

            TargetCorrelationComboResult comboResult =
                evaluateCombination(subset, combo, featureNames, target, localRng);

            {
                std::lock_guard<std::mutex> lock(resultMutex);
                result.combinations.push_back(std::move(comboResult));
                completed.fetch_add(1);
                updateProgress(completed.load());
            }
        }));
    }

    for (auto& task : tasks) {
        task.wait();
    }

    std::sort(result.combinations.begin(), result.combinations.end(),
              [](const TargetCorrelationComboResult& lhs, const TargetCorrelationComboResult& rhs) {
                  return lhs.rSquared > rhs.rSquared;
              });

    result.mcptReplicationsEvaluated = 1;

    if (m_config.mcptReplications > 0) {
        Eigen::VectorXd permuted = target;
        std::vector<int> indices(target.size());
        std::iota(indices.begin(), indices.end(), 0);

        for (int rep = 0; rep < m_config.mcptReplications; ++rep) {
            std::shuffle(indices.begin(), indices.end(), rng);
            for (int i = 0; i < target.size(); ++i) {
                permuted[i] = target[indices[i]];
            }

            double bestCritThisRep = 0.0;
            for (auto& comboResult : result.combinations) {
                double r2 = computeRSquared(comboResult.designMatrix,
                                            comboResult.designMatrixTranspose,
                                            comboResult.xtxInverse,
                                            permuted);
                if (r2 >= comboResult.rSquared - 1e-12) {
                    comboResult.mcptSoloCount += 1;
                }
                if (r2 > bestCritThisRep) {
                    bestCritThisRep = r2;
                }
            }

            for (auto& comboResult : result.combinations) {
                if (bestCritThisRep >= comboResult.rSquared - 1e-12) {
                    comboResult.mcptBestOfCount += 1;
                }
            }
        }

        const double denom = static_cast<double>(m_config.mcptReplications + 1);
        for (auto& comboResult : result.combinations) {
            comboResult.mcptSoloPValue = static_cast<double>(comboResult.mcptSoloCount) / denom;
            comboResult.mcptBestOfPValue = static_cast<double>(comboResult.mcptBestOfCount) / denom;
        }
        result.mcptReplicationsEvaluated = m_config.mcptReplications + 1;
    } else {
        for (auto& comboResult : result.combinations) {
            comboResult.mcptSoloPValue = 1.0;
            comboResult.mcptBestOfPValue = 1.0;
        }
    }

    if (progressCallback) {
        progressCallback(1.0);
    }

    return result;
}

TargetCorrelationComboResult TargetCorrelationAnalyzer::evaluateCombination(
    const Eigen::MatrixXd& data,
    const std::vector<int>& featureIndices,
    const std::vector<std::string>& featureNames,
    const Eigen::VectorXd& target,
    std::mt19937_64& rng) const {

    HmmModelConfig modelConfig;
    modelConfig.numStates = m_config.numStates;
    modelConfig.numFeatures = static_cast<int>(data.cols());
    modelConfig.maxIterations = m_config.maxIterations;
    modelConfig.numRestarts = m_config.numRestarts;
    modelConfig.tolerance = m_config.tolerance;
    modelConfig.regularization = m_config.regularization;

    HmmFitResult fitResult;

#if defined(HMM_WITH_CUDA)
    bool canUseGpu = m_config.useGpu &&
                     HmmGpuAvailable() &&
                     HmmGpuSupports(modelConfig.numStates,
                                    modelConfig.numFeatures);
    if (canUseGpu) {
        try {
            fitResult = FitHmmGpu(data, modelConfig, rng, std::function<void(int, double)>());
        } catch (const std::exception&) {
            canUseGpu = false;
        }
    }
    if (!canUseGpu) {
        HmmModel model(modelConfig);
        fitResult = model.fit(data, rng);
    }
#else
    HmmModel model(modelConfig);
    fitResult = model.fit(data, rng);
#endif

    const int T = static_cast<int>(data.rows());
    Eigen::MatrixXd designMatrix(T, m_config.numStates + 1);
    designMatrix.leftCols(m_config.numStates) = fitResult.statePosterior;
    designMatrix.col(m_config.numStates).setOnes();

    Eigen::MatrixXd designMatrixTranspose = designMatrix.transpose();
    Eigen::MatrixXd xtx = designMatrixTranspose * designMatrix;
    Eigen::MatrixXd identity = Eigen::MatrixXd::Identity(xtx.rows(), xtx.cols());
    xtx += identity * m_config.regularization;
    Eigen::LDLT<Eigen::MatrixXd> ldlt(xtx);
    if (ldlt.info() != Eigen::Success) {
        xtx += identity * (10.0 * m_config.regularization);
        ldlt.compute(xtx);
    }
    Eigen::MatrixXd xtxInverse;
    if (ldlt.info() == Eigen::Success) {
        xtxInverse = ldlt.solve(identity);
    } else {
        Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd> cod(xtx);
        xtxInverse = cod.pseudoInverse();
    }

    TargetCorrelationComboResult result;
    result.featureIndices = featureIndices;
    result.featureNames.reserve(featureIndices.size());
    for (int idx : featureIndices) {
        result.featureNames.push_back(featureNames[idx]);
    }
    result.logLikelihood = fitResult.logLikelihood;
    result.hmmFit = std::move(fitResult);
    result.designMatrix = std::move(designMatrix);
    result.designMatrixTranspose = std::move(designMatrixTranspose);
    result.xtxInverse = std::move(xtxInverse);

    Eigen::VectorXd coefficients;
    result.rSquared = computeRSquared(result.designMatrix,
                                      result.designMatrixTranspose,
                                      result.xtxInverse,
                                      target,
                                      &coefficients);

    Eigen::VectorXd predictions = result.designMatrix * coefficients;
    Eigen::VectorXd residuals = target - predictions;
    result.rmse = std::sqrt(residuals.array().square().mean());

    return result;
}

double TargetCorrelationAnalyzer::computeRSquared(const Eigen::MatrixXd& designMatrix,
                                                  const Eigen::MatrixXd& designMatrixTranspose,
                                                  const Eigen::MatrixXd& xtxInverse,
                                                  const Eigen::VectorXd& target,
                                                  Eigen::VectorXd* coefficients) const {
    Eigen::VectorXd xty = designMatrixTranspose * target;
    Eigen::VectorXd beta = xtxInverse * xty;
    if (coefficients) {
        *coefficients = beta;
    }
    Eigen::VectorXd predictions = designMatrix * beta;
    Eigen::VectorXd residuals = target - predictions;
    double ssRes = residuals.squaredNorm();
    double meanTarget = target.mean();
    double ssTot = (target.array() - meanTarget).matrix().squaredNorm();
    if (ssTot <= 1e-12) {
        return 0.0;
    }
    double r2 = 1.0 - ssRes / ssTot;
    if (!std::isfinite(r2)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, r2));
}

std::vector<std::vector<int>> TargetCorrelationAnalyzer::generateCombinations(int numFeatures,
                                                                             int combinationSize) const {
    std::vector<std::vector<int>> combos;
    std::vector<int> current(combinationSize);

    std::function<void(int, int)> backtrack = [&](int start, int depth) {
        if (depth == combinationSize) {
            combos.push_back(current);
            return;
        }
        for (int i = start; i <= numFeatures - (combinationSize - depth); ++i) {
            current[depth] = i;
            backtrack(i + 1, depth + 1);
        }
    };

    backtrack(0, 0);
    return combos;
}

} // namespace hmm

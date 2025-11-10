#include "HmmModel.h"

#include <Eigen/Eigenvalues>
#include <Eigen/Cholesky>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <algorithm>

namespace hmm {

namespace {
constexpr double LOG_TWO_PI = 1.83787706640934548356065947281123539;

Eigen::MatrixXd computeCentered(const Eigen::MatrixXd& data) {
    Eigen::MatrixXd centered = data.rowwise() - data.colwise().mean();
    return centered;
}

} // namespace

HmmModel::HmmModel(HmmModelConfig config)
    : m_config(std::move(config)) {
    if (m_config.numStates <= 0) {
        throw std::invalid_argument("HmmModel requires a positive number of states");
    }
    if (m_config.numFeatures <= 0) {
        throw std::invalid_argument("HmmModel requires a positive number of features");
    }
    if (m_config.maxIterations <= 0) {
        throw std::invalid_argument("HmmModel requires a positive iteration limit");
    }
    if (m_config.numRestarts <= 0) {
        throw std::invalid_argument("HmmModel requires at least one restart");
    }
}

HmmFitResult HmmModel::fit(const Eigen::MatrixXd& observations,
                           std::mt19937_64& rng,
                           std::function<void(int,double)> progressCallback) const {
    const int numObservations = static_cast<int>(observations.rows());
    if (numObservations < 2) {
        throw std::invalid_argument("Hidden Markov Model requires at least two observations");
    }
    if (observations.cols() != m_config.numFeatures) {
        throw std::invalid_argument("Observation feature dimension does not match model configuration");
    }

    HmmFitResult bestResult;
    bestResult.logLikelihood = -std::numeric_limits<double>::infinity();
    bestResult.converged = false;

    for (int restart = 0; restart < m_config.numRestarts; ++restart) {
        HmmModelParameters params;
        initializeParameters(observations, params, rng);

        WorkingState work;
        work.logEmission.resize(m_config.numStates, numObservations);
        work.alpha.resize(numObservations, m_config.numStates);
        work.beta.resize(numObservations, m_config.numStates);
        work.gamma.resize(numObservations, m_config.numStates);
        work.xiSum.setZero(m_config.numStates, m_config.numStates);
        work.gammaSums.setZero(m_config.numStates);

        double previousLogLikelihood = -std::numeric_limits<double>::infinity();
        bool converged = false;
        int iteration = 0;

        for (; iteration < m_config.maxIterations; ++iteration) {
            computeLogEmissionProbabilities(observations, params, work.logEmission);
            double logLikelihood = forwardBackward(params, work);

            if (progressCallback) {
                progressCallback(iteration, logLikelihood);
            }

            if (!std::isfinite(logLikelihood)) {
                break;
            }

            double improvement = logLikelihood - previousLogLikelihood;
            if (iteration > 0 && std::abs(improvement) < m_config.tolerance) {
                converged = true;
            }
            previousLogLikelihood = logLikelihood;

            maximizationStep(observations, work, params);

            if (converged) {
                break;
            }
        }

        if (previousLogLikelihood > bestResult.logLikelihood) {
            bestResult.logLikelihood = previousLogLikelihood;
            bestResult.iterations = iteration + 1;
            bestResult.converged = converged;
            bestResult.parameters = params;
            bestResult.statePosterior = work.gamma;
        }
    }

    return bestResult;
}

void HmmModel::initializeParameters(const Eigen::MatrixXd& observations,
                                    HmmModelParameters& params,
                                    std::mt19937_64& rng) const {
    const int numObservations = static_cast<int>(observations.rows());
    params.initialProbabilities = Eigen::VectorXd::Constant(m_config.numStates, 1.0 / m_config.numStates);
    params.transitionMatrix = Eigen::MatrixXd::Constant(m_config.numStates, m_config.numStates,
                                                        1.0 / m_config.numStates);
    params.means.resize(m_config.numStates, m_config.numFeatures);
    params.covariances.assign(m_config.numStates,
                              Eigen::MatrixXd::Identity(m_config.numFeatures, m_config.numFeatures));

    std::uniform_int_distribution<int> dist(0, numObservations - 1);
    for (int state = 0; state < m_config.numStates; ++state) {
        params.means.row(state) = observations.row(dist(rng));
    }

    Eigen::MatrixXd centered = computeCentered(observations);
    Eigen::MatrixXd sharedCov = (centered.transpose() * centered) / static_cast<double>(numObservations);
    sharedCov += Eigen::MatrixXd::Identity(m_config.numFeatures, m_config.numFeatures) * m_config.regularization;
    for (auto& cov : params.covariances) {
        cov = sharedCov;
    }
}

void HmmModel::computeLogEmissionProbabilities(const Eigen::MatrixXd& observations,
                                               const HmmModelParameters& params,
                                               Eigen::MatrixXd& logEmission) const {
    const int numObservations = static_cast<int>(observations.rows());

    for (int state = 0; state < m_config.numStates; ++state) {
        Eigen::LLT<Eigen::MatrixXd> llt(params.covariances[state]);
        if (llt.info() != Eigen::Success) {
            // Ensure positive definite by adding regularization
            Eigen::MatrixXd adjusted = params.covariances[state];
            ensurePositiveDefinite(adjusted, m_config.regularization);
            llt.compute(adjusted);
        }

        Eigen::MatrixXd invCov = llt.solve(Eigen::MatrixXd::Identity(m_config.numFeatures, m_config.numFeatures));
        double logDet = 0.0;
        const auto& L = llt.matrixL();
        for (int i = 0; i < m_config.numFeatures; ++i) {
            logDet += std::log(L(i, i));
        }
        logDet = 2.0 * logDet;

        const Eigen::RowVectorXd mean = params.means.row(state);
        for (int t = 0; t < numObservations; ++t) {
            Eigen::RowVectorXd diff = observations.row(t) - mean;
            double quadForm = diff * invCov * diff.transpose();
            double logProb = -0.5 * (m_config.numFeatures * LOG_TWO_PI + logDet + quadForm);
            logEmission(state, t) = logProb;
        }
    }
}

double HmmModel::forwardBackward(const HmmModelParameters& params,
                                 WorkingState& work) const {
    const int T = static_cast<int>(work.logEmission.cols());
    const int S = m_config.numStates;

    Eigen::MatrixXd logTransition = params.transitionMatrix.array().max(1e-18).log();
    Eigen::VectorXd logInit = params.initialProbabilities.array().max(1e-18).log();

    // Forward pass
    work.alpha.row(0) = (logInit + work.logEmission.col(0)).transpose();
    for (int t = 1; t < T; ++t) {
        for (int j = 0; j < S; ++j) {
            Eigen::VectorXd prev = work.alpha.row(t - 1).transpose() + logTransition.col(j);
            work.alpha(t, j) = work.logEmission(j, t) + logSumExp(prev);
        }
    }

    double logLikelihood = logSumExp(work.alpha.row(T - 1).transpose());

    // Backward pass
    work.beta.row(T - 1).setZero();
    for (int t = T - 2; t >= 0; --t) {
        for (int i = 0; i < S; ++i) {
            Eigen::VectorXd future(S);
            for (int j = 0; j < S; ++j) {
                future(j) = logTransition(i, j) + work.logEmission(j, t + 1) + work.beta(t + 1, j);
            }
            work.beta(t, i) = logSumExp(future);
        }
    }

    // Gamma (state posterior) and Xi sums
    work.gammaSums.setZero();
    work.xiSum.setZero();

    for (int t = 0; t < T; ++t) {
        for (int i = 0; i < S; ++i) {
            double value = work.alpha(t, i) + work.beta(t, i) - logLikelihood;
            double posterior = std::exp(std::max(value, -1e6)); // clamp to avoid underflow issues
            work.gamma(t, i) = posterior;
            work.gammaSums(i) += posterior;
        }
        double rowSum = work.gamma.row(t).sum();
        if (rowSum > 0) {
            work.gamma.row(t) /= rowSum;
        }
    }

    for (int t = 0; t < T - 1; ++t) {
        double normalizer = -std::numeric_limits<double>::infinity();
        Eigen::MatrixXd logXi(S, S);
        for (int i = 0; i < S; ++i) {
            for (int j = 0; j < S; ++j) {
                logXi(i, j) = work.alpha(t, i) + logTransition(i, j) +
                              work.logEmission(j, t + 1) + work.beta(t + 1, j);
                if (logXi(i, j) > normalizer) {
                    normalizer = logXi(i, j);
                }
            }
        }
        double sumExp = 0.0;
        for (int i = 0; i < S; ++i) {
            for (int j = 0; j < S; ++j) {
                sumExp += std::exp(logXi(i, j) - normalizer);
            }
        }
        double logSum = normalizer + std::log(sumExp);
        for (int i = 0; i < S; ++i) {
            for (int j = 0; j < S; ++j) {
                double value = std::exp(logXi(i, j) - logSum);
                work.xiSum(i, j) += value;
            }
        }
    }

    return logLikelihood;
}

void HmmModel::maximizationStep(const Eigen::MatrixXd& observations,
                                const WorkingState& work,
                                HmmModelParameters& params) const {
    const int T = static_cast<int>(observations.rows());
    const int S = m_config.numStates;

    // Update initial probabilities using gamma at time 0
    Eigen::VectorXd init = work.gamma.row(0).transpose();
    double initSum = init.sum();
    if (initSum > 0) {
        params.initialProbabilities = init / initSum;
    } else {
        params.initialProbabilities = Eigen::VectorXd::Constant(S, 1.0 / S);
    }

    // Update transitions
    Eigen::VectorXd xiRowSums = work.xiSum.rowwise().sum();
    for (int i = 0; i < S; ++i) {
        if (xiRowSums(i) > 0) {
            params.transitionMatrix.row(i) = work.xiSum.row(i) / xiRowSums(i);
        } else {
            params.transitionMatrix.row(i).setConstant(1.0 / S);
        }
    }

    // Update means and covariances
    for (int state = 0; state < S; ++state) {
        double gammaSum = work.gammaSums(state);
        if (gammaSum <= m_config.regularization) {
            // Reinitialize on insufficient support
            params.means.row(state) = observations.colwise().mean();
            params.covariances[state] =
                Eigen::MatrixXd::Identity(m_config.numFeatures, m_config.numFeatures) * m_config.regularization;
            continue;
        }

        Eigen::RowVectorXd mean = Eigen::RowVectorXd::Zero(m_config.numFeatures);
        for (int t = 0; t < T; ++t) {
            mean += work.gamma(t, state) * observations.row(t);
        }
        mean /= gammaSum;
        params.means.row(state) = mean;

        Eigen::MatrixXd cov = Eigen::MatrixXd::Zero(m_config.numFeatures, m_config.numFeatures);
        for (int t = 0; t < T; ++t) {
            Eigen::RowVectorXd diff = observations.row(t) - mean;
            cov += work.gamma(t, state) * diff.transpose() * diff;
        }
        cov /= gammaSum;
        cov += Eigen::MatrixXd::Identity(m_config.numFeatures, m_config.numFeatures) * m_config.regularization;
        ensurePositiveDefinite(cov, m_config.regularization);
        params.covariances[state] = cov;
    }
}

double HmmModel::logSumExp(const Eigen::VectorXd& values) {
    double maxCoeff = values.maxCoeff();
    if (!std::isfinite(maxCoeff)) {
        return -std::numeric_limits<double>::infinity();
    }
    double sum = 0.0;
    for (int i = 0; i < values.size(); ++i) {
        sum += std::exp(values[i] - maxCoeff);
    }
    return maxCoeff + std::log(sum);
}

double HmmModel::ensurePositiveDefinite(Eigen::MatrixXd& matrix, double minDeterminant) {
    const double eps = minDeterminant;
    int attempts = 0;
    while (attempts < 10) {
        Eigen::LLT<Eigen::MatrixXd> llt(matrix);
        if (llt.info() == Eigen::Success) {
            return matrix.determinant();
        }
        matrix += Eigen::MatrixXd::Identity(matrix.rows(), matrix.cols()) * eps;
        attempts++;
    }
    return matrix.determinant();
}

} // namespace hmm

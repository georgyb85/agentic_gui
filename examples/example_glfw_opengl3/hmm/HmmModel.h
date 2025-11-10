#pragma once

#include <Eigen/Dense>
#include <vector>
#include <random>
#include <functional>

namespace hmm {

struct HmmModelConfig {
    int numStates = 2;
    int numFeatures = 1;
    int maxIterations = 500;
    int numRestarts = 5;
    double tolerance = 1e-6;
    double regularization = 1e-6;
    bool verbose = false;
};

struct HmmModelParameters {
    Eigen::VectorXd initialProbabilities;            // size = numStates
    Eigen::MatrixXd transitionMatrix;                // numStates x numStates
    Eigen::MatrixXd means;                           // numStates x numFeatures
    std::vector<Eigen::MatrixXd> covariances;        // numStates matrices (numFeatures x numFeatures)
};

struct HmmFitResult {
    HmmModelParameters parameters;
    Eigen::MatrixXd statePosterior;                  // numObservations x numStates
    double logLikelihood = -std::numeric_limits<double>::infinity();
    int iterations = 0;
    bool converged = false;
};

class HmmModel {
public:
    explicit HmmModel(HmmModelConfig config);

    // Fit the model to data (rows = observations, cols = features)
    HmmFitResult fit(const Eigen::MatrixXd& observations,
                     std::mt19937_64& rng,
                     std::function<void(int,double)> progressCallback = {}) const;

private:
    HmmModelConfig m_config;

    struct WorkingState {
        Eigen::MatrixXd logEmission;                 // numStates x numObservations
        Eigen::MatrixXd alpha;                       // numObservations x numStates (log-space)
        Eigen::MatrixXd beta;                        // numObservations x numStates (log-space)
        Eigen::MatrixXd gamma;                       // numObservations x numStates (posterior)
        Eigen::MatrixXd xiSum;                       // numStates x numStates
        Eigen::VectorXd gammaSums;                   // numStates
    };

    void initializeParameters(const Eigen::MatrixXd& observations,
                              HmmModelParameters& params,
                              std::mt19937_64& rng) const;

    void computeLogEmissionProbabilities(const Eigen::MatrixXd& observations,
                                         const HmmModelParameters& params,
                                         Eigen::MatrixXd& logEmission) const;

    double forwardBackward(const HmmModelParameters& params,
                           WorkingState& work) const;

    void maximizationStep(const Eigen::MatrixXd& observations,
                          const WorkingState& work,
                          HmmModelParameters& params) const;

    static double logSumExp(const Eigen::VectorXd& values);
    static double ensurePositiveDefinite(Eigen::MatrixXd& matrix, double minDeterminant);
};

} // namespace hmm

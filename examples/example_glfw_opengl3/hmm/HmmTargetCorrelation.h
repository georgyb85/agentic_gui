#pragma once

#include "HmmModel.h"
#include <Eigen/Dense>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace hmm {

struct TargetCorrelationConfig {
    int numStates = 3;
    int combinationSize = 2;          // Dimensionality of predictor combinations (1-3)
    int maxIterations = 500;
    int numRestarts = 5;
    double tolerance = 1e-6;
    double regularization = 1e-6;
    int mcptReplications = 0;
    int maxThreads = 8;
    bool standardize = true;
    bool useGpu = false;              // Placeholder for future CUDA acceleration
};

struct TargetCorrelationComboResult {
    std::vector<int> featureIndices;              // Indices into candidate feature list
    std::vector<std::string> featureNames;
    double rSquared = 0.0;
    double rmse = 0.0;
    double logLikelihood = 0.0;
    double mcptSoloPValue = 0.0;
    double mcptBestOfPValue = 0.0;
    int mcptSoloCount = 1;
    int mcptBestOfCount = 1;
    HmmFitResult hmmFit;
    Eigen::MatrixXd designMatrix;                 // T x (numStates + 1)
    Eigen::MatrixXd designMatrixTranspose;        // (numStates + 1) x T
    Eigen::MatrixXd xtxInverse;                   // (numStates + 1) x (numStates + 1)
};

struct TargetCorrelationResult {
    std::vector<TargetCorrelationComboResult> combinations;
    int mcptReplicationsEvaluated = 1;
};

class TargetCorrelationAnalyzer {
public:
    explicit TargetCorrelationAnalyzer(TargetCorrelationConfig config);

    TargetCorrelationResult analyze(const Eigen::MatrixXd& candidateFeatures,
                                    const std::vector<std::string>& featureNames,
                                    const Eigen::VectorXd& target,
                                    std::mt19937_64& rng,
                                    std::function<void(double)> progressCallback = {});

private:
    TargetCorrelationConfig m_config;

    TargetCorrelationComboResult evaluateCombination(const Eigen::MatrixXd& data,
                                                     const std::vector<int>& featureIndices,
                                                     const std::vector<std::string>& featureNames,
                                                     const Eigen::VectorXd& target,
                                                     std::mt19937_64& rng) const;

    double computeRSquared(const Eigen::MatrixXd& designMatrix,
                           const Eigen::MatrixXd& designMatrixTranspose,
                           const Eigen::MatrixXd& xtxInverse,
                           const Eigen::VectorXd& target,
                           Eigen::VectorXd* coefficients = nullptr) const;

    std::vector<std::vector<int>> generateCombinations(int numFeatures, int combinationSize) const;
};

} // namespace hmm


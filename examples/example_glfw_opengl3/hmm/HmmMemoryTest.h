#pragma once

#include "HmmModel.h"

#include <Eigen/Dense>
#include <functional>
#include <random>
#include <vector>

namespace hmm {

struct HmmMemoryConfig {
    int numStates = 3;
    int maxIterations = 500;
    int numRestarts = 5;
    double tolerance = 1e-6;
    double regularization = 1e-6;
    int mcptReplications = 20;    // Includes the original ordering
    int maxThreads = 8;
    bool standardize = true;
    bool useGpu = false;
};

struct HmmMemoryResult {
    double originalLogLikelihood = 0.0;
    std::vector<double> permutationLogLikelihoods;   // Size = mcptReplications - 1
    double pValue = 1.0;
    double meanPermutationLogLikelihood = 0.0;
    double stdPermutationLogLikelihood = 0.0;
    HmmFitResult originalFit;
};

class HmmMemoryAnalyzer {
public:
    explicit HmmMemoryAnalyzer(HmmMemoryConfig config);

    HmmMemoryResult analyze(const Eigen::MatrixXd& observations,
                            std::mt19937_64& rng,
                            std::function<void(double)> progressCallback = {});

private:
    HmmMemoryConfig m_config;
};

} // namespace hmm

#pragma once

#include <Eigen/Dense>
#include <string>
#include <vector>

namespace fsca {

struct FscaConfig {
    int numComponents = 3;
    bool standardize = true;
};

struct FscaComponent {
    int variableIndex = -1;
    std::string variableName;
    double uniqueVariance = 0.0;
    double cumulativeVariance = 0.0;
    Eigen::VectorXd loadings;          // Correlation of component with original variables
};

struct FscaResult {
    std::vector<FscaComponent> components;
    double totalVariance = 0.0;
    double explainedVariance = 0.0;
    Eigen::MatrixXd orthonormalBasis;  // Columns are orthonormal component vectors
};

class FscaAnalyzer {
public:
    explicit FscaAnalyzer(FscaConfig config);

    FscaResult analyze(const Eigen::MatrixXd& data,
                       const std::vector<std::string>& columnNames) const;

private:
    FscaConfig m_config;
};

} // namespace fsca


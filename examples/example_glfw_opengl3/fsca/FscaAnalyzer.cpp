#include "FscaAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace fsca {

FscaAnalyzer::FscaAnalyzer(FscaConfig config)
    : m_config(std::move(config)) {
    if (m_config.numComponents < 1) {
        m_config.numComponents = 1;
    }
}

FscaResult FscaAnalyzer::analyze(const Eigen::MatrixXd& data,
                                 const std::vector<std::string>& columnNames) const {
    if (data.rows() < 2) {
        throw std::invalid_argument("FSCA requires at least two observations");
    }
    if (data.cols() < 1) {
        throw std::invalid_argument("FSCA requires at least one feature column");
    }
    if (static_cast<int>(columnNames.size()) != data.cols()) {
        throw std::invalid_argument("FSCA column name count must match matrix columns");
    }

    const int n = static_cast<int>(data.rows());
    const int p = static_cast<int>(data.cols());

    Eigen::MatrixXd working = data;
    std::vector<double> means(p, 0.0);
    std::vector<double> stddevs(p, 1.0);

    for (int j = 0; j < p; ++j) {
        double mean = working.col(j).mean();
        means[j] = mean;
        Eigen::ArrayXd centered = working.col(j).array() - mean;
        double variance = centered.square().sum() / static_cast<double>(n - 1);
        double stddev = std::sqrt(std::max(variance, 1e-12));
        stddevs[j] = stddev;
        if (m_config.standardize) {
            working.col(j) = centered / stddev;
        } else {
            working.col(j) = centered.matrix();
        }
    }

    double totalVariance = 0.0;
    for (int j = 0; j < p; ++j) {
        Eigen::ArrayXd centered = working.col(j).array();
        double variance = centered.square().sum() / static_cast<double>(n - 1);
        totalVariance += variance;
    }
    if (totalVariance <= 0.0) {
        totalVariance = static_cast<double>(p);
    }

    std::vector<int> selected;
    selected.reserve(std::min(m_config.numComponents, p));
    std::vector<Eigen::VectorXd> components;
    components.reserve(std::min(m_config.numComponents, p));

    FscaResult result;
    result.totalVariance = totalVariance;
    double cumulativeVariance = 0.0;

    std::vector<bool> used(p, false);

    for (int comp = 0; comp < std::min(m_config.numComponents, p); ++comp) {
        double bestScore = -std::numeric_limits<double>::infinity();
        int bestIndex = -1;
        Eigen::VectorXd bestResidual;

        for (int j = 0; j < p; ++j) {
            if (used[j]) {
                continue;
            }
            Eigen::VectorXd residual = working.col(j);
            for (const auto& component : components) {
                double proj = component.dot(residual);
                residual -= proj * component;
            }
            double score = residual.squaredNorm();
            if (score > bestScore && score > 1e-9) {
                bestScore = score;
                bestIndex = j;
                bestResidual = std::move(residual);
            }
        }

        if (bestIndex < 0) {
            break;
        }

        double norm = bestResidual.norm();
        if (norm <= 1e-9) {
            break;
        }
        Eigen::VectorXd componentVec = bestResidual / norm;

        double uniqueVariance = (norm * norm) / static_cast<double>(n - 1);
        cumulativeVariance += uniqueVariance;

        FscaComponent compInfo;
        compInfo.variableIndex = bestIndex;
        compInfo.variableName = columnNames[bestIndex];
        compInfo.uniqueVariance = uniqueVariance;
        compInfo.cumulativeVariance = cumulativeVariance;
        compInfo.loadings = working.transpose() * componentVec / static_cast<double>(n - 1);

        result.components.push_back(compInfo);
        used[bestIndex] = true;
        selected.push_back(bestIndex);
        components.push_back(componentVec);
    }

    if (!components.empty()) {
        result.orthonormalBasis.resize(n, static_cast<int>(components.size()));
        for (std::size_t i = 0; i < components.size(); ++i) {
            result.orthonormalBasis.col(static_cast<int>(i)) = components[i];
        }
    }
    result.explainedVariance = cumulativeVariance;

    return result;
}

} // namespace fsca


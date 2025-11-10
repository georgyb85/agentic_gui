#pragma once

#include <Eigen/Dense>
#include <functional>
#include <vector>

namespace stationarity {

struct MeanBreakConfig {
    int minSegmentLength = 20;
    bool standardize = false;
};

struct MeanBreakResult {
    bool valid = false;
    int breakIndex = -1;                 // Index of first observation in second segment
    double fStatistic = 0.0;
    double pValue = 1.0;
    double meanBefore = 0.0;
    double meanAfter = 0.0;
    double overallMean = 0.0;
    double sseBefore = 0.0;
    double sseAfter = 0.0;
    double sseCombined = 0.0;
    double sseSingle = 0.0;
    double effectSize = 0.0;
};

class MeanBreakTest {
public:
    explicit MeanBreakTest(MeanBreakConfig config);

    MeanBreakResult run(const Eigen::VectorXd& series,
                        std::function<void(double)> progressCallback = {}) const;

private:
    MeanBreakConfig m_config;
};

} // namespace stationarity


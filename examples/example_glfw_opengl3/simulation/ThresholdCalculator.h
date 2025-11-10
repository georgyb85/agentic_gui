#pragma once

#include <vector>
#include <algorithm>
#include <limits>
#include <utility>

namespace simulation {

enum class ThresholdMethod {
    Percentile95,      // Traditional 95th percentile method
    OptimalROC         // ROC-based profit factor optimization
};

class ThresholdCalculator {
public:
    // Calculate 95th percentile threshold
    static float CalculatePercentileThreshold(const std::vector<float>& predictions, 
                                               float percentile = 0.95f);
    
    // Calculate optimal threshold that maximizes profit factor
    // Based on ROC.CPP opt_thresh function
    static float CalculateOptimalThreshold(const std::vector<float>& predictions,
                                            const std::vector<float>& returns,
                                            int min_kept_percent = 1);
    
    // Calculate optimal threshold for short positions (predictions < threshold)
    static float CalculateOptimalShortThreshold(const std::vector<float>& predictions,
                                                 const std::vector<float>& returns,
                                                 int min_kept_percent = 1);
    
    // Unified interface for threshold calculation
    static float CalculateThreshold(ThresholdMethod method,
                                     const std::vector<float>& predictions,
                                     const std::vector<float>& returns,
                                     float percentile = 0.95f);
    
    // Calculate profit factor for given predictions and threshold
    static float CalculateProfitFactor(const std::vector<float>& predictions,
                                        const std::vector<float>& returns,
                                        float threshold);
    
    // Structure to hold profit factor breakdown
    struct ProfitFactorResult {
        float profit_factor;
        float total_wins;
        float total_losses;
        int num_trades;
        int num_winning_trades;
    };
    
    // Calculate detailed profit factor metrics
    static ProfitFactorResult CalculateProfitFactorDetailed(
        const std::vector<float>& predictions,
        const std::vector<float>& returns,
        float threshold);
};

} // namespace simulation
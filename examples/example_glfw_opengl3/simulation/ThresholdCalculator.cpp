#include "ThresholdCalculator.h"
#include <cmath>
#include <iostream>

namespace simulation {

float ThresholdCalculator::CalculatePercentileThreshold(const std::vector<float>& predictions, 
                                                         float percentile) {
    if (predictions.empty()) {
        return 0.0f;
    }
    
    // Create a copy and sort it
    std::vector<float> sorted_preds = predictions;
    std::sort(sorted_preds.begin(), sorted_preds.end());
    
    // Calculate the percentile index
    int percentile_idx = static_cast<int>(percentile * (sorted_preds.size() - 1));
    percentile_idx = std::max(0, std::min(percentile_idx, static_cast<int>(sorted_preds.size() - 1)));
    
    return sorted_preds[percentile_idx];
}

float ThresholdCalculator::CalculateOptimalThreshold(const std::vector<float>& predictions,
                                                      const std::vector<float>& returns,
                                                      int min_kept_percent) {
    if (predictions.empty() || returns.empty() || predictions.size() != returns.size()) {
        return 0.0f;
    }
    
    // Create sorted pairs of (prediction, return)
    std::vector<std::pair<float, float>> pred_return_pairs;
    for (size_t i = 0; i < predictions.size(); ++i) {
        pred_return_pairs.push_back({predictions[i], returns[i]});
    }
    
    // Sort by prediction value (ascending)
    std::sort(pred_return_pairs.begin(), pred_return_pairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    // Initialize with complete set trading (all positions above minimum threshold)
    float win_above = 0.0f;
    float lose_above = 0.0f;
    
    for (const auto& pair : pred_return_pairs) {
        if (pair.second > 0.0f) {
            win_above += pair.second;
        } else {
            lose_above -= pair.second;  // Make positive for calculation
        }
    }
    
    float best_pf = (lose_above > 0) ? (win_above / lose_above) : std::numeric_limits<float>::max();
    float best_threshold = pred_return_pairs[0].first;
    
    // Minimum number of samples to keep
    int min_kept = std::max(1, static_cast<int>(pred_return_pairs.size() * min_kept_percent / 100.0f));
    
    // Search all possible thresholds
    for (size_t i = 0; i < pred_return_pairs.size() - 1; ++i) {
        // Remove this case from "above" set
        if (pred_return_pairs[i].second > 0.0f) {
            win_above -= pred_return_pairs[i].second;
        } else {
            lose_above += pred_return_pairs[i].second;
        }
        
        // Skip if this is part of a tied block
        if (pred_return_pairs[i+1].first == pred_return_pairs[i].first) {
            continue;
        }
        
        // Check if we have enough samples above threshold
        int samples_above = static_cast<int>(pred_return_pairs.size()) - static_cast<int>(i) - 1;
        if (samples_above >= min_kept && lose_above > 0) {
            float pf = win_above / lose_above;
            if (pf > best_pf) {
                best_pf = pf;
                best_threshold = pred_return_pairs[i+1].first;
            }
        }
    }
    
    return best_threshold;
}

float ThresholdCalculator::CalculateOptimalShortThreshold(const std::vector<float>& predictions,
                                                           const std::vector<float>& returns,
                                                           int min_kept_percent) {
    if (predictions.empty() || returns.empty() || predictions.size() != returns.size()) {
        return 0.0f;
    }
    
    // Create sorted pairs of (prediction, return)
    std::vector<std::pair<float, float>> pred_return_pairs;
    for (size_t i = 0; i < predictions.size(); ++i) {
        pred_return_pairs.push_back({predictions[i], returns[i]});
    }
    
    // Sort by prediction value (descending for shorts - we want to start from highest)
    std::sort(pred_return_pairs.begin(), pred_return_pairs.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    // Initialize with complete set shorting (all positions below maximum threshold)
    float win_below = 0.0f;
    float lose_below = 0.0f;
    
    for (const auto& pair : pred_return_pairs) {
        // For shorts, we profit when return is negative (inverted)
        float short_return = -pair.second;
        if (short_return > 0.0f) {
            win_below += short_return;
        } else {
            lose_below -= short_return;  // Make positive for calculation
        }
    }
    
    float best_pf = (lose_below > 0) ? (win_below / lose_below) : std::numeric_limits<float>::max();
    float best_threshold = pred_return_pairs[0].first;
    
    // Minimum number of samples to keep
    int min_kept = std::max(1, static_cast<int>(pred_return_pairs.size() * min_kept_percent / 100.0f));
    
    // Search all possible thresholds
    for (size_t i = 0; i < pred_return_pairs.size() - 1; ++i) {
        // Remove this case from "below" set
        float short_return = -pred_return_pairs[i].second;
        if (short_return > 0.0f) {
            win_below -= short_return;
        } else {
            lose_below += short_return;
        }
        
        // Skip if this is part of a tied block
        if (pred_return_pairs[i+1].first == pred_return_pairs[i].first) {
            continue;
        }
        
        // Check if we have enough samples below threshold
        int samples_below = static_cast<int>(pred_return_pairs.size()) - static_cast<int>(i) - 1;
        if (samples_below >= min_kept && lose_below > 0) {
            float pf = win_below / lose_below;
            if (pf > best_pf) {
                best_pf = pf;
                best_threshold = pred_return_pairs[i+1].first;
            }
        }
    }
    
    return best_threshold;
}

float ThresholdCalculator::CalculateThreshold(ThresholdMethod method,
                                               const std::vector<float>& predictions,
                                               const std::vector<float>& returns,
                                               float percentile) {
    switch (method) {
        case ThresholdMethod::Percentile95:
            return CalculatePercentileThreshold(predictions, percentile);
        
        case ThresholdMethod::OptimalROC:
            return CalculateOptimalThreshold(predictions, returns);
        
        default:
            return CalculatePercentileThreshold(predictions, percentile);
    }
}

float ThresholdCalculator::CalculateProfitFactor(const std::vector<float>& predictions,
                                                  const std::vector<float>& returns,
                                                  float threshold) {
    if (predictions.empty() || returns.empty() || predictions.size() != returns.size()) {
        return 0.0f;
    }
    
    float total_wins = 0.0f;
    float total_losses = 0.0f;
    
    for (size_t i = 0; i < predictions.size(); ++i) {
        if (predictions[i] > threshold) {
            if (returns[i] > 0.0f) {
                total_wins += returns[i];
            } else {
                total_losses -= returns[i];  // Make positive
            }
        }
    }
    
    if (total_losses > 0) {
        return total_wins / total_losses;
    } else if (total_wins > 0) {
        return std::numeric_limits<float>::max();
    } else {
        return 0.0f;
    }
}

ThresholdCalculator::ProfitFactorResult ThresholdCalculator::CalculateProfitFactorDetailed(
    const std::vector<float>& predictions,
    const std::vector<float>& returns,
    float threshold) {
    
    ProfitFactorResult result = {0.0f, 0.0f, 0.0f, 0, 0};
    
    if (predictions.empty() || returns.empty() || predictions.size() != returns.size()) {
        return result;
    }
    
    for (size_t i = 0; i < predictions.size(); ++i) {
        if (predictions[i] > threshold) {
            result.num_trades++;
            if (returns[i] > 0.0f) {
                result.total_wins += returns[i];
                result.num_winning_trades++;
            } else {
                result.total_losses -= returns[i];  // Make positive
            }
        }
    }
    
    if (result.total_losses > 0) {
        result.profit_factor = result.total_wins / result.total_losses;
    } else if (result.total_wins > 0) {
        result.profit_factor = std::numeric_limits<float>::max();
    } else {
        result.profit_factor = 0.0f;
    }
    
    return result;
}

} // namespace simulation
#include "PerformanceMetrics.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace simulation {
namespace metrics {

PerformanceMetrics::RegressionMetrics PerformanceMetrics::Calculate(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals,
    int num_parameters,
    float risk_free_rate) {
    
    if (predictions.size() != actuals.size() || predictions.empty()) {
        throw std::invalid_argument("Predictions and actuals must have same non-zero size");
    }
    
    RegressionMetrics metrics = {};
    size_t n = predictions.size();
    
    // Basic regression metrics
    metrics.mse = CalculateMSE(predictions, actuals);
    metrics.rmse = std::sqrt(metrics.mse);
    metrics.mae = CalculateMAE(predictions, actuals);
    metrics.mape = CalculateMAPE(predictions, actuals);
    metrics.r2 = CalculateR2(predictions, actuals);
    
    // Adjusted R²
    if (num_parameters > 0 && n > num_parameters + 1) {
        metrics.adjusted_r2 = 1.0f - (1.0f - metrics.r2) * (n - 1) / (n - num_parameters - 1);
    } else {
        metrics.adjusted_r2 = metrics.r2;
    }
    
    // Directional accuracy
    metrics.directional_accuracy = CalculateDirectionalAccuracy(predictions, actuals);
    
    // Trading metrics (using 0 as threshold for simplicity)
    float threshold = 0.0f;
    CalculateTradingMetrics(predictions, actuals, threshold,
        metrics.hit_rate, metrics.avg_win, metrics.avg_loss, metrics.profit_factor);
    
    if (metrics.avg_loss != 0) {
        metrics.win_loss_ratio = metrics.avg_win / std::abs(metrics.avg_loss);
    }
    
    // Risk metrics
    std::vector<float> returns = CalculateReturns(actuals);
    if (!returns.empty()) {
        std::vector<float> cumulative = CalculateCumulativeReturns(returns);
        metrics.max_drawdown = CalculateMaxDrawdown(cumulative);
        metrics.sharpe_ratio = CalculateSharpeRatio(returns, risk_free_rate);
        
        // Sortino ratio (using downside deviation)
        std::vector<float> downside_returns;
        for (float r : returns) {
            if (r < risk_free_rate) {
                downside_returns.push_back(r - risk_free_rate);
            }
        }
        if (!downside_returns.empty()) {
            float downside_dev = std::sqrt(
                std::accumulate(downside_returns.begin(), downside_returns.end(), 0.0f,
                    [](float sum, float r) { return sum + r * r; }) / downside_returns.size()
            );
            if (downside_dev > 0) {
                float avg_return = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
                metrics.sortino_ratio = (avg_return - risk_free_rate) / downside_dev;
            }
        }
        
        // Calmar ratio
        if (metrics.max_drawdown > 0) {
            float total_return = cumulative.empty() ? 0 : cumulative.back();
            float annualized_return = total_return; // Simplified - should annualize properly
            metrics.calmar_ratio = annualized_return / metrics.max_drawdown;
        }
    }
    
    // Statistical tests
    std::vector<float> residuals = CalculateResiduals(predictions, actuals);
    metrics.durbin_watson = CalculateDurbinWatson(residuals);
    
    // Information criteria (simplified - need log likelihood)
    if (num_parameters > 0) {
        float log_likelihood = -n * std::log(2 * M_PI) / 2 - n * std::log(metrics.mse) / 2 - n / 2;
        metrics.aic = CalculateAIC(log_likelihood, num_parameters);
        metrics.bic = CalculateBIC(log_likelihood, num_parameters, n);
    }
    
    return metrics;
}

float PerformanceMetrics::CalculateMSE(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals) {
    
    float sum_sq_error = 0.0f;
    for (size_t i = 0; i < predictions.size(); ++i) {
        float error = predictions[i] - actuals[i];
        sum_sq_error += error * error;
    }
    return sum_sq_error / predictions.size();
}

float PerformanceMetrics::CalculateMAE(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals) {
    
    float sum_abs_error = 0.0f;
    for (size_t i = 0; i < predictions.size(); ++i) {
        sum_abs_error += std::abs(predictions[i] - actuals[i]);
    }
    return sum_abs_error / predictions.size();
}

float PerformanceMetrics::CalculateMAPE(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals) {
    
    float sum_pct_error = 0.0f;
    int valid_count = 0;
    
    for (size_t i = 0; i < predictions.size(); ++i) {
        if (std::abs(actuals[i]) > 1e-10) {  // Avoid division by zero
            sum_pct_error += std::abs((actuals[i] - predictions[i]) / actuals[i]);
            valid_count++;
        }
    }
    
    return valid_count > 0 ? (sum_pct_error / valid_count) * 100.0f : 0.0f;
}

float PerformanceMetrics::CalculateR2(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals) {
    
    float mean_actual = std::accumulate(actuals.begin(), actuals.end(), 0.0f) / actuals.size();
    
    float ss_res = 0.0f;  // Residual sum of squares
    float ss_tot = 0.0f;  // Total sum of squares
    
    for (size_t i = 0; i < predictions.size(); ++i) {
        float residual = actuals[i] - predictions[i];
        ss_res += residual * residual;
        
        float deviation = actuals[i] - mean_actual;
        ss_tot += deviation * deviation;
    }
    
    if (ss_tot == 0) return 0.0f;
    return 1.0f - (ss_res / ss_tot);
}

float PerformanceMetrics::CalculateDirectionalAccuracy(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals) {
    
    int correct = 0;
    for (size_t i = 0; i < predictions.size(); ++i) {
        // Check if signs match (both positive or both negative)
        if ((predictions[i] > 0 && actuals[i] > 0) ||
            (predictions[i] < 0 && actuals[i] < 0) ||
            (predictions[i] == 0 && actuals[i] == 0)) {
            correct++;
        }
    }
    return (float)correct / predictions.size();
}

void PerformanceMetrics::CalculateTradingMetrics(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals,
    float threshold,
    float& hit_rate,
    float& avg_win,
    float& avg_loss,
    float& profit_factor) {
    
    std::vector<float> wins, losses;
    int signals = 0;
    int hits = 0;
    
    for (size_t i = 0; i < predictions.size(); ++i) {
        if (predictions[i] > threshold) {
            signals++;
            if (actuals[i] > 0) {
                hits++;
                wins.push_back(actuals[i]);
            } else {
                losses.push_back(actuals[i]);
            }
        }
    }
    
    hit_rate = signals > 0 ? (float)hits / signals : 0.0f;
    
    avg_win = wins.empty() ? 0.0f : 
        std::accumulate(wins.begin(), wins.end(), 0.0f) / wins.size();
    
    avg_loss = losses.empty() ? 0.0f :
        std::accumulate(losses.begin(), losses.end(), 0.0f) / losses.size();
    
    float total_wins = std::accumulate(wins.begin(), wins.end(), 0.0f);
    float total_losses = std::abs(std::accumulate(losses.begin(), losses.end(), 0.0f));
    
    profit_factor = total_losses > 0 ? total_wins / total_losses : 
                    (total_wins > 0 ? std::numeric_limits<float>::max() : 0.0f);
}

float PerformanceMetrics::CalculateMaxDrawdown(const std::vector<float>& cumulative_returns) {
    if (cumulative_returns.empty()) return 0.0f;
    
    float max_drawdown = 0.0f;
    float peak = cumulative_returns[0];
    
    for (float value : cumulative_returns) {
        if (value > peak) {
            peak = value;
        }
        float drawdown = (peak - value) / (peak != 0 ? std::abs(peak) : 1.0f);
        max_drawdown = std::max(max_drawdown, drawdown);
    }
    
    return max_drawdown;
}

float PerformanceMetrics::CalculateSharpeRatio(
    const std::vector<float>& returns,
    float risk_free_rate) {
    
    if (returns.empty()) return 0.0f;
    
    float mean_return = std::accumulate(returns.begin(), returns.end(), 0.0f) / returns.size();
    
    float variance = 0.0f;
    for (float r : returns) {
        float diff = r - mean_return;
        variance += diff * diff;
    }
    
    float std_dev = std::sqrt(variance / returns.size());
    
    if (std_dev == 0) return 0.0f;
    
    return (mean_return - risk_free_rate) / std_dev;
}

float PerformanceMetrics::CalculateDurbinWatson(const std::vector<float>& residuals) {
    if (residuals.size() < 2) return 2.0f;  // No autocorrelation
    
    float sum_sq_diff = 0.0f;
    float sum_sq = 0.0f;
    
    for (size_t i = 1; i < residuals.size(); ++i) {
        float diff = residuals[i] - residuals[i-1];
        sum_sq_diff += diff * diff;
    }
    
    for (float r : residuals) {
        sum_sq += r * r;
    }
    
    if (sum_sq == 0) return 2.0f;
    
    return sum_sq_diff / sum_sq;  // Should be around 2 for no autocorrelation
}

float PerformanceMetrics::CalculateAIC(float log_likelihood, int num_parameters) {
    return 2 * num_parameters - 2 * log_likelihood;
}

float PerformanceMetrics::CalculateBIC(float log_likelihood, int num_parameters, int num_samples) {
    return std::log(num_samples) * num_parameters - 2 * log_likelihood;
}

std::vector<float> PerformanceMetrics::CalculateResiduals(
    const std::vector<float>& predictions,
    const std::vector<float>& actuals) {
    
    std::vector<float> residuals;
    residuals.reserve(predictions.size());
    
    for (size_t i = 0; i < predictions.size(); ++i) {
        residuals.push_back(actuals[i] - predictions[i]);
    }
    
    return residuals;
}

std::vector<float> PerformanceMetrics::CalculateReturns(const std::vector<float>& values) {
    std::vector<float> returns;
    if (values.size() < 2) return returns;
    
    returns.reserve(values.size() - 1);
    for (size_t i = 1; i < values.size(); ++i) {
        if (values[i-1] != 0) {
            returns.push_back((values[i] - values[i-1]) / values[i-1]);
        } else {
            returns.push_back(0.0f);
        }
    }
    
    return returns;
}

std::vector<float> PerformanceMetrics::CalculateCumulativeReturns(const std::vector<float>& returns) {
    std::vector<float> cumulative;
    if (returns.empty()) return cumulative;
    
    cumulative.reserve(returns.size());
    float cum_return = 0.0f;
    
    for (float r : returns) {
        cum_return += r;
        cumulative.push_back(cum_return);
    }
    
    return cumulative;
}

// ModelComparison implementation
ModelComparison::ComparisonResult ModelComparison::Compare(
    const std::map<std::string, PerformanceMetrics::RegressionMetrics>& models,
    const std::vector<std::string>& priority_metrics) {
    
    ComparisonResult result;
    result.model_metrics = models;
    
    if (models.empty()) {
        return result;
    }
    
    // Calculate composite scores
    for (const auto& [model_name, metrics] : models) {
        float score = CalculateCompositeScore(metrics);
        result.ranking_scores[model_name] = score;
    }
    
    // Find best model
    auto best_it = std::max_element(result.ranking_scores.begin(), result.ranking_scores.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (best_it != result.ranking_scores.end()) {
        result.best_model = best_it->first;
    }
    
    // Generate recommendations
    for (const auto& [model_name, metrics] : models) {
        if (metrics.sharpe_ratio > 1.0f) {
            result.recommendations.push_back(model_name + " has good risk-adjusted returns");
        }
        if (metrics.max_drawdown > 0.2f) {
            result.recommendations.push_back(model_name + " has high drawdown risk");
        }
        if (metrics.directional_accuracy > 0.6f) {
            result.recommendations.push_back(model_name + " has good directional accuracy");
        }
    }
    
    return result;
}

std::vector<std::pair<std::string, float>> ModelComparison::RankByMetric(
    const std::map<std::string, PerformanceMetrics::RegressionMetrics>& models,
    const std::string& metric_name) {
    
    std::vector<std::pair<std::string, float>> rankings;
    
    for (const auto& [model_name, metrics] : models) {
        float value = 0.0f;
        
        // Extract metric value
        if (metric_name == "mse") value = -metrics.mse;  // Negative because lower is better
        else if (metric_name == "rmse") value = -metrics.rmse;
        else if (metric_name == "mae") value = -metrics.mae;
        else if (metric_name == "r2") value = metrics.r2;
        else if (metric_name == "sharpe_ratio") value = metrics.sharpe_ratio;
        else if (metric_name == "directional_accuracy") value = metrics.directional_accuracy;
        else if (metric_name == "max_drawdown") value = -metrics.max_drawdown;
        
        rankings.push_back({model_name, value});
    }
    
    // Sort by value (descending)
    std::sort(rankings.begin(), rankings.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    return rankings;
}

float ModelComparison::CalculateCompositeScore(
    const PerformanceMetrics::RegressionMetrics& metrics,
    const std::map<std::string, float>& weights) {
    
    float score = 0.0f;
    float total_weight = 0.0f;
    
    for (const auto& [metric_name, weight] : weights) {
        float value = 0.0f;
        
        if (metric_name == "sharpe_ratio") value = metrics.sharpe_ratio;
        else if (metric_name == "r2") value = metrics.r2;
        else if (metric_name == "directional_accuracy") value = metrics.directional_accuracy;
        else if (metric_name == "mae") value = 1.0f / (1.0f + metrics.mae);  // Inverse for error metrics
        else if (metric_name == "max_drawdown") value = 1.0f - metrics.max_drawdown;
        
        score += value * weight;
        total_weight += weight;
    }
    
    return total_weight > 0 ? score / total_weight : 0.0f;
}

// PerformanceTracker implementation
void PerformanceTracker::AddFoldMetrics(int fold_number, const PerformanceMetrics::RegressionMetrics& metrics) {
    m_fold_metrics[fold_number] = metrics;
}

PerformanceMetrics::RegressionMetrics PerformanceTracker::GetAverageMetrics() const {
    if (m_fold_metrics.empty()) {
        return {};
    }
    
    PerformanceMetrics::RegressionMetrics avg = {};
    
    for (const auto& [fold, metrics] : m_fold_metrics) {
        avg.mse += metrics.mse;
        avg.rmse += metrics.rmse;
        avg.mae += metrics.mae;
        avg.mape += metrics.mape;
        avg.r2 += metrics.r2;
        avg.adjusted_r2 += metrics.adjusted_r2;
        avg.directional_accuracy += metrics.directional_accuracy;
        avg.hit_rate += metrics.hit_rate;
        avg.sharpe_ratio += metrics.sharpe_ratio;
        avg.max_drawdown += metrics.max_drawdown;
    }
    
    float n = m_fold_metrics.size();
    avg.mse /= n;
    avg.rmse /= n;
    avg.mae /= n;
    avg.mape /= n;
    avg.r2 /= n;
    avg.adjusted_r2 /= n;
    avg.directional_accuracy /= n;
    avg.hit_rate /= n;
    avg.sharpe_ratio /= n;
    avg.max_drawdown /= n;
    
    return avg;
}

std::vector<float> PerformanceTracker::GetMetricHistory(const std::string& metric_name) const {
    std::vector<float> history;
    
    // Extract metrics in fold order
    std::vector<std::pair<int, float>> fold_values;
    for (const auto& [fold, metrics] : m_fold_metrics) {
        float value = 0.0f;
        
        if (metric_name == "mse") value = metrics.mse;
        else if (metric_name == "rmse") value = metrics.rmse;
        else if (metric_name == "mae") value = metrics.mae;
        else if (metric_name == "r2") value = metrics.r2;
        else if (metric_name == "sharpe_ratio") value = metrics.sharpe_ratio;
        else if (metric_name == "directional_accuracy") value = metrics.directional_accuracy;
        
        fold_values.push_back({fold, value});
    }
    
    // Sort by fold number
    std::sort(fold_values.begin(), fold_values.end());
    
    // Extract values
    for (const auto& [fold, value] : fold_values) {
        history.push_back(value);
    }
    
    return history;
}

bool PerformanceTracker::IsPerformanceDegrading(const std::string& metric_name, int window_size) const {
    auto history = GetMetricHistory(metric_name);
    
    if (history.size() < (size_t)window_size * 2) {
        return false;  // Not enough data
    }
    
    // Compare recent window average with previous window
    float recent_avg = 0.0f;
    float previous_avg = 0.0f;
    
    size_t start_recent = history.size() - window_size;
    size_t start_previous = start_recent - window_size;
    
    for (int i = 0; i < window_size; ++i) {
        recent_avg += history[start_recent + i];
        previous_avg += history[start_previous + i];
    }
    
    recent_avg /= window_size;
    previous_avg /= window_size;
    
    // Check if degrading (depends on metric)
    if (metric_name == "mse" || metric_name == "rmse" || metric_name == "mae") {
        return recent_avg > previous_avg * 1.1f;  // 10% worse for error metrics
    } else {
        return recent_avg < previous_avg * 0.9f;  // 10% worse for good metrics
    }
}

} // namespace metrics
} // namespace simulation
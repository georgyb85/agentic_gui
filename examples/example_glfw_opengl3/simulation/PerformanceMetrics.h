#pragma once

#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace simulation {
namespace metrics {

// Universal performance metrics that apply to all regression models
class PerformanceMetrics {
public:
    struct RegressionMetrics {
        // Basic metrics
        float mse;          // Mean Squared Error
        float rmse;         // Root Mean Squared Error
        float mae;          // Mean Absolute Error
        float mape;         // Mean Absolute Percentage Error
        float r2;           // R-squared
        float adjusted_r2;  // Adjusted R-squared
        
        // Directional metrics (for trading)
        float directional_accuracy;  // % of correct direction predictions
        float hit_rate;              // % of profitable trades
        float avg_win;               // Average winning trade
        float avg_loss;              // Average losing trade
        float win_loss_ratio;        // avg_win / avg_loss
        float profit_factor;         // sum(wins) / sum(losses)
        
        // Risk metrics
        float max_drawdown;         // Maximum drawdown
        float sharpe_ratio;         // Risk-adjusted return
        float sortino_ratio;        // Downside risk-adjusted return
        float calmar_ratio;         // Return / Max Drawdown
        
        // Statistical tests
        float durbin_watson;        // Test for autocorrelation in residuals
        float jarque_bera;          // Test for normality of residuals
        float ljung_box;            // Test for independence of residuals
        
        // Information criteria (for model selection)
        float aic;                  // Akaike Information Criterion
        float bic;                  // Bayesian Information Criterion
        
        // Convert to map for display
        std::map<std::string, float> ToMap() const {
            return {
                {"MSE", mse},
                {"RMSE", rmse},
                {"MAE", mae},
                {"MAPE", mape},
                {"R²", r2},
                {"Adjusted R²", adjusted_r2},
                {"Directional Accuracy", directional_accuracy},
                {"Hit Rate", hit_rate},
                {"Avg Win", avg_win},
                {"Avg Loss", avg_loss},
                {"Win/Loss Ratio", win_loss_ratio},
                {"Profit Factor", profit_factor},
                {"Max Drawdown", max_drawdown},
                {"Sharpe Ratio", sharpe_ratio},
                {"Sortino Ratio", sortino_ratio},
                {"Calmar Ratio", calmar_ratio},
                {"Durbin-Watson", durbin_watson},
                {"AIC", aic},
                {"BIC", bic}
            };
        }
    };
    
    // Calculate all metrics
    static RegressionMetrics Calculate(
        const std::vector<float>& predictions,
        const std::vector<float>& actuals,
        int num_parameters = 0,  // For adjusted R² and information criteria
        float risk_free_rate = 0.0f  // For Sharpe ratio
    );
    
    // Individual metric calculations
    static float CalculateMSE(const std::vector<float>& predictions, const std::vector<float>& actuals);
    static float CalculateMAE(const std::vector<float>& predictions, const std::vector<float>& actuals);
    static float CalculateMAPE(const std::vector<float>& predictions, const std::vector<float>& actuals);
    static float CalculateR2(const std::vector<float>& predictions, const std::vector<float>& actuals);
    
    // Trading-specific metrics
    static float CalculateDirectionalAccuracy(
        const std::vector<float>& predictions, 
        const std::vector<float>& actuals
    );
    
    static void CalculateTradingMetrics(
        const std::vector<float>& predictions,
        const std::vector<float>& actuals,
        float threshold,
        float& hit_rate,
        float& avg_win,
        float& avg_loss,
        float& profit_factor
    );
    
    static float CalculateMaxDrawdown(const std::vector<float>& cumulative_returns);
    static float CalculateSharpeRatio(
        const std::vector<float>& returns,
        float risk_free_rate = 0.0f
    );
    
    // Statistical tests
    static float CalculateDurbinWatson(const std::vector<float>& residuals);
    
    // Model selection criteria
    static float CalculateAIC(float log_likelihood, int num_parameters);
    static float CalculateBIC(float log_likelihood, int num_parameters, int num_samples);
    
    // Utility functions
    static std::vector<float> CalculateResiduals(
        const std::vector<float>& predictions,
        const std::vector<float>& actuals
    );
    
    static std::vector<float> CalculateReturns(const std::vector<float>& values);
    static std::vector<float> CalculateCumulativeReturns(const std::vector<float>& returns);
};

// Metrics comparison for model selection
class ModelComparison {
public:
    struct ComparisonResult {
        std::string best_model;
        std::map<std::string, PerformanceMetrics::RegressionMetrics> model_metrics;
        std::map<std::string, float> ranking_scores;  // Composite scores for ranking
        std::vector<std::string> recommendations;
    };
    
    // Compare multiple models
    static ComparisonResult Compare(
        const std::map<std::string, PerformanceMetrics::RegressionMetrics>& models,
        const std::vector<std::string>& priority_metrics = {"sharpe_ratio", "r2", "mae"}
    );
    
    // Rank models by specific metric
    static std::vector<std::pair<std::string, float>> RankByMetric(
        const std::map<std::string, PerformanceMetrics::RegressionMetrics>& models,
        const std::string& metric_name
    );
    
    // Calculate composite score for ranking
    static float CalculateCompositeScore(
        const PerformanceMetrics::RegressionMetrics& metrics,
        const std::map<std::string, float>& weights = {
            {"sharpe_ratio", 0.3f},
            {"r2", 0.2f},
            {"directional_accuracy", 0.2f},
            {"mae", 0.15f},
            {"max_drawdown", 0.15f}
        }
    );
};

// Performance tracking over time
class PerformanceTracker {
public:
    // Track metrics over multiple folds
    void AddFoldMetrics(int fold_number, const PerformanceMetrics::RegressionMetrics& metrics);
    
    // Get statistics over all folds
    PerformanceMetrics::RegressionMetrics GetAverageMetrics() const;
    PerformanceMetrics::RegressionMetrics GetMedianMetrics() const;
    std::map<std::string, std::pair<float, float>> GetMetricRanges() const; // min, max
    
    // Detect performance degradation
    bool IsPerformanceDegrading(const std::string& metric_name, int window_size = 5) const;
    
    // Get metrics history for plotting
    std::vector<float> GetMetricHistory(const std::string& metric_name) const;
    
private:
    std::map<int, PerformanceMetrics::RegressionMetrics> m_fold_metrics;
};

} // namespace metrics
} // namespace simulation
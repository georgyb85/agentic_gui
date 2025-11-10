#pragma once

#include <string>
#include <vector>
#include <chrono>
#include "imgui.h"
#include "ThresholdCalculator.h"

namespace simulation {

// Forward declarations
class ISimulationModel;

// Transformation parameters for data preprocessing
struct TransformParams {
    float mean = 0.0f;
    float std_dev = 1.0f;
    float scaling_factor = 0.001f;
};

// Result from a single fold in walk-forward validation
struct FoldResult {
    // Fold identification
    int fold_number;
    
    // Data split info
    int train_start, train_end;
    int test_start, test_end;
    int n_train_samples;
    int n_val_samples;
    int n_test_samples;
    
    // Features used for this fold (for feature schedule tracking)
    std::vector<std::string> features_used;
    
    // Training results
    int best_iteration;
    float best_score;
    bool model_learned_nothing;  // True when model doesn't improve from initialization
    bool used_cached_model;      // True when using previously cached model
    
    // Transformation parameters used
    float mean_scale, std_scale;
    
    // Trading thresholds
    float prediction_threshold_scaled;
    float prediction_threshold_original;
    float dynamic_positive_threshold;

    // Long trading thresholds (both stored for simulator selection)
    float long_threshold_95th;     // 95th percentile threshold (long)
    float long_threshold_optimal;  // Optimal threshold (PF-based) for longs

    // Short trading thresholds
    float short_threshold_scaled;
    float short_threshold_original;
    float short_threshold_5th;     // 5th percentile threshold (short)
    float short_threshold_optimal; // Optimal threshold (PF-based) for shorts
    
    // Trading results - Long trades
    int n_signals;
    float signal_sum;
    float signal_rate;
    float avg_return_on_signals;
    float median_return_on_signals;
    float std_return_on_signals;
    float hit_rate;
    float avg_predicted_return_on_signals;
    float running_sum;
    float running_sum_short;  // Running sum for short trades
    float running_sum_dual;   // Running sum for dual (long+short) trades
    
    // Trading results - Short trades
    int n_short_signals;
    float short_signal_sum;
    float short_signal_rate;
    float avg_return_on_short_signals;
    float short_hit_rate;
    float sum_short_wins;
    float sum_short_losses;
    
    // Profit factor metrics - Long trades
    float profit_factor_train;  // PF on training data (hypothetical)
    float profit_factor_test;   // PF on test data (actual)
    float sum_wins;            // Sum of winning trades
    float sum_losses;          // Sum of losing trades (absolute value)
    
    // Profit factor metrics - Short trades
    float profit_factor_short_train;
    float profit_factor_short_test;
    
    // Store raw predictions for this fold (optional)
    std::vector<float> test_predictions_original;  // Original scale predictions
    
    // Cached string representations for table display
    mutable std::string fold_str;
    mutable std::string signals_str;
    mutable std::string rate_str;
    mutable std::string return_str;
    mutable std::string hit_str;
    mutable std::string sum_str;
    mutable bool cache_dirty = true;
    
    void UpdateCache() const;
};

// Base configuration for all models
struct ModelConfigBase {
    // Feature engineering
    std::vector<std::string> feature_columns;
    std::string target_column;
    
    // Feature schedule for dynamic feature selection per fold
    std::string feature_schedule;  // Format: "startRow-endRow: feature1, feature2, ..."
    bool use_feature_schedule = false;
    
    // Data transformation
    bool use_standardization = false;
    bool use_tanh_transform = true;
    float tanh_scaling_factor = 0.001f;
    
    // Training parameters
    float val_split_ratio = 0.8f;
    int random_seed = 43;
    
    // Model reuse settings
    bool reuse_previous_model = false;
    
    // Threshold calculation method
    ThresholdMethod threshold_method = ThresholdMethod::Percentile95;
    
    // Performance options
    bool calculate_training_profit_factor = true;  // Can be disabled for speed
    
    virtual ~ModelConfigBase() = default;
};

// Configuration for walk-forward simulation
struct WalkForwardConfig {
    int train_size = 10000;        // Number of bars for training
    int test_size = 200;           // Number of bars for testing  
    int train_test_gap = 9;        // Gap between train and test sets
    int fold_step = 200;           // Step size between folds
                                   // Note: fold_step == test_size gives non-overlapping test sets
                                   //       fold_step < test_size gives overlapping test sets
                                   //       fold_step > test_size leaves gaps between test sets
    int start_fold = 50;           // Starting fold number
    int end_fold = -1;             // Ending fold number (-1 for auto)
    int initial_offset = 6000;     // Initial offset in data
};

// Model cache for reusing successful models
struct CachedModelData {
    bool is_valid = false;
    std::string model_type;
    std::vector<char> model_buffer;
    TransformParams transform_params;
    float prediction_threshold_scaled;
    float prediction_threshold_original;
    float dynamic_positive_threshold;
    int source_fold;
};

class ModelCache {
public:
    void CacheModel(
        const class ISimulationModel& model,
        const TransformParams& params,
        float pred_thresh_scaled,
        float pred_thresh_orig,
        float dyn_pos_thresh,
        int fold_number
    );
    
    bool LoadCachedModel(
        class ISimulationModel& model,
        TransformParams& params,
        float& pred_thresh_scaled,
        float& pred_thresh_orig,
        float& dyn_pos_thresh
    ) const;
    
    bool HasCachedModel() const { return m_cached.is_valid; }
    void Clear() { m_cached = CachedModelData(); }
    int GetSourceFold() const { return m_cached.source_fold; }
    
private:
    CachedModelData m_cached;
};

// Trade mode for plotting
enum class TradeMode {
    LongOnly,    // Only long trades
    ShortOnly,   // Only short trades
    Dual         // Both long and short trades
};

// Complete simulation run with results
struct SimulationRun {
    std::string name;
    std::string config_description;
    std::string model_type;  // "XGBoost", "LightGBM", etc.
    std::string dataset_measurement;
    std::string dataset_id;
    std::string prediction_measurement;
    
    // Configuration used (polymorphic - can be XGBoostConfig, etc.)
    std::unique_ptr<ModelConfigBase> config;
    WalkForwardConfig walk_forward_config;
    
    // Feature schedule info
    bool using_feature_schedule = false;
    std::string feature_schedule;  // The actual schedule text
    
    // Model predictions storage (all test predictions across all folds)
    std::vector<float> all_test_predictions;  // Flat array of all predictions
    std::vector<float> all_test_actuals;      // Corresponding actual values
    std::vector<int> fold_prediction_offsets; // Start index for each fold's predictions
    std::vector<int64_t> all_test_timestamps; // Unix timestamps (ms) for each prediction
    
    // Results
    std::vector<FoldResult> foldResults;
    
    // Profit plots for different trade modes
    std::vector<double> profitPlotX;
    std::vector<double> profitPlotY_long;   // Long-only cumulative profit
    std::vector<double> profitPlotY_short;  // Short-only cumulative profit
    std::vector<double> profitPlotY_dual;   // Dual (long+short) cumulative profit
    
    // Current display mode for plotting
    TradeMode currentPlotMode = TradeMode::LongOnly;
    
    // Visualization
    ImVec4 plotColor;
    
    // Timing
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    bool completed;
};

// Model prediction result
struct PredictionResult {
    std::vector<float> predictions;
    bool success;
    std::string error_message;
};

// Model training result  
struct TrainingResult {
    bool success;
    bool model_learned;
    int best_iteration;
    float best_score;
    float validation_threshold;
    TransformParams transform_params;
    std::vector<char> serialized_model;  // For caching
    std::string error_message;
};

} // namespace simulation

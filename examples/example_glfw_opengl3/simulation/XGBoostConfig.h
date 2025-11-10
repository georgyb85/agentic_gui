#pragma once

#include "SimulationTypes.h"
#include "ThresholdCalculator.h"

namespace simulation {

// XGBoost-specific configuration
struct XGBoostConfig : public ModelConfigBase {
    // Initialize base class defaults
    XGBoostConfig() {
        // Base class defaults important for signal generation
        use_tanh_transform = true;
        use_standardization = false;
        tanh_scaling_factor = 0.001f;
        val_split_ratio = 0.8f;
        random_seed = 43;
        reuse_previous_model = false;
        threshold_method = ThresholdMethod::Percentile95;  // Default to traditional method
    }
    // XGBoost hyperparameters
    float learning_rate = 0.01f;
    int max_depth = 4;
    float min_child_weight = 10.0f;
    float subsample = 0.8f;
    float colsample_bytree = 0.7f;
    float lambda = 2.0f;         // L2 regularization
    
    // Training control
    int num_boost_round = 2000;
    int early_stopping_rounds = 50;
    int min_boost_rounds = 100;  // Minimum iterations to ensure quality
    bool force_minimum_training = true;  // Force minimum iterations even if no improvement
    
    // Execution parameters
    std::string tree_method = "hist";
    std::string objective = "reg:squarederror";
    std::string device = "cuda";  // Will fallback to CPU if not available
    
    // Quantile parameters (only used when objective is reg:quantileerror)
    float quantile_alpha = 0.95f;  // For quantile regression (0.05 for 5th, 0.95 for 95th)
    
    // Threshold calculation method
    ThresholdMethod threshold_method = ThresholdMethod::Percentile95;
    
    // Copy configuration (for copy/paste functionality)
    XGBoostConfig& operator=(const XGBoostConfig& other) = default;
    
    // Get string representation for display
    std::string ToString() const {
        char buffer[512];
        std::string loss_str;
        if (objective == "reg:squarederror") {
            loss_str = "MSE";
        } else if (objective == "reg:quantileerror") {
            char q_buf[32];
            snprintf(q_buf, sizeof(q_buf), "Q%.0f%%", quantile_alpha * 100);
            loss_str = q_buf;
        } else {
            loss_str = objective;
        }
        
        snprintf(buffer, sizeof(buffer),
            "XGBoost[%s]: LR=%.3f, Depth=%d, MinChild=%.1f, Subsample=%.2f, ColSample=%.2f, Lambda=%.1f, Rounds=%d",
            loss_str.c_str(), learning_rate, max_depth, min_child_weight, subsample, colsample_bytree, lambda, num_boost_round);
        return std::string(buffer);
    }
};

} // namespace simulation
#pragma once

#include "imgui.h"
#include <string>

namespace stepwise {

// XGBoost configuration for stepwise selection
struct XGBoostConfig {
    // XGBoost hyperparameters
    float learning_rate = 0.01f;
    int max_depth = 4;
    float min_child_weight = 10.0f;
    float subsample = 0.8f;
    float colsample_bytree = 0.7f;
    float lambda = 2.0f;         // L2 regularization
    float alpha = 0.0f;          // L1 regularization
    
    // Training control
    int num_boost_round = 500;
    int early_stopping_rounds = 50;
    
    // Execution parameters
    std::string tree_method = "hist";
    std::string objective = "reg:squarederror";
    std::string device = "cpu";  // CPU by default for stepwise
    
    // Get string representation for display
    std::string ToString() const {
        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "XGBoost: LR=%.3f, Depth=%d, Rounds=%d, Lambda=%.1f",
            learning_rate, max_depth, num_boost_round, lambda);
        return std::string(buffer);
    }
};

// Widget for XGBoost configuration in stepwise selection
class XGBoostConfigWidget {
public:
    XGBoostConfigWidget() = default;
    ~XGBoostConfigWidget() = default;
    
    // Draw the configuration UI
    // Returns true if any value changed
    bool Draw();
    
    // Get/Set configuration
    const XGBoostConfig& GetConfig() const { return m_config; }
    void SetConfig(const XGBoostConfig& config) { m_config = config; }
    
    // Reset to defaults
    void ResetToDefaults() { m_config = XGBoostConfig(); }
    
private:
    XGBoostConfig m_config;
};

} // namespace stepwise
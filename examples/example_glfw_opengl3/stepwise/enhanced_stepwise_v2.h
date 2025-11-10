#pragma once

#include <string>
#include <vector>
#include <memory>
#include "data_matrix.h"
#include "stepwise_data_reader.h"
#include "enhanced_stepwise_selector_v2.h"
#include "model_interface.h"

namespace stepwise {

// Main interface for enhanced stepwise selection with pluggable models
class EnhancedStepwiseV2 {
public:
    // Configuration for the stepwise algorithm
    using StepwiseConfig = EnhancedStepwiseSelectorV2::SelectionConfig;
    
    // Results structure
    struct StepwiseResults {
        std::vector<int> selected_feature_indices;
        std::vector<std::string> selected_feature_names;
        std::string target_name;
        double final_r_square;
        std::vector<double> model_p_values;
        std::vector<double> change_p_values;
        std::vector<double> step_r_squares;
        std::vector<double> step_timing_ms;  // Timing for each step in milliseconds
        bool terminated_early;
        std::string termination_reason;
        size_t total_cases_loaded;
        size_t total_steps;
        std::vector<double> final_coefficients;  // Empty for non-linear models
        double total_elapsed_ms;  // Total algorithm runtime in milliseconds
        std::string model_type;  // Type of model used
        std::vector<float> feature_importances;  // For tree-based models
    };
    
private:
    StepwiseDataReader data_reader_;
    std::unique_ptr<EnhancedStepwiseSelectorV2> selector_;
    std::unique_ptr<DataMatrix> loaded_features_;
    std::vector<double> loaded_targets_;
    std::unique_ptr<IStepwiseModel> model_;
    
    // Fit final model and get coefficients (if applicable)
    std::vector<double> compute_final_coefficients(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& feature_indices
    ) const;
    
public:
    // Constructor with model
    explicit EnhancedStepwiseV2(
        std::unique_ptr<IStepwiseModel> model,
        const StepwiseConfig& config = StepwiseConfig{}
    );
    
    // Main entry point - run stepwise selection from file
    StepwiseResults run_from_file(
        const std::string& data_file_path,
        const std::vector<std::string>& feature_column_names,
        const std::string& target_column_name,
        int start_row = -1,  // -1 means use all rows
        int end_row = -1     // -1 means use all rows
    );
    
    // Run stepwise selection on pre-loaded data
    StepwiseResults run_on_data(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<std::string>& feature_names,
        const std::string& target_name = "TARGET"
    );
    
    // Configuration access
    const StepwiseConfig& get_config() const { return selector_->get_config(); }
    void set_config(const StepwiseConfig& config) { selector_->set_config(config); }
    
    // Model access
    void set_model(std::unique_ptr<IStepwiseModel> model) {
        model_ = std::move(model);
        if (selector_) {
            selector_->set_model(model_->clone());
        }
    }
    
    // Access to loaded data (after running from file)
    const DataMatrix* get_loaded_features() const { return loaded_features_.get(); }
    const std::vector<double>& get_loaded_targets() const { return loaded_targets_; }
    
    // Print results in formatted way
    static void print_results(const StepwiseResults& results);
    
    // Print final model coefficients (if applicable)
    static void print_model_coefficients(
        const StepwiseResults& results,
        const std::vector<std::string>& feature_names
    );
};

} // namespace stepwise
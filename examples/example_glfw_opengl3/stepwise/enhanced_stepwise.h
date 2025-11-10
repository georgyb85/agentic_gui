#pragma once

#include <string>
#include <vector>
#include <memory>
#include "data_matrix.h"
#include "stepwise_data_reader.h"
#include "enhanced_stepwise_selector.h"

// Main interface for enhanced stepwise selection
class EnhancedStepwise {
public:
    // Configuration for the stepwise algorithm
    using StepwiseConfig = EnhancedStepwiseSelector::SelectionConfig;
    
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
        std::vector<double> final_coefficients;
        double total_elapsed_ms;  // Total algorithm runtime in milliseconds
    };
    
private:
    StepwiseDataReader data_reader_;
    EnhancedStepwiseSelector selector_;
    std::unique_ptr<DataMatrix> loaded_features_;
    std::vector<double> loaded_targets_;
    
    // Fit final model and get coefficients
    std::vector<double> compute_final_coefficients(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& feature_indices
    ) const;
    
public:
    explicit EnhancedStepwise(const StepwiseConfig& config = StepwiseConfig{});
    
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
    const StepwiseConfig& get_config() const { return selector_.get_config(); }
    void set_config(const StepwiseConfig& config) { selector_.set_config(config); }
    
    // Access to loaded data (after running from file)
    const DataMatrix* get_loaded_features() const { return loaded_features_.get(); }
    const std::vector<double>& get_loaded_targets() const { return loaded_targets_; }
    
    // Print results in formatted way
    static void print_results(const StepwiseResults& results);
    
    // Print final model coefficients
    static void print_model_coefficients(
        const StepwiseResults& results,
        const std::vector<std::string>& feature_names
    );
};
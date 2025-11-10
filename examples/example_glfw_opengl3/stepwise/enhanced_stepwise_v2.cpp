#include "enhanced_stepwise_v2.h"
#include "../simple_logger.h"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace stepwise {

EnhancedStepwiseV2::EnhancedStepwiseV2(
    std::unique_ptr<IStepwiseModel> model,
    const StepwiseConfig& config)
    : model_(std::move(model)) {
    selector_ = std::make_unique<EnhancedStepwiseSelectorV2>(model_->clone(), config);
}

EnhancedStepwiseV2::StepwiseResults EnhancedStepwiseV2::run_from_file(
    const std::string& data_file_path,
    const std::vector<std::string>& feature_column_names,
    const std::string& target_column_name,
    int start_row,
    int end_row) {
    
    // Load data from file
    auto loaded_data = data_reader_.load_space_separated_file(
        data_file_path,
        feature_column_names,
        target_column_name,
        start_row,
        end_row
    );
    
    if (!loaded_data.features || loaded_data.target.empty()) {
        throw std::runtime_error("Failed to load data from file");
    }
    
    loaded_features_ = std::move(loaded_data.features);
    loaded_targets_ = std::move(loaded_data.target);
    
    return run_on_data(*loaded_features_, loaded_targets_, feature_column_names, target_column_name);
}

EnhancedStepwiseV2::StepwiseResults EnhancedStepwiseV2::run_on_data(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<std::string>& feature_names,
    const std::string& target_name) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Set feature names on the data matrix
    const_cast<DataMatrix&>(X).set_column_names(feature_names);
    
    // Run selection algorithm
    auto selection_results = selector_->select_features(X, y);
    
    // Convert to StepwiseResults
    StepwiseResults results;
    results.target_name = target_name;
    results.model_type = selection_results.model_type;
    results.terminated_early = selection_results.terminated_early;
    results.termination_reason = selection_results.termination_reason;
    results.total_cases_loaded = X.rows();
    results.total_steps = selection_results.total_steps;
    
    // Extract final feature indices and names
    if (selection_results.final_feature_set.n_features > 0) {
        results.selected_feature_indices = selection_results.final_feature_set.feature_indices;
        for (int idx : results.selected_feature_indices) {
            if (idx < static_cast<int>(feature_names.size())) {
                results.selected_feature_names.push_back(feature_names[idx]);
            }
        }
        results.final_r_square = selection_results.final_feature_set.cv_score;
    }
    
    // Extract step information
    for (const auto& step : selection_results.steps) {
        if (!step.best_feature_sets.empty()) {
            results.step_r_squares.push_back(step.step_performance);
            results.model_p_values.push_back(step.model_p_value);
            results.change_p_values.push_back(step.change_p_value);
            results.step_timing_ms.push_back(step.step_elapsed_ms);
        }
    }
    
    // Compute final coefficients if model supports it
    if (model_->has_coefficients() && !results.selected_feature_indices.empty()) {
        results.final_coefficients = compute_final_coefficients(X, y, results.selected_feature_indices);
    }
    
    // Get feature importances for tree-based models (simplified for now)
    // In a full implementation, this would extract importances from XGBoost
    
    auto end_time = std::chrono::high_resolution_clock::now();
    results.total_elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    return results;
}

std::vector<double> EnhancedStepwiseV2::compute_final_coefficients(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& feature_indices) const {
    
    // Fit final model
    auto final_model = model_->clone();
    final_model->fit(X, y, feature_indices);
    
    // Get coefficients
    return final_model->get_coefficients();
}

void EnhancedStepwiseV2::print_results(const StepwiseResults& results) {
    std::cout << "\n=== Enhanced Stepwise Selection Results ===" << std::endl;
    std::cout << "Model Type: " << results.model_type << std::endl;
    std::cout << "Target: " << results.target_name << std::endl;
    std::cout << "Total cases: " << results.total_cases_loaded << std::endl;
    std::cout << "Total steps: " << results.total_steps << std::endl;
    
    if (results.terminated_early) {
        std::cout << "Early termination: " << results.termination_reason << std::endl;
    }
    
    std::cout << "\nSelected features (" << results.selected_feature_names.size() << "):" << std::endl;
    for (size_t i = 0; i < results.selected_feature_names.size(); ++i) {
        std::cout << "  " << (i+1) << ". " << results.selected_feature_names[i];
        if (i < results.model_p_values.size() && results.model_p_values[i] > 0) {
            std::cout << " (p=" << std::fixed << std::setprecision(4) << results.model_p_values[i] << ")";
        }
        std::cout << std::endl;
    }
    
    std::cout << "\nFinal R-squared: " << std::fixed << std::setprecision(4) 
              << results.final_r_square << std::endl;
    
    if (!results.step_r_squares.empty()) {
        std::cout << "\nStep-by-step R-squared:" << std::endl;
        for (size_t i = 0; i < results.step_r_squares.size(); ++i) {
            std::cout << "  Step " << (i+1) << ": " 
                      << std::fixed << std::setprecision(4) << results.step_r_squares[i];
            if (i < results.change_p_values.size() && results.change_p_values[i] > 0) {
                std::cout << " (change p=" << results.change_p_values[i] << ")";
            }
            std::cout << std::endl;
        }
    }
    
    std::cout << "\nTotal elapsed time: " << std::fixed << std::setprecision(1) 
              << results.total_elapsed_ms << " ms" << std::endl;
}

void EnhancedStepwiseV2::print_model_coefficients(
    const StepwiseResults& results,
    const std::vector<std::string>& feature_names) {
    
    if (results.final_coefficients.empty()) {
        std::cout << "\nNo coefficients available for " << results.model_type << " model" << std::endl;
        return;
    }
    
    std::cout << "\n=== Model Coefficients ===" << std::endl;
    
    if (results.final_coefficients.size() > 0) {
        std::cout << "Intercept: " << std::fixed << std::setprecision(6) 
                  << results.final_coefficients[0] << std::endl;
    }
    
    for (size_t i = 0; i < results.selected_feature_names.size(); ++i) {
        if (i + 1 < results.final_coefficients.size()) {
            std::cout << results.selected_feature_names[i] << ": " 
                      << std::fixed << std::setprecision(6) 
                      << results.final_coefficients[i + 1] << std::endl;
        }
    }
}

} // namespace stepwise
#include "enhanced_stepwise.h"
#include "linear_quadratic_model.h"
#include "../simple_logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>

EnhancedStepwise::EnhancedStepwise(const StepwiseConfig& config) 
    : selector_(config) {
}

EnhancedStepwise::StepwiseResults EnhancedStepwise::run_from_file(
    const std::string& data_file_path,
    const std::vector<std::string>& feature_column_names,
    const std::string& target_column_name,
    int start_row,
    int end_row) {
    
    StepwiseResults results;
    results.target_name = target_column_name;
    results.terminated_early = false;
    results.total_steps = 0;
    results.total_elapsed_ms = 0.0;
    
    auto algorithm_start = std::chrono::high_resolution_clock::now();
    
    try {
        // Load data from file
        SimpleLogger::Log("Loading data from: " + data_file_path);
        if (start_row >= 0 && end_row >= 0) {
            std::ostringstream row_info;
            row_info << "Using row range: " << start_row << "-" << end_row;
            SimpleLogger::Log(row_info.str());
        }
        auto loaded_data = data_reader_.load_space_separated_file(
            data_file_path, feature_column_names, target_column_name, start_row, end_row
        );
        
        if (!loaded_data.features || loaded_data.features->rows() == 0) {
            results.termination_reason = "No valid data loaded from file";
            return results;
        }
        
        // Store loaded data for later access
        loaded_features_ = std::move(loaded_data.features);
        loaded_targets_ = std::move(loaded_data.target);
        
        results.total_cases_loaded = loaded_data.n_cases_loaded;
        
        std::ostringstream msg;
        msg << "Loaded " << results.total_cases_loaded << " cases with " 
            << loaded_features_->cols() << " features";
        SimpleLogger::Log(msg.str());
        
        // Standardize target for unit variance (required for R-square calculation)
        double target_mean = 0.0;
        for (double val : loaded_targets_) {
            target_mean += val;
        }
        target_mean /= loaded_targets_.size();
        
        double target_var = 0.0;
        for (double val : loaded_targets_) {
            double diff = val - target_mean;
            target_var += diff * diff;
        }
        target_var /= loaded_targets_.size();  // Use population variance to match legacy
        double target_std = std::sqrt(target_var);
        
        if (target_std > 0.0) {
            for (double& val : loaded_targets_) {
                val = (val - target_mean) / target_std;
            }
        }
        
        // BUG FIX #1: Standardize all predictor feature columns (critical for linear-quadratic model)
        SimpleLogger::Log("Standardizing predictor features...");
        for (size_t i = 0; i < loaded_features_->cols(); ++i) {
            loaded_features_->standardize_column(i);
        }
        SimpleLogger::Log("Standardized all predictor feature columns.");
        
        // Run selection on loaded data
        auto results = run_on_data(*loaded_features_, loaded_targets_, feature_column_names, target_column_name);
        
        // Calculate total elapsed time
        auto algorithm_end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(algorithm_end - algorithm_start);
        results.total_elapsed_ms = total_duration.count() / 1000000.0;  // Convert to milliseconds
        
        std::ostringstream timing_msg;
        timing_msg << "Total algorithm runtime: " << std::fixed << std::setprecision(2) 
                   << results.total_elapsed_ms << " ms";
        SimpleLogger::Log(timing_msg.str());
        
        return results;
        
    } catch (const std::exception& e) {
        results.termination_reason = "Error loading data: " + std::string(e.what());
        SimpleLogger::Log("ERROR: " + results.termination_reason);
        
        // Calculate elapsed time even on error
        auto algorithm_end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(algorithm_end - algorithm_start);
        results.total_elapsed_ms = total_duration.count() / 1000000.0;
        
        return results;
    }
}

EnhancedStepwise::StepwiseResults EnhancedStepwise::run_on_data(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<std::string>& feature_names,
    const std::string& target_name) {
    
    StepwiseResults results;
    results.target_name = target_name;
    results.total_cases_loaded = X.rows();
    
    if (X.cols() != feature_names.size()) {
        results.termination_reason = "Feature names size mismatch with data matrix columns";
        return results;
    }
    
    // Run stepwise selection
    auto selection_results = selector_.select_features(X, y);
    
    // Convert results
    results.terminated_early = selection_results.terminated_early;
    results.termination_reason = selection_results.termination_reason;
    results.total_steps = selection_results.total_steps;
    
    if (!selection_results.steps.empty()) {
        // Extract step-by-step results
        results.model_p_values.reserve(selection_results.steps.size());
        results.change_p_values.reserve(selection_results.steps.size());
        results.step_r_squares.reserve(selection_results.steps.size());
        results.step_timing_ms.reserve(selection_results.steps.size());
        
        for (const auto& step : selection_results.steps) {
            results.model_p_values.push_back(step.model_p_value);
            results.change_p_values.push_back(step.change_p_value);
            results.step_r_squares.push_back(step.step_performance);
            results.step_timing_ms.push_back(step.step_elapsed_ms);
        }
        
        // Final results
        const auto& final_features = selection_results.final_feature_set.get_features();
        results.selected_feature_indices = final_features;
        results.final_r_square = selection_results.final_feature_set.get_performance();
        
        // Get feature names
        results.selected_feature_names.reserve(final_features.size());
        for (int feature_idx : final_features) {
            if (feature_idx >= 0 && feature_idx < static_cast<int>(feature_names.size())) {
                results.selected_feature_names.push_back(feature_names[feature_idx]);
            }
        }
        
        // Compute final model coefficients
        if (!final_features.empty()) {
            results.final_coefficients = compute_final_coefficients(X, y, final_features);
        }
    }
    
    return results;
}

std::vector<double> EnhancedStepwise::compute_final_coefficients(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& feature_indices) const {
    
    LinearQuadraticModel model;
    
    // Use the new get_final_coefficients method which fits and returns coefficients
    return model.get_final_coefficients(X, y, feature_indices);
}

void EnhancedStepwise::print_results(const StepwiseResults& results) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Enhanced Stepwise Selection Results" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "Target variable: " << results.target_name << std::endl;
    std::cout << "Total cases loaded: " << results.total_cases_loaded << std::endl;
    std::cout << "Total steps: " << results.total_steps << std::endl;
    std::cout << "Total runtime: " << std::fixed << std::setprecision(2) 
              << results.total_elapsed_ms << " ms" << std::endl;
    std::cout << "Terminated early: " << (results.terminated_early ? "Yes" : "No") << std::endl;
    std::cout << "Termination reason: " << results.termination_reason << std::endl;
    
    if (!results.step_r_squares.empty()) {
        std::cout << "\nFinal R-square: " << std::fixed << std::setprecision(4) 
                  << results.final_r_square << std::endl;
        
        std::cout << "\nSelected features (" << results.selected_feature_names.size() << "):" << std::endl;
        for (size_t i = 0; i < results.selected_feature_names.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << results.selected_feature_names[i] << std::endl;
        }
        
        if (!results.model_p_values.empty() && results.model_p_values.size() == results.step_r_squares.size()) {
            std::cout << "\nStep-by-step results:" << std::endl;
            std::cout << "Step  R-square  Model p-val  Change p-val  Time (ms)" << std::endl;
            std::cout << "----  --------  -----------  ------------  ---------" << std::endl;
            
            for (size_t i = 0; i < results.step_r_squares.size(); ++i) {
                std::cout << std::setw(4) << (i + 1) << "  "
                          << std::fixed << std::setprecision(4) << std::setw(8) << results.step_r_squares[i] << "  "
                          << std::setprecision(3) << std::setw(11) << results.model_p_values[i] << "  "
                          << std::setprecision(3) << std::setw(12) << results.change_p_values[i] << "  "
                          << std::setprecision(1) << std::setw(9) 
                          << (i < results.step_timing_ms.size() ? results.step_timing_ms[i] : 0.0) << std::endl;
            }
        }
    }
    
    std::cout << "\n========================================" << std::endl;
}

void EnhancedStepwise::print_model_coefficients(
    const StepwiseResults& results,
    const std::vector<std::string>& feature_names) {
    
    if (results.final_coefficients.empty() || results.selected_feature_indices.empty()) {
        std::cout << "No model coefficients available" << std::endl;
        return;
    }
    
    int n_pred = static_cast<int>(results.selected_feature_indices.size());
    int n_coef = static_cast<int>(results.final_coefficients.size());
    int expected_coef = LinearQuadraticModel::get_n_terms(n_pred);
    
    if (n_coef != expected_coef) {
        std::cout << "Coefficient count mismatch: expected " << expected_coef 
                  << ", got " << n_coef << std::endl;
        return;
    }
    
    std::cout << "\nLinear-Quadratic Model Coefficients:" << std::endl;
    std::cout << "====================================" << std::endl;
    
    int coef_idx = 0;
    
    // Linear terms
    std::cout << "Linear terms:" << std::endl;
    for (int i = 0; i < n_pred; ++i) {
        int feature_idx = results.selected_feature_indices[i];
        std::string name = (feature_idx < static_cast<int>(feature_names.size())) 
                          ? feature_names[feature_idx] 
                          : ("Feature_" + std::to_string(feature_idx));
        
        std::cout << "  " << std::setw(15) << name 
                  << ": " << std::fixed << std::setprecision(6) 
                  << results.final_coefficients[coef_idx++] << std::endl;
    }
    
    // Square terms
    std::cout << "\nSquare terms:" << std::endl;
    for (int i = 0; i < n_pred; ++i) {
        int feature_idx = results.selected_feature_indices[i];
        std::string name = (feature_idx < static_cast<int>(feature_names.size())) 
                          ? feature_names[feature_idx] 
                          : ("Feature_" + std::to_string(feature_idx));
        
        std::cout << "  " << std::setw(15) << (name + "²") 
                  << ": " << std::fixed << std::setprecision(6) 
                  << results.final_coefficients[coef_idx++] << std::endl;
    }
    
    // Cross-product terms
    if (n_pred > 1) {
        std::cout << "\nCross-product terms:" << std::endl;
        for (int i = 0; i < n_pred; ++i) {
            for (int j = i + 1; j < n_pred; ++j) {
                int feature_idx_i = results.selected_feature_indices[i];
                int feature_idx_j = results.selected_feature_indices[j];
                
                std::string name_i = (feature_idx_i < static_cast<int>(feature_names.size())) 
                                   ? feature_names[feature_idx_i] 
                                   : ("Feature_" + std::to_string(feature_idx_i));
                std::string name_j = (feature_idx_j < static_cast<int>(feature_names.size())) 
                                   ? feature_names[feature_idx_j] 
                                   : ("Feature_" + std::to_string(feature_idx_j));
                
                std::cout << "  " << std::setw(15) << (name_i + "×" + name_j) 
                          << ": " << std::fixed << std::setprecision(6) 
                          << results.final_coefficients[coef_idx++] << std::endl;
            }
        }
    }
    
    // Constant term
    std::cout << "\nConstant term:" << std::endl;
    std::cout << "  " << std::setw(15) << "CONSTANT" 
              << ": " << std::fixed << std::setprecision(6) 
              << results.final_coefficients[coef_idx] << std::endl;
    
    std::cout << "====================================" << std::endl;
}
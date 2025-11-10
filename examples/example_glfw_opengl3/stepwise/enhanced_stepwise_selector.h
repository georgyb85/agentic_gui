#pragma once

#include <vector>
#include <set>
#include <memory>
#include <functional>
#include "data_matrix.h"
#include "cross_validator.h"
#include "linear_quadratic_model.h"
#include "../simple_logger.h"

// Enhanced stepwise feature selection algorithm
class EnhancedStepwiseSelector {
public:
    struct SelectionConfig {
        int n_kept = 5;                    // Number of best models retained per step
        int n_folds = 4;                   // Cross-validation folds
        int min_predictors = 1;            // Minimum required predictors
        int max_predictors = -1;           // Maximum predictors (-1 = no limit)
        int mcpt_replications = 100;       // Monte Carlo permutation test replications
        enum PermutationType { COMPLETE = 1, CYCLIC = 2 };
        PermutationType mcpt_type = COMPLETE;  // Permutation type
        bool early_termination = true;     // Stop if performance degrades
        std::function<bool()> cancel_callback; // Optional cancellation callback
    };
    
    struct SelectionStep {
        std::vector<FeatureSet> best_feature_sets;
        double step_performance;
        double model_p_value;
        double change_p_value;
        std::vector<std::string> selected_feature_names;
        double step_elapsed_ms;  // Time for this step in milliseconds
    };
    
    struct SelectionResults {
        std::vector<SelectionStep> steps;
        FeatureSet final_feature_set;
        bool terminated_early;
        std::string termination_reason;
        size_t total_steps;
        double total_elapsed_ms;  // Total selection time in milliseconds
    };
    
private:
    SelectionConfig config_;
    CrossValidator cv_;
    
    // Feature combination structure (now used locally)
    struct FeatureCombination {
        std::vector<int> features;
        bool operator<(const FeatureCombination& other) const;
        bool operator==(const FeatureCombination& other) const;
    };
    
    // Helper functions
    std::vector<FeatureSet> find_first_variable(
        LinearQuadraticModel& model,
        const DataMatrix& X,
        const std::vector<double>& y,
        int n_candidates,
        std::set<FeatureCombination>& tested_combinations
    ) const;
    
    std::vector<FeatureSet> add_next_variable(
        LinearQuadraticModel& model,
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<FeatureSet>& current_best,
        int n_candidates,
        std::set<FeatureCombination>& tested_combinations
    ) const;
    
    void log_step_results(
        int step_number,
        const SelectionStep& step
    ) const;
    
    // Legacy random number generator to match original implementation
    static double legacy_fast_unif(int* iparam);
    
    
public:
    explicit EnhancedStepwiseSelector(const SelectionConfig& config = SelectionConfig{});
    
    // Main selection algorithm
    SelectionResults select_features(
        const DataMatrix& X,
        const std::vector<double>& y
    ) const;
    
    // Configuration access
    const SelectionConfig& get_config() const { return config_; }
    void set_config(const SelectionConfig& config);
};
#pragma once

#include <vector>
#include <set>
#include <memory>
#include <functional>
#include "data_matrix.h"
#include "cross_validator.h"
#include "model_interface.h"
#include "../simple_logger.h"

namespace stepwise {

// Enhanced stepwise feature selection algorithm with pluggable models
class EnhancedStepwiseSelectorV2 {
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
        std::string model_type;   // Type of model used
    };
    
private:
    SelectionConfig config_;
    CrossValidator cv_;
    std::unique_ptr<IStepwiseModel> model_template_;  // Template model to clone
    
    // Feature combination structure
    struct FeatureCombination {
        std::vector<int> features;
        bool operator<(const FeatureCombination& other) const;
        bool operator==(const FeatureCombination& other) const;
    };
    
    // Helper functions
    std::vector<FeatureSet> find_first_variable(
        const DataMatrix& X,
        const std::vector<double>& y,
        int n_candidates,
        std::set<FeatureCombination>& tested_combinations
    ) const;
    
    std::vector<FeatureSet> add_next_variable(
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
    
    // Monte Carlo permutation test for model significance
    double compute_model_pvalue(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& feature_indices,
        double observed_score
    ) const;
    
    // Monte Carlo permutation test for change significance
    double compute_change_pvalue(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& current_features,
        const std::vector<int>& previous_features,
        double current_score,
        double previous_score
    ) const;
    
    // Legacy random number generator to match original implementation
    static double legacy_fast_unif(int* iparam);
    
public:
    // Constructor with model type
    explicit EnhancedStepwiseSelectorV2(
        std::unique_ptr<IStepwiseModel> model,
        const SelectionConfig& config = SelectionConfig{}
    );
    
    // Main selection algorithm
    SelectionResults select_features(
        const DataMatrix& X,
        const std::vector<double>& y
    ) const;
    
    // Configuration access
    const SelectionConfig& get_config() const { return config_; }
    void set_config(const SelectionConfig& config);
    
    // Model access
    void set_model(std::unique_ptr<IStepwiseModel> model) {
        model_template_ = std::move(model);
    }
};

} // namespace stepwise
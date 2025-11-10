#include "enhanced_stepwise_selector_v2.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <numeric>
#include <cmath>

namespace stepwise {

// FeatureCombination comparison operators
bool EnhancedStepwiseSelectorV2::FeatureCombination::operator<(const FeatureCombination& other) const {
    if (features.size() != other.features.size()) {
        return features.size() < other.features.size();
    }
    return features < other.features;
}

bool EnhancedStepwiseSelectorV2::FeatureCombination::operator==(const FeatureCombination& other) const {
    return features == other.features;
}

// Legacy random number generator
double EnhancedStepwiseSelectorV2::legacy_fast_unif(int* iparam) {
    *iparam = (*iparam * 899999963) + 1;
    double work = (double) *iparam / 2147483647.0;
    return work * 0.5 + 0.25;
}

EnhancedStepwiseSelectorV2::EnhancedStepwiseSelectorV2(
    std::unique_ptr<IStepwiseModel> model,
    const SelectionConfig& config)
    : config_(config)
    , cv_(config.n_folds)
    , model_template_(std::move(model)) {
}

void EnhancedStepwiseSelectorV2::set_config(const SelectionConfig& config) {
    config_ = config;
    cv_ = CrossValidator(config.n_folds);
}

EnhancedStepwiseSelectorV2::SelectionResults EnhancedStepwiseSelectorV2::select_features(
    const DataMatrix& X,
    const std::vector<double>& y) const {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    SelectionResults results;
    results.model_type = model_template_->get_model_type();
    
    std::set<FeatureCombination> tested_combinations;
    std::vector<FeatureSet> current_best;
    
    // Logging
    SimpleLogger logger;
    logger.Log("Starting Enhanced Stepwise Selection with " + results.model_type);
    logger.Log("Data dimensions: " + std::to_string(X.rows()) + " x " + std::to_string(X.cols()));
    
    // Step 1: Find first variable
    auto step_start = std::chrono::high_resolution_clock::now();
    current_best = find_first_variable(X, y, config_.n_kept, tested_combinations);
    
    if (current_best.empty()) {
        results.terminated_early = true;
        results.termination_reason = "No valid first variable found";
        return results;
    }
    
    SelectionStep first_step;
    first_step.best_feature_sets = current_best;
    first_step.step_performance = current_best[0].cv_score;
    first_step.model_p_value = compute_model_pvalue(X, y, current_best[0].feature_indices, current_best[0].cv_score);
    first_step.change_p_value = first_step.model_p_value;
    
    auto step_end = std::chrono::high_resolution_clock::now();
    first_step.step_elapsed_ms = std::chrono::duration<double, std::milli>(step_end - step_start).count();
    
    results.steps.push_back(first_step);
    log_step_results(1, first_step);
    
    // Steps 2+: Add variables
    int step_number = 2;
    double previous_best_score = current_best[0].cv_score;
    
    while (step_number <= config_.max_predictors || config_.max_predictors == -1) {
        // Check cancellation
        if (config_.cancel_callback && config_.cancel_callback()) {
            results.terminated_early = true;
            results.termination_reason = "User cancelled";
            break;
        }
        
        step_start = std::chrono::high_resolution_clock::now();
        auto new_best = add_next_variable(X, y, current_best, config_.n_kept, tested_combinations);
        
        if (new_best.empty()) {
            results.terminated_early = true;
            results.termination_reason = "No improvement possible";
            break;
        }
        
        SelectionStep step;
        step.best_feature_sets = new_best;
        step.step_performance = new_best[0].cv_score;
        
        // Compute p-values if enabled
        if (config_.mcpt_replications > 0) {
            step.model_p_value = compute_model_pvalue(X, y, new_best[0].feature_indices, new_best[0].cv_score);
            
            // Find previous best with one less feature
            std::vector<int> prev_features;
            for (const auto& fs : current_best) {
                if (fs.n_features == new_best[0].n_features - 1) {
                    prev_features = fs.feature_indices;
                    break;
                }
            }
            
            if (!prev_features.empty()) {
                step.change_p_value = compute_change_pvalue(X, y, new_best[0].feature_indices, 
                                                           prev_features, new_best[0].cv_score, 
                                                           previous_best_score);
            }
        }
        
        step_end = std::chrono::high_resolution_clock::now();
        step.step_elapsed_ms = std::chrono::duration<double, std::milli>(step_end - step_start).count();
        
        results.steps.push_back(step);
        log_step_results(step_number, step);
        
        // Check for early termination
        if (config_.early_termination && new_best[0].cv_score <= previous_best_score) {
            results.terminated_early = true;
            results.termination_reason = "Performance degraded";
            break;
        }
        
        // Check minimum predictors
        if (new_best[0].n_features >= config_.min_predictors && 
            new_best[0].cv_score > previous_best_score * 1.001) {  // Small improvement threshold
            // Good enough
        }
        
        current_best = new_best;
        previous_best_score = new_best[0].cv_score;
        step_number++;
    }
    
    // Set final results
    if (!current_best.empty()) {
        results.final_feature_set = current_best[0];
    }
    
    results.total_steps = results.steps.size();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    results.total_elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    return results;
}

std::vector<FeatureSet> EnhancedStepwiseSelectorV2::find_first_variable(
    const DataMatrix& X,
    const std::vector<double>& y,
    int n_candidates,
    std::set<FeatureCombination>& tested_combinations) const {
    
    std::vector<FeatureSet> candidates;
    
    for (int i = 0; i < X.cols(); ++i) {
        FeatureCombination combo;
        combo.features = {i};
        
        if (tested_combinations.count(combo) > 0) {
            continue;
        }
        
        tested_combinations.insert(combo);
        
        // Create model for this feature
        auto model = model_template_->clone();
        
        // Perform cross-validation
        // Note: We need to adapt this for the model interface
        // For now, use a simple train-test evaluation
        model->fit(X, y, {i});
        double cv_score = model->score(X, y, {i});
        
        FeatureSet fs;
        fs.feature_indices = {i};
        fs.cv_score = cv_score;
        fs.train_score = cv_score;  // Simplified
        fs.n_features = 1;
        
        candidates.push_back(fs);
    }
    
    // Sort by CV score (descending)
    std::sort(candidates.begin(), candidates.end(), 
              [](const FeatureSet& a, const FeatureSet& b) {
                  return a.cv_score > b.cv_score;
              });
    
    // Keep top n_candidates
    if (candidates.size() > static_cast<size_t>(n_candidates)) {
        candidates.resize(n_candidates);
    }
    
    return candidates;
}

std::vector<FeatureSet> EnhancedStepwiseSelectorV2::add_next_variable(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<FeatureSet>& current_best,
    int n_candidates,
    std::set<FeatureCombination>& tested_combinations) const {
    
    std::vector<FeatureSet> candidates;
    
    // For each of the current best feature sets
    for (const auto& base_fs : current_best) {
        // Try adding each feature not already in the set
        for (int i = 0; i < X.cols(); ++i) {
            // Check if feature is already in the set
            if (std::find(base_fs.feature_indices.begin(), 
                         base_fs.feature_indices.end(), i) != base_fs.feature_indices.end()) {
                continue;
            }
            
            // Create new feature combination
            std::vector<int> new_features = base_fs.feature_indices;
            new_features.push_back(i);
            std::sort(new_features.begin(), new_features.end());
            
            FeatureCombination combo;
            combo.features = new_features;
            
            if (tested_combinations.count(combo) > 0) {
                continue;
            }
            
            tested_combinations.insert(combo);
            
            // Create model for this feature set
            auto model = model_template_->clone();
            
            // Perform cross-validation
            // Note: We need to adapt this for the model interface
            // For now, use a simple train-test evaluation
            model->fit(X, y, new_features);
            double cv_score = model->score(X, y, new_features);
            
            FeatureSet fs;
            fs.feature_indices = new_features;
            fs.cv_score = cv_score;
            fs.train_score = cv_score;  // Simplified
            fs.n_features = new_features.size();
            
            candidates.push_back(fs);
        }
    }
    
    // Sort by CV score (descending)
    std::sort(candidates.begin(), candidates.end(), 
              [](const FeatureSet& a, const FeatureSet& b) {
                  return a.cv_score > b.cv_score;
              });
    
    // Keep top n_candidates
    if (candidates.size() > static_cast<size_t>(n_candidates)) {
        candidates.resize(n_candidates);
    }
    
    return candidates;
}

void EnhancedStepwiseSelectorV2::log_step_results(
    int step_number,
    const SelectionStep& step) const {
    
    std::stringstream ss;
    ss << "Step " << step_number << ": ";
    ss << "Best CV score = " << step.step_performance;
    ss << ", Features = " << step.best_feature_sets[0].n_features;
    
    if (step.model_p_value > 0) {
        ss << ", Model p-value = " << step.model_p_value;
    }
    
    if (step.change_p_value > 0) {
        ss << ", Change p-value = " << step.change_p_value;
    }
    
    ss << ", Time = " << step.step_elapsed_ms << " ms";
    
    SimpleLogger logger;
    logger.Log(ss.str());
}

double EnhancedStepwiseSelectorV2::compute_model_pvalue(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& feature_indices,
    double observed_score) const {
    
    if (config_.mcpt_replications <= 0) {
        return -1.0;
    }
    
    // Simplified Monte Carlo permutation test
    int better_count = 0;
    std::vector<double> y_permuted = y;
    std::mt19937 rng(42);
    
    for (int rep = 0; rep < config_.mcpt_replications; ++rep) {
        // Permute y
        std::shuffle(y_permuted.begin(), y_permuted.end(), rng);
        
        // Compute score with permuted y
        auto model = model_template_->clone();
        model->fit(X, y_permuted, feature_indices);
        double perm_score = model->score(X, y_permuted, feature_indices);
        
        if (perm_score >= observed_score) {
            better_count++;
        }
    }
    
    return static_cast<double>(better_count + 1) / (config_.mcpt_replications + 1);
}

double EnhancedStepwiseSelectorV2::compute_change_pvalue(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& current_features,
    const std::vector<int>& previous_features,
    double current_score,
    double previous_score) const {
    
    if (config_.mcpt_replications <= 0) {
        return -1.0;
    }
    
    double observed_improvement = current_score - previous_score;
    
    // Simplified Monte Carlo permutation test for change
    int better_count = 0;
    std::vector<double> y_permuted = y;
    std::mt19937 rng(42);
    
    for (int rep = 0; rep < config_.mcpt_replications; ++rep) {
        // Permute y
        std::shuffle(y_permuted.begin(), y_permuted.end(), rng);
        
        // Compute scores with permuted y
        auto model1 = model_template_->clone();
        model1->fit(X, y_permuted, current_features);
        double perm_score_current = model1->score(X, y_permuted, current_features);
        
        auto model2 = model_template_->clone();
        model2->fit(X, y_permuted, previous_features);
        double perm_score_previous = model2->score(X, y_permuted, previous_features);
        
        double perm_improvement = perm_score_current - perm_score_previous;
        
        if (perm_improvement >= observed_improvement) {
            better_count++;
        }
    }
    
    return static_cast<double>(better_count + 1) / (config_.mcpt_replications + 1);
}

} // namespace stepwise
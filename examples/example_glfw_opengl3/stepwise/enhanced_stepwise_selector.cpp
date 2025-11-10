#include "enhanced_stepwise_selector.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <omp.h>
#include "memory_pool.h"

bool EnhancedStepwiseSelector::FeatureCombination::operator<(const FeatureCombination& other) const {
    return features < other.features;
}

bool EnhancedStepwiseSelector::FeatureCombination::operator==(const FeatureCombination& other) const {
    return features == other.features;
}

EnhancedStepwiseSelector::EnhancedStepwiseSelector(const SelectionConfig& config)
    : config_(config), cv_(config.n_folds) {
    
    if (config_.max_predictors == -1) {
        config_.max_predictors = static_cast<int>(1000);  // Reasonable default
    }
}

void EnhancedStepwiseSelector::set_config(const SelectionConfig& config) {
    config_ = config;
    cv_.set_n_folds(config.n_folds);
    
    if (config_.max_predictors == -1) {
        config_.max_predictors = static_cast<int>(1000);
    }
}


EnhancedStepwiseSelector::SelectionResults EnhancedStepwiseSelector::select_features(
    const DataMatrix& X,
    const std::vector<double>& y) const {
    
    SelectionResults results;
    results.terminated_early = false;
    results.total_steps = 0;
    results.total_elapsed_ms = 0.0;
    
    auto algorithm_start = std::chrono::high_resolution_clock::now();
    
    if (X.cols() == 0 || X.rows() == 0) {
        results.termination_reason = "No data provided";
        return results;
    }
    
    int ncases = static_cast<int>(X.rows());
    int ncand = static_cast<int>(X.cols());
    
    // Local state management - no more mutable class members!
    std::vector<double> permuted_y = y;  // Work with a copy for permutations
    std::vector<double> target_work(ncases);  // For cyclic permutations
    
    std::vector<FeatureSet> current_best_sets;
    double prior_step_performance = -1e60;
    LinearQuadraticModel model;  // Local model instance
    
    SimpleLogger::Log("");
    SimpleLogger::Log("Stepwise inclusion of variables...");
    #ifdef _OPENMP
    std::ostringstream parallel_strategy_msg;
    parallel_strategy_msg << "Conditional parallelism: Baseline run uses parallel candidates, MCPT uses parallel replications";
    SimpleLogger::Log(parallel_strategy_msg.str());
    #endif
    SimpleLogger::Log("");
    if (config_.mcpt_replications > 1) {
        SimpleLogger::Log("R-square  MOD pval  CHG pval  Predictors...");
    } else {
        SimpleLogger::Log("R-square  Predictors...");
    }
    
    // Main algorithm loop
    for (int n_so_far = 0; n_so_far < config_.max_predictors; ++n_so_far) {
        // Check for cancellation
        if (config_.cancel_callback && config_.cancel_callback()) {
            results.termination_reason = "Analysis cancelled by user";
            results.terminated_early = true;
            goto finish_selection;
        }
        
        results.total_steps = n_so_far + 1;
        
        // Start timing for this step
        auto step_start = std::chrono::high_resolution_clock::now();
        
        // MCPT variables (local to this step)
        double original_crit = 0.0, original_change = 0.0;
        int mcpt_mod_count = 0, mcpt_change_count = 0;
        std::vector<FeatureSet> step_best_sets;
        double step_performance = 0.0;
        
        // --- Unpermuted baseline run (irep = 0) done serially first ---
        {
            std::set<FeatureCombination> tested_combinations;
            permuted_y = y;  // Use original data for baseline run
            
            // Find the next best feature sets for baseline
            std::vector<FeatureSet> next_best_sets;
            if (n_so_far == 0) {
                next_best_sets = find_first_variable(model, X, permuted_y, ncand, tested_combinations);
            } else {
                next_best_sets = add_next_variable(model, X, permuted_y, current_best_sets, ncand, tested_combinations);
            }
            
            if (next_best_sets.empty()) {
                // Only terminate early if we've reached minimum predictors
                if (n_so_far >= config_.min_predictors) {
                    results.termination_reason = "No further improvement found.";
                    results.terminated_early = true;
                    goto finish_selection;
                } else {
                    // Force continuation if we haven't met minimum predictors
                    SimpleLogger::Log("Warning: No improvement found but min_predictors not reached");
                    results.termination_reason = "No variables found but minimum not reached";
                    results.terminated_early = true;
                    goto finish_selection;
                }
            }
            
            double current_performance = next_best_sets[0].get_performance();
            double normalized_prior = (prior_step_performance < 0.0) ? 0.0 : prior_step_performance;
            double new_crit = (current_performance < 0.0) ? 0.0 : current_performance;
            
            // Store baseline results
            original_crit = new_crit;
            original_change = new_crit - normalized_prior;
            mcpt_mod_count = mcpt_change_count = 1;
            
            step_best_sets = next_best_sets;
            step_performance = current_performance;
            
            // CRITICAL FIX: Early termination check on unpermuted run
            if (config_.early_termination && 
                current_performance <= prior_step_performance && 
                n_so_far >= config_.min_predictors) {
                
                results.termination_reason = "STEPWISE terminated early because adding a new variable caused performance degradation";
                results.terminated_early = true;
                goto finish_selection;
            }
        }
        
        // --- Parallel MCPT Loop for permuted replications (irep = 1 to config_.mcpt_replications-1) ---
        if (config_.mcpt_replications > 1) {
            #ifdef _OPENMP
            int num_threads = omp_get_max_threads();
            std::ostringstream parallel_msg;
            parallel_msg << "Running " << (config_.mcpt_replications - 1) << " permutation replications in parallel using " 
                         << num_threads << " threads";
            SimpleLogger::Log(parallel_msg.str());
            #endif
            
            #pragma omp parallel for reduction(+:mcpt_mod_count, mcpt_change_count)
            for (int irep = 1; irep < config_.mcpt_replications; ++irep) {
                // Check for cancellation in parallel threads
                if (config_.cancel_callback && config_.cancel_callback()) {
                    continue;  // Skip this iteration if cancelled
                }
                
                // Thread-local variables for thread safety
                LinearQuadraticModel thread_local_model;
                std::set<FeatureCombination> thread_local_tested_combinations;
                std::vector<double> thread_permuted_y = y;  // Each thread gets its own copy
                std::vector<double> thread_target_work(ncases);
                
                // CRITICAL FIX: Use exact legacy random number generation and shuffling
                int irand = 17 * irep + 11;  // Always use the same shuffle in each rep
                
                // Warm up the generator (legacy does this twice)
                legacy_fast_unif(&irand);
                legacy_fast_unif(&irand);
                
                if (config_.mcpt_type == SelectionConfig::COMPLETE) {
                    // Complete permutation using exact legacy Fisher-Yates shuffle
                    int i = ncases;  // Number remaining to be shuffled
                    while (i > 1) {  // While at least 2 left to shuffle
                        int j = static_cast<int>(legacy_fast_unif(&irand) * i);
                        if (j >= i) j = i - 1;
                        
                        double dtemp = thread_permuted_y[--i];
                        thread_permuted_y[i] = thread_permuted_y[j];
                        thread_permuted_y[j] = dtemp;
                    }
                } else {
                    // Cyclic permutation using legacy logic
                    int j = static_cast<int>(legacy_fast_unif(&irand) * ncases);
                    if (j >= ncases) j = ncases - 1;
                    
                    for (int i = 0; i < ncases; ++i) {
                        thread_target_work[i] = thread_permuted_y[(i + j) % ncases];
                    }
                    thread_permuted_y = thread_target_work;
                }
                
                // Find best variables for this permutation
                std::vector<FeatureSet> next_best_sets;
                if (n_so_far == 0) {
                    next_best_sets = find_first_variable(thread_local_model, X, thread_permuted_y, ncand, thread_local_tested_combinations);
                } else {
                    next_best_sets = add_next_variable(thread_local_model, X, thread_permuted_y, current_best_sets, ncand, thread_local_tested_combinations);
                }
                
                if (!next_best_sets.empty()) {
                    double current_performance = next_best_sets[0].get_performance();
                    double normalized_prior = (prior_step_performance < 0.0) ? 0.0 : prior_step_performance;
                    double new_crit = (current_performance < 0.0) ? 0.0 : current_performance;
                    
                    // These updates are automatically handled by the reduction clause
                    if (new_crit >= original_crit) {
                        mcpt_mod_count++;
                    }
                    if (new_crit - normalized_prior >= original_change) {
                        mcpt_change_count++;
                    }
                }
            }
        }
        // --- End Parallel MCPT Loop ---
        
        // Early termination now handled inside MCPT loop to match legacy behavior
        
        // Update global state for next iteration
        current_best_sets = step_best_sets;
        prior_step_performance = step_performance;
        
        // 4. Log results for this step  
        if (n_so_far == 0) mcpt_change_count = mcpt_mod_count;
        
        
        std::string msg;
        if (config_.mcpt_replications > 1) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(4) << original_crit << "    "
                << std::setprecision(3) << (double)mcpt_mod_count / config_.mcpt_replications << "     "
                << std::setprecision(3) << (double)mcpt_change_count / config_.mcpt_replications << "  ";
            msg = oss.str();
        } else {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(4) << original_crit << " ";
            msg = oss.str();
        }
        
        // Add feature names
        if (!current_best_sets.empty()) {
            const auto& features = current_best_sets[0].get_features();
            for (int feature_idx : features) {
                if (feature_idx < static_cast<int>(X.get_column_names().size())) {
                    msg += " " + X.get_column_names()[feature_idx];
                } else {
                    msg += " VAR" + std::to_string(feature_idx);
                }
            }
        }
        SimpleLogger::Log(msg);
        
        // Calculate step timing
        auto step_end = std::chrono::high_resolution_clock::now();
        auto step_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(step_end - step_start);
        double step_elapsed_ms = step_duration.count() / 1000000.0;  // Convert to milliseconds
        
        // Store step results
        SelectionStep step;
        step.best_feature_sets = current_best_sets;
        step.step_performance = step_performance;  // Use actual performance, not normalized value
        step.model_p_value = (double)mcpt_mod_count / config_.mcpt_replications;
        step.change_p_value = (double)mcpt_change_count / config_.mcpt_replications;
        step.step_elapsed_ms = step_elapsed_ms;
        results.steps.push_back(step);
        
        // Log step timing
        std::ostringstream timing_msg;
        timing_msg << "Step " << (n_so_far + 1) << " completed in " 
                   << std::fixed << std::setprecision(2) << step_elapsed_ms << " ms";
        SimpleLogger::Log(timing_msg.str());
    }
    
finish_selection:
    
    // Finalize results
    if (!current_best_sets.empty()) {
        results.final_feature_set = current_best_sets[0];
    } else if (!results.steps.empty()) {
        // Early termination case - use the last successful step
        results.final_feature_set = results.steps.back().best_feature_sets[0];
    }
    
    // Calculate total algorithm timing
    auto algorithm_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(algorithm_end - algorithm_start);
    results.total_elapsed_ms = total_duration.count() / 1000000.0;  // Convert to milliseconds
    
    SimpleLogger::Log("");
    if (results.terminated_early) {
        SimpleLogger::Log("STEPWISE terminated early: " + results.termination_reason);
    } else {
        SimpleLogger::Log("STEPWISE successfully completed");
    }
    
    std::ostringstream total_timing_msg;
    total_timing_msg << "Total selection time: " << std::fixed << std::setprecision(2) 
                     << results.total_elapsed_ms << " ms";
    SimpleLogger::Log(total_timing_msg.str());
    SimpleLogger::Log("");
    
    return results;
}

std::vector<FeatureSet> EnhancedStepwiseSelector::find_first_variable(
    LinearQuadraticModel& model,
    const DataMatrix& X,
    const std::vector<double>& y,
    int n_candidates,
    std::set<FeatureCombination>& tested_combinations) const {

    // STEP 1: Generate the list of unique tasks to be done. (This is fast)
    std::vector<std::vector<int>> tasks;
    for (int var_idx = 0; var_idx < n_candidates; ++var_idx) {
        std::vector<int> single_feature = {var_idx};
        FeatureCombination combo;
        combo.features = single_feature;
        
        // If we haven't tested this combination before, add it to our task list.
        if (tested_combinations.find(combo) == tested_combinations.end()) {
            tasks.push_back(single_feature);
        }
    }

    // STEP 2: Execute the tasks in parallel.
    std::vector<FeatureSet> candidate_sets;
    candidate_sets.reserve(tasks.size());
    omp_lock_t writelock;
    omp_init_lock(&writelock);

    int num_tasks = static_cast<int>(tasks.size());
    #pragma omp parallel for if(!omp_in_parallel())
    for (int i = 0; i < num_tasks; ++i) {
        const auto& feature_set_indices = tasks[i];
        LinearQuadraticModel thread_local_model;

        double performance = cv_.compute_criterion(thread_local_model, X, y, feature_set_indices);

        if (performance >= 0) {
            FeatureSet fs;
            fs.set_features(feature_set_indices);
            fs.set_performance(performance);

            // Lock only to append the result. This is a very short critical section.
            omp_set_lock(&writelock);
            candidate_sets.push_back(fs);
            omp_unset_lock(&writelock);
        }
    }
    
    omp_destroy_lock(&writelock);

    // Update the master list of tested combinations serially after the parallel run.
    for(const auto& task : tasks) {
        FeatureCombination combo;
        combo.features = task;
        tested_combinations.insert(combo);
    }
    
    // Sort and resize as before.
    std::sort(candidate_sets.begin(), candidate_sets.end());
    if (candidate_sets.size() > static_cast<size_t>(config_.n_kept)) {
        candidate_sets.resize(config_.n_kept);
    }
    
    return candidate_sets;
}

std::vector<FeatureSet> EnhancedStepwiseSelector::add_next_variable(
    LinearQuadraticModel& model,
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<FeatureSet>& current_best,
    int n_candidates,
    std::set<FeatureCombination>& tested_combinations) const {

    // STEP 1: Generate the list of unique tasks to be done. (This is fast)
    std::vector<std::vector<int>> tasks;
    std::set<FeatureCombination> new_combos_this_step; // Use a local set to find unique new tasks

    for (const auto& base_set : current_best) {
        for (int var_idx = 0; var_idx < n_candidates; ++var_idx) {
            if (std::find(base_set.get_features().begin(), base_set.get_features().end(), var_idx) != base_set.get_features().end()) {
                continue;
            }
            
            std::vector<int> new_features = base_set.get_features();
            new_features.push_back(var_idx);
            std::sort(new_features.begin(), new_features.end());

            FeatureCombination combo;
            combo.features = new_features;

            // If it's not in the global tested list AND not already in our local task list...
            if (tested_combinations.find(combo) == tested_combinations.end() && 
                new_combos_this_step.find(combo) == new_combos_this_step.end()) {
                
                tasks.push_back(new_features);
                new_combos_this_step.insert(combo);
            }
        }
    }

    // STEP 2: Execute the tasks in parallel.
    std::vector<FeatureSet> new_candidate_sets;
    new_candidate_sets.reserve(tasks.size());
    omp_lock_t writelock;
    omp_init_lock(&writelock);

    int num_tasks = static_cast<int>(tasks.size());
    #pragma omp parallel for if(!omp_in_parallel())
    for (int i = 0; i < num_tasks; ++i) {
        const auto& feature_set_indices = tasks[i];
        LinearQuadraticModel thread_local_model;

        double performance = cv_.compute_criterion(thread_local_model, X, y, feature_set_indices);
        
        if (performance >= 0) {
            FeatureSet fs;
            fs.set_features(feature_set_indices);
            fs.set_performance(performance);

            omp_set_lock(&writelock);
            new_candidate_sets.push_back(fs);
            omp_unset_lock(&writelock);
        }
    }
    omp_destroy_lock(&writelock);

    // Update the master list of tested combinations serially.
    tested_combinations.insert(new_combos_this_step.begin(), new_combos_this_step.end());

    // Sort and resize as before.
    std::sort(new_candidate_sets.begin(), new_candidate_sets.end());
    if (new_candidate_sets.size() > static_cast<size_t>(config_.n_kept)) {
        new_candidate_sets.resize(config_.n_kept);
    }
    
    return new_candidate_sets;
}


void EnhancedStepwiseSelector::log_step_results(
    int step_number,
    const SelectionStep& step) const {
    
    SimpleLogger::Log("Step " + std::to_string(step_number) + 
                     ": Performance = " + std::to_string(step.step_performance) +
                     ", Model p-val = " + std::to_string(step.model_p_value) +
                     ", Change p-val = " + std::to_string(step.change_p_value));
}

// CRITICAL FIX: Legacy random number generator to exactly match STEPWISE.CPP
// This implements the exact same algorithm as fast_unif() in RAND32.CPP
double EnhancedStepwiseSelector::legacy_fast_unif(int* iparam) {
    constexpr long IA = 16807;
    constexpr long IM = 2147483647;
    constexpr long IQ = 127773;
    constexpr long IR = 2836;
    
    long k = *iparam / IQ;
    *iparam = IA * (*iparam - k * IQ) - IR * k;
    if (*iparam < 0) {
        *iparam += IM;
    }
    return *iparam / static_cast<double>(IM);
}
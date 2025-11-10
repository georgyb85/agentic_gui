#include "monte_carlo_permutation_test.h"
#include "linear_quadratic_model.h"
#include <algorithm>
#include <stdexcept>

// Legacy random number generator implementation (same as RAND32.CPP)
double MonteCarloPermutationTest::fast_unif(int* iparam) {
    constexpr long IA = 16807;
    constexpr long IM = 2147483647;
    constexpr double AM = (1.0 / IM);
    constexpr long IQ = 127773;
    constexpr long IR = 2836;
    
    long k = (*iparam) / IQ;
    *iparam = IA * (*iparam - k * IQ) - IR * k;
    if (*iparam < 0) {
        *iparam += IM;
    }
    
    return AM * (*iparam);
}

MonteCarloPermutationTest::MCPTResults MonteCarloPermutationTest::compute_significance(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& current_features,
    double observed_performance,
    double prior_performance) const {
    
    if (n_replications_ < 1) {
        throw std::invalid_argument("Number of replications must be at least 1");
    }
    
    MCPTResults results;
    results.model_count = 1;    // Count original (unpermuted) result
    results.change_count = 1;   // Count original (unpermuted) result
    results.total_replications = n_replications_;
    
    // Clamp negative performance values to 0 for conservative testing
    double clamped_observed = (observed_performance < 0.0) ? 0.0 : observed_performance;
    double clamped_prior = (prior_performance < 0.0) ? 0.0 : prior_performance;
    double observed_change = clamped_observed - clamped_prior;
    
    // Perform permutation replications
    for (int irep = 1; irep < n_replications_; ++irep) {  // Start from 1 (skip unpermuted)
        // Create copy of targets for permutation
        std::vector<double> permuted_targets = y;
        
        // Permute targets using deterministic seed based on replication number
        if (permutation_type_ == COMPLETE) {
            permute_targets_complete(permuted_targets, irep);
        } else {
            permute_targets_cyclic(permuted_targets, irep);
        }
        
        // Compute performance on permuted data
        LinearQuadraticModel model;  // Create model instance for this iteration
        double permuted_performance = cv_.compute_criterion(model, X, permuted_targets, current_features);
        
        // Clamp negative performance for conservative test
        if (permuted_performance < 0.0) {
            permuted_performance = 0.0;
        }
        
        // Update model p-value counter
        if (permuted_performance >= clamped_observed) {
            results.model_count++;
        }
        
        // Update change p-value counter
        double permuted_change = permuted_performance - clamped_prior;
        if (permuted_change >= observed_change) {
            results.change_count++;
        }
    }
    
    // Compute p-values
    results.model_p_value = static_cast<double>(results.model_count) / n_replications_;
    results.change_p_value = static_cast<double>(results.change_count) / n_replications_;
    
    return results;
}

void MonteCarloPermutationTest::permute_targets_complete(
    std::vector<double>& targets, int seed) const {
    
    // Use same seeding strategy as original code
    int irand = 17 * seed + 11;
    fast_unif(&irand);  // Warm up generator
    fast_unif(&irand);  // Warm up generator again
    
    // Fisher-Yates shuffle using fast_unif
    int n = static_cast<int>(targets.size());
    for (int i = n - 1; i > 0; --i) {
        int j = static_cast<int>(fast_unif(&irand) * (i + 1));
        if (j > i) j = i;  // Clamp to valid range
        
        if (i != j) {
            std::swap(targets[i], targets[j]);
        }
    }
}

void MonteCarloPermutationTest::permute_targets_cyclic(
    std::vector<double>& targets, int seed) const {
    
    // Use same seeding strategy as original code
    int irand = 17 * seed + 11;
    fast_unif(&irand);  // Warm up generator
    fast_unif(&irand);  // Warm up generator again
    
    // Cyclic rotation by random offset
    int n = static_cast<int>(targets.size());
    if (n <= 1) return;
    
    int offset = static_cast<int>(fast_unif(&irand) * n);
    if (offset >= n) offset = n - 1;
    
    if (offset == 0) return;  // No rotation needed
    
    // Create temporary copy for rotation
    std::vector<double> temp_targets(targets.size());
    for (int i = 0; i < n; ++i) {
        temp_targets[i] = targets[(i + offset) % n];
    }
    
    // Copy back
    targets = temp_targets;
}
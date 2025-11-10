#pragma once

#include <vector>
#include "data_matrix.h"
#include "cross_validator.h"

// Monte Carlo Permutation Test for statistical significance
class MonteCarloPermutationTest {
public:
    enum PermutationType {
        COMPLETE = 1,  // Complete permutation (best for independent targets)
        CYCLIC = 2     // Cyclic permutation (for serial correlation)
    };
    
    struct MCPTResults {
        double model_p_value;     // P-value for overall model performance
        double change_p_value;    // P-value for performance improvement
        int model_count;          // Count of permutations >= original performance
        int change_count;         // Count of improvements >= original improvement
        int total_replications;   // Total number of replications performed
    };
    
private:
    int n_replications_;
    PermutationType permutation_type_;
    CrossValidator cv_;
    
    // Random number generator (same algorithm as legacy fast_unif)
    static double fast_unif(int* iparam);
    
    // Permute target vector using fast_unif generator
    void permute_targets_complete(std::vector<double>& targets, int seed) const;
    void permute_targets_cyclic(std::vector<double>& targets, int seed) const;
    
public:
    MonteCarloPermutationTest(
        int n_replications = 100,
        PermutationType perm_type = COMPLETE,
        int n_folds = 4
    ) : n_replications_(n_replications), permutation_type_(perm_type), cv_(n_folds) {}
    
    // Compute statistical significance of feature set performance
    MCPTResults compute_significance(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& current_features,
        double observed_performance,
        double prior_performance = -1e60  // For change p-value computation
    ) const;
    
    // Getters/setters
    int get_n_replications() const { return n_replications_; }
    void set_n_replications(int n_reps) { n_replications_ = n_reps; }
    
    PermutationType get_permutation_type() const { return permutation_type_; }
    void set_permutation_type(PermutationType perm_type) { permutation_type_ = perm_type; }
    
    int get_n_folds() const { return cv_.get_n_folds(); }
    void set_n_folds(int n_folds) { cv_.set_n_folds(n_folds); }
};
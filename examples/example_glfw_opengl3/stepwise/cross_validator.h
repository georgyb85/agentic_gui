#pragma once

#include <vector>
#include <utility>
#include "data_matrix.h"
#include "linear_quadratic_model.h"

// Cross-validation system for evaluating feature sets
class CrossValidator {
private:
    int n_folds_;
    
    // Generate fold boundaries for cross-validation
    void create_folds(int n_cases, std::vector<std::pair<int, int>>& folds) const;
    
public:
    explicit CrossValidator(int n_folds = 4) : n_folds_(n_folds) {}
    
    // Compute cross-validation criterion (R-square) for a feature set
    // Returns R-square value (1.0 - normalized_error)
    // The model is passed in as a dependency
    double compute_criterion(
        LinearQuadraticModel& model,
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& feature_indices
    ) const;
    
    // Get number of folds
    int get_n_folds() const { return n_folds_; }
    
    // Set number of folds
    void set_n_folds(int n_folds) { n_folds_ = n_folds; }
};
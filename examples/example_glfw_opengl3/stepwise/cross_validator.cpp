#include "cross_validator.h"
#include <stdexcept>
#include <numeric>
#include <cmath>

double CrossValidator::compute_criterion(
    LinearQuadraticModel& model,
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& feature_indices) const {
    
    if (feature_indices.empty()) {
        return -1.0;  // Invalid feature set
    }
    
    int n_cases = static_cast<int>(y.size());
    if (n_cases <= n_folds_) {
        throw std::invalid_argument("Number of cases must be greater than number of folds");
    }
    
    // Create fold boundaries
    std::vector<std::pair<int, int>> folds;
    create_folds(n_cases, folds);
    
    double total_error = 0.0;
    bool any_fold_failed = false;
    
    // Perform cross-validation
    for (const auto& fold : folds) {
        int test_start = fold.first;
        int test_stop = fold.second;
        
        // Train model on all cases except the test fold
        bool fit_success = model.fit(X, y, feature_indices, test_start, test_stop);
        if (!fit_success) {
            any_fold_failed = true;
            break;
        }
        
        // Evaluate on test fold
        double fold_error = model.evaluate(X, y, feature_indices, test_start, test_stop);
        total_error += fold_error;
    }
    
    if (any_fold_failed) {
        return -1.0;  // Model fitting failed
    }
    
    // Calculate R-square criterion
    // Following the original algorithm: R-square = 1.0 - (error / n_cases)
    // This assumes targets are standardized to unit variance
    double r_square = 1.0 - (total_error / n_cases);
    
    return r_square;
}

void CrossValidator::create_folds(int n_cases, std::vector<std::pair<int, int>>& folds) const {
    folds.clear();
    folds.reserve(n_folds_);
    
    int n_remaining = n_cases;
    int test_start = 0;
    
    for (int ifold = 0; ifold < n_folds_; ++ifold) {
        int fold_size = n_remaining / (n_folds_ - ifold);
        int test_stop = test_start + fold_size;
        
        folds.emplace_back(test_start, test_stop);
        
        n_remaining -= fold_size;
        test_start = test_stop;
    }
}
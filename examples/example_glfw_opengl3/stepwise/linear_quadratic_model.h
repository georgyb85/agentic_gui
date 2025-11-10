#pragma once

#include <vector>
#include <Eigen/Dense> // Use Eigen's core dense matrix library
#include "data_matrix.h"
#include "memory_pool.h"

// The LinearQuadraticModel is now a wrapper around Eigen's fast linear algebra.
class LinearQuadraticModel {
private:
    // Coefficients are now stored in a more efficient, math-oriented Eigen vector.
    Eigen::VectorXd coefficients_;
    
    // Cached full design matrix for cross-validation optimization
    mutable Eigen::MatrixXd cached_full_matrix_;
    mutable std::vector<int> cached_feature_indices_;
    mutable bool cache_valid_ = false;

    // Private helper to build the full design matrix (A in Ax=b).
    // This is the core of the new, efficient approach.
    void build_design_matrix(
        Eigen::MatrixXd& A,
        const DataMatrix& X,
        const std::vector<int>& feature_indices,
        const std::vector<size_t>& row_indices
    ) const;
    
    // Optimized vectorized version for building full matrix
    void build_design_matrix_vectorized(
        Eigen::MatrixXd& A,
        const DataMatrix& X,
        const std::vector<int>& feature_indices
    ) const;
    
    // Adaptive solver selection based on problem characteristics
    enum SolverType { QR_SOLVER, LLT_SOLVER, SVD_SOLVER };
    SolverType select_best_solver(const Eigen::MatrixXd& A) const;

public:
    LinearQuadraticModel() = default;

    // Fits the model to the training data (all rows EXCEPT the excluded fold)
    // Returns true on success.
    bool fit(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& feature_indices,
        int exclude_start,
        int exclude_stop);

    // Evaluates the model on the test data (rows WITHIN the specified fold)
    // Returns the sum of squared errors for the fold.
    double evaluate(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& feature_indices,
        int test_start,
        int test_stop) const;
    
    // Fits a final model on ALL data and returns the coefficients.
    std::vector<double> get_final_coefficients(
        const DataMatrix& X,
        const std::vector<double>& y,
        const std::vector<int>& feature_indices
    );
    
    // Get model coefficients (compatibility method)
    const std::vector<double>& get_coefficients() const { 
        static std::vector<double> temp;
        temp.resize(coefficients_.size());
        for (int i = 0; i < coefficients_.size(); ++i) {
            temp[i] = coefficients_(i);
        }
        return temp;
    }
    
    // Get number of terms in the linear-quadratic expansion (static helper)
    static int get_n_terms(int n_predictors) {
        return n_predictors + n_predictors * (n_predictors + 1) / 2 + 1;  // linear + quadratic + constant
    }
};
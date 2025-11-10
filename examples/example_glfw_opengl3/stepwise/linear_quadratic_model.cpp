#include "linear_quadratic_model.h"
#include <numeric>
#include <stdexcept>
#include <Eigen/QR>
#include <Eigen/Cholesky>

// Vectorized version for building the full design matrix at once
void LinearQuadraticModel::build_design_matrix_vectorized(
    Eigen::MatrixXd& A,
    const DataMatrix& X,
    const std::vector<int>& feature_indices) const
{
    const size_t n_rows = X.rows();
    const int npred = static_cast<int>(feature_indices.size());
    
    // Calculate the total number of terms in the model:
    // N (linear) + N (square) + N*(N-1)/2 (interaction) + 1 (for intercept)
    const int n_linear_terms = npred;
    const int n_square_terms = npred;
    const int n_interaction_terms = npred > 1 ? npred * (npred - 1) / 2 : 0;
    const int n_total_terms = n_linear_terms + n_square_terms + n_interaction_terms + 1;

    if (n_rows == 0 || n_total_terms == 0) {
        A.resize(0, 0);
        return;
    }
    
    // === MEMORY POOL OPTIMIZATION (DISABLED) ===
    // Memory pool disabled due to static destruction issues
    // Just use regular allocation
    A.resize(n_rows, n_total_terms);
    int current_col = 0;

    // === VECTORIZED OPERATIONS ===
    
    // 1. Linear terms - Copy columns directly from DataMatrix
    for (int p = 0; p < npred; ++p) {
        int col_idx = feature_indices[p];
        // Vectorized column copy
        for (size_t i = 0; i < n_rows; ++i) {
            A(i, current_col) = X.get(i, col_idx);
        }
        current_col++;
    }
    
    // 2. Square terms - Use Eigen's array operations for element-wise squaring
    int linear_start = 0;
    for (int p = 0; p < npred; ++p) {
        // Vectorized squaring: copy the linear column and square it
        A.col(current_col) = A.col(linear_start + p).array().square();
        current_col++;
    }
    
    // 3. Interaction terms - Use Eigen's element-wise multiplication
    for (int p1 = 0; p1 < npred; ++p1) {
        for (int p2 = p1 + 1; p2 < npred; ++p2) {
            // Vectorized element-wise multiplication
            A.col(current_col) = A.col(linear_start + p1).array() * A.col(linear_start + p2).array();
            current_col++;
        }
    }
    
    // 4. Intercept term - Vectorized constant assignment
    A.col(current_col).setOnes();
}

// Original method kept for compatibility with row-subset operations
void LinearQuadraticModel::build_design_matrix(
    Eigen::MatrixXd& A,
    const DataMatrix& X,
    const std::vector<int>& feature_indices,
    const std::vector<size_t>& row_indices) const
{
    const size_t n_rows = row_indices.size();
    const int npred = static_cast<int>(feature_indices.size());
    
    const int n_linear_terms = npred;
    const int n_square_terms = npred;
    const int n_interaction_terms = npred > 1 ? npred * (npred - 1) / 2 : 0;
    const int n_total_terms = n_linear_terms + n_square_terms + n_interaction_terms + 1;

    if (n_rows == 0 || n_total_terms == 0) {
        A.resize(0, 0);
        return;
    }
    
    A.resize(n_rows, n_total_terms);

    for (size_t i = 0; i < n_rows; ++i) {
        const size_t case_idx = row_indices[i];
        int current_col = 0;
        
        // 1. Linear terms
        for (int p = 0; p < npred; ++p) {
            A(i, current_col++) = X.get(case_idx, feature_indices[p]);
        }
        
        // 2. Square terms
        for (int p = 0; p < npred; ++p) {
            double val = X.get(case_idx, feature_indices[p]);
            A(i, current_col++) = val * val;
        }

        // 3. Interaction terms
        for (int p1 = 0; p1 < npred; ++p1) {
            for (int p2 = p1 + 1; p2 < npred; ++p2) {
                A(i, current_col++) = X.get(case_idx, feature_indices[p1]) * X.get(case_idx, feature_indices[p2]);
            }
        }
        
        // 4. Intercept term
        A(i, current_col++) = 1.0;
    }
}

// Adaptive solver selection based on matrix characteristics
LinearQuadraticModel::SolverType LinearQuadraticModel::select_best_solver(const Eigen::MatrixXd& A) const {
    const int n_rows = static_cast<int>(A.rows());
    const int n_cols = static_cast<int>(A.cols());
    
    // For small problems with good conditioning, use QR
    if (n_cols <= 50 && n_rows >= n_cols * 2) {
        // Check if we can use normal equations (A'A is well-conditioned)
        // This is fastest but requires good conditioning
        Eigen::MatrixXd AtA = A.transpose() * A;
        Eigen::LLT<Eigen::MatrixXd> llt(AtA);
        if (llt.info() == Eigen::Success) {
            // Estimate condition number (rough check)
            double diag_min = AtA.diagonal().minCoeff();
            double diag_max = AtA.diagonal().maxCoeff();
            if (diag_min > 0 && diag_max / diag_min < 1e6) {
                return LLT_SOLVER;  // Well-conditioned, use normal equations
            }
        }
        return QR_SOLVER;  // Use QR for moderate conditioning
    }
    
    // For larger or potentially ill-conditioned problems, use SVD
    return SVD_SOLVER;
}

bool LinearQuadraticModel::fit(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& feature_indices,
    int exclude_start,
    int exclude_stop)
{
    const size_t n_total_cases = y.size();
    const size_t n_train_cases = n_total_cases - (exclude_stop - exclude_start);

    if (n_train_cases == 0) return false;

    // === OPTIMIZATION 1: Cross-validation matrix caching ===
    // Check if we can use cached full matrix
    bool use_cache = (exclude_stop > exclude_start) && // This is a CV fold
                     (cache_valid_) && 
                     (cached_feature_indices_ == feature_indices);
    
    Eigen::MatrixXd A;
    Eigen::VectorXd b(n_train_cases);
    
    if (use_cache) {
        // Use cached full matrix with block operations
        int current_row = 0;
        A.resize(n_train_cases, cached_full_matrix_.cols());
        
        // Extract training rows using block operations
        if (exclude_start > 0) {
            int block_size = exclude_start;
            A.block(current_row, 0, block_size, A.cols()) = 
                cached_full_matrix_.block(0, 0, block_size, A.cols());
            for (int i = 0; i < block_size; ++i) {
                b(current_row++) = y[i];
            }
        }
        
        if (exclude_stop < n_total_cases) {
            int block_start = exclude_stop;
            int block_size = static_cast<int>(n_total_cases) - exclude_stop;
            A.block(current_row, 0, block_size, A.cols()) = 
                cached_full_matrix_.block(block_start, 0, block_size, A.cols());
            for (size_t i = exclude_stop; i < n_total_cases; ++i) {
                b(current_row++) = y[i];
            }
        }
    } else {
        // Build matrix from scratch
        if (exclude_stop > exclude_start) {
            // CV fold - build subset
            std::vector<size_t> train_indices;
            train_indices.reserve(n_train_cases);
            int current_b_row = 0;
            
            for (size_t i = 0; i < n_total_cases; ++i) {
                if (i < static_cast<size_t>(exclude_start) || i >= static_cast<size_t>(exclude_stop)) {
                    train_indices.push_back(i);
                    b(current_b_row++) = y[i];
                }
            }
            
            build_design_matrix(A, X, feature_indices, train_indices);
        } else {
            // Full data - use vectorized version and cache it
            build_design_matrix_vectorized(A, X, feature_indices);
            
            // Cache for future CV folds
            cached_full_matrix_ = A;
            cached_feature_indices_ = feature_indices;
            cache_valid_ = true;
            
            // Fill b vector
            for (size_t i = 0; i < n_total_cases; ++i) {
                b(i) = y[i];
            }
        }
    }
    
    if (A.rows() == 0 || A.cols() == 0) return false;

    // === OPTIMIZATION 2: Adaptive solver selection ===
    SolverType solver = select_best_solver(A);
    
    switch (solver) {
        case LLT_SOLVER: {
            // Fastest: Normal equations with Cholesky decomposition
            Eigen::MatrixXd AtA = A.transpose() * A;
            Eigen::VectorXd Atb = A.transpose() * b;
            coefficients_ = AtA.llt().solve(Atb);
            break;
        }
        case QR_SOLVER: {
            // Fast and stable: QR decomposition
            coefficients_ = A.householderQr().solve(b);
            break;
        }
        case SVD_SOLVER: {
            // Most stable but slower: SVD
            coefficients_ = A.bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(b);
            break;
        }
    }

    return true;
}

double LinearQuadraticModel::evaluate(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& feature_indices,
    int test_start,
    int test_stop) const
{
    const size_t n_test_cases = test_stop - test_start;
    if (n_test_cases == 0) return 0.0;

    // === OPTIMIZATION: Use cached matrix if available ===
    Eigen::MatrixXd A;
    
    if (cache_valid_ && cached_feature_indices_ == feature_indices && 
        test_start >= 0 && test_stop <= static_cast<int>(y.size())) {
        // Extract test block from cached matrix
        A = cached_full_matrix_.block(test_start, 0, n_test_cases, cached_full_matrix_.cols());
    } else {
        // Build test matrix
        std::vector<size_t> test_indices(n_test_cases);
        std::iota(test_indices.begin(), test_indices.end(), test_start);
        build_design_matrix(A, X, feature_indices, test_indices);
    }
    
    if (A.rows() == 0 || A.cols() == 0) return 0.0;

    // Predict values using vectorized operations
    Eigen::VectorXd y_hat = A * coefficients_;
    
    // Calculate SSE using Eigen's vectorized operations
    double total_error = 0.0;
    for (size_t i = 0; i < n_test_cases; ++i) {
        double diff = y[test_start + i] - y_hat(i);
        total_error += diff * diff;
    }

    return total_error;
}

std::vector<double> LinearQuadraticModel::get_final_coefficients(
    const DataMatrix& X,
    const std::vector<double>& y,
    const std::vector<int>& feature_indices)
{
    // Fit the model using all available data
    fit(X, y, feature_indices, 0, 0);

    // Convert Eigen::VectorXd to std::vector<double>
    std::vector<double> result_coeffs(coefficients_.size());
    Eigen::VectorXd::Map(&result_coeffs[0], coefficients_.size()) = coefficients_;
    return result_coeffs;
}
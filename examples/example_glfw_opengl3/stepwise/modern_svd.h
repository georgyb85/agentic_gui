#pragma once

#include <vector>

// A faithful, modernized port of the legacy SingularValueDecomp class.
// This preserves the exact numerical algorithms from SVDCMP.CPP to ensure
// identical results to the legacy implementation.
class ModernSVD {
public:
    // Constructor allocates all necessary memory.
    // save_a_matrix: if true, preserves the original design matrix 'a'
    // by performing the decomposition on a copy 'u'.
    ModernSVD(int n_rows, int n_cols, bool save_a_matrix = false);

    // Default destructor is sufficient due to std::vector.
    ~ModernSVD() = default;

    // No copy/move semantics to keep it simple.
    ModernSVD(const ModernSVD&) = delete;
    ModernSVD& operator=(const ModernSVD&) = delete;

    // Accessors for filling the design matrix and RHS vector
    double* get_design_matrix_ptr() { return a.data(); }
    double* get_rhs_vector_ptr() { return b.data(); }

    // Computes the singular value decomposition of the matrix 'a'.
    void decompose();

    // Solves Ax=b using the SVD.
    // threshold: A relative limit for singular values (e.g., 1e-7).
    // solution: Output vector where the solution is placed.
    void back_substitute(double threshold, double* solution);

    // Check if the constructor was successful.
    bool is_ok() const { return ok; }
    int rows() const { return n_rows_; }
    int cols() const { return n_cols_; }

private:
    // Private helper methods - direct ports from legacy code
    void bidiag();
    double bid1(int col, double scale);
    double bid2(int col, double scale);
    void right();
    void left();
    void cancel(int low, int high);
    void qr(int low, int high);
    void qr_mrot(int col, double sine, double cosine);
    void qr_vrot(int col, double sine, double cosine);
    static double pythag(double a, double b);

    int n_rows_;
    int n_cols_;
    bool ok;
    double norm;

    // Data members matching the legacy class
    std::vector<double> a;    // Input design matrix, becomes U if u is not used
    std::vector<double> u;    // Optional storage for U if 'a' is preserved
    std::vector<double> w;    // Singular values
    std::vector<double> v;    // V matrix
    std::vector<double> b;    // Right-hand-side vector
    std::vector<double> work; // Scratch vector
};
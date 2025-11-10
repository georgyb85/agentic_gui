#pragma once

#include <vector>
#include <string>
#include <memory>
#include "../aligned_allocator.h"

// Modern data container to replace raw double arrays
class DataMatrix {
private:
    AlignedVector<double> data_;  // Column-major storage: [n_cols * n_rows]
    size_t n_rows_;
    size_t n_cols_;
    std::vector<std::string> column_names_;
    
public:
    // Constructors
    DataMatrix() : n_rows_(0), n_cols_(0) {}
    DataMatrix(size_t rows, size_t cols);
    
    // Basic accessors
    size_t rows() const { return n_rows_; }
    size_t cols() const { return n_cols_; }
    
    // Data access - column-major for cache efficiency in statistical operations
    double* get_column(size_t col);
    const double* get_column(size_t col) const;
    
    double& operator()(size_t row, size_t col);
    const double& operator()(size_t row, size_t col) const;
    
    // Element access method
    double get(size_t row, size_t col) const;
    
    // Column names
    void set_column_names(const std::vector<std::string>& names);
    const std::vector<std::string>& get_column_names() const;
    std::string get_column_name(size_t col) const;
    int find_column_index(const std::string& name) const;
    
    // Data manipulation
    void resize(size_t rows, size_t cols);
    void standardize_column(size_t col);
    void copy_column(size_t source_col, std::vector<double>& dest) const;
    
    // Raw data access (for legacy compatibility)
    double* raw_data() { return data_.data(); }
    const double* raw_data() const { return data_.data(); }
};

// Feature set management
class FeatureSet {
private:
    std::vector<int> feature_indices_;
    double performance_criterion_;
    double p_value_model_;
    double p_value_change_;
    
public:
    FeatureSet() : performance_criterion_(-1e60), p_value_model_(1.0), p_value_change_(1.0) {}
    
    // Feature management
    void add_feature(int index) { feature_indices_.push_back(index); }
    void set_features(const std::vector<int>& indices) { feature_indices_ = indices; }
    const std::vector<int>& get_features() const { return feature_indices_; }
    size_t size() const { return feature_indices_.size(); }
    bool empty() const { return feature_indices_.empty(); }
    
    // Performance metrics
    void set_performance(double criterion) { performance_criterion_ = criterion; }
    double get_performance() const { return performance_criterion_; }
    
    void set_model_p_value(double p_val) { p_value_model_ = p_val; }
    double get_model_p_value() const { return p_value_model_; }
    
    void set_change_p_value(double p_val) { p_value_change_ = p_val; }
    double get_change_p_value() const { return p_value_change_; }
    
    // Comparison operators for sorting
    bool operator<(const FeatureSet& other) const {
        return performance_criterion_ > other.performance_criterion_;  // Best first
    }
};
#include "data_matrix.h"
#include <stdexcept>
#include <numeric>
#include <cmath>
#include <algorithm>

DataMatrix::DataMatrix(size_t rows, size_t cols) 
    : n_rows_(rows), n_cols_(cols) {
    data_.resize(rows * cols);
    column_names_.resize(cols);
}

double* DataMatrix::get_column(size_t col) {
    if (col >= n_cols_) {
        throw std::out_of_range("Column index out of range");
    }
    return data_.data() + col * n_rows_;
}

const double* DataMatrix::get_column(size_t col) const {
    if (col >= n_cols_) {
        throw std::out_of_range("Column index out of range");
    }
    return data_.data() + col * n_rows_;
}

double& DataMatrix::operator()(size_t row, size_t col) {
    if (row >= n_rows_ || col >= n_cols_) {
        throw std::out_of_range("Matrix indices out of range");
    }
    return data_[col * n_rows_ + row];
}

const double& DataMatrix::operator()(size_t row, size_t col) const {
    if (row >= n_rows_ || col >= n_cols_) {
        throw std::out_of_range("Matrix indices out of range");
    }
    return data_[col * n_rows_ + row];
}

double DataMatrix::get(size_t row, size_t col) const {
    if (row >= n_rows_ || col >= n_cols_) {
        throw std::out_of_range("Matrix indices out of range");
    }
    return data_[col * n_rows_ + row];
}

void DataMatrix::set_column_names(const std::vector<std::string>& names) {
    if (names.size() != n_cols_) {
        throw std::invalid_argument("Number of names must match number of columns");
    }
    column_names_ = names;
}

const std::vector<std::string>& DataMatrix::get_column_names() const {
    return column_names_;
}

std::string DataMatrix::get_column_name(size_t col) const {
    if (col >= n_cols_) {
        throw std::out_of_range("Column index out of range");
    }
    if (col < column_names_.size()) {
        return column_names_[col];
    }
    return "Col_" + std::to_string(col);
}

int DataMatrix::find_column_index(const std::string& name) const {
    auto it = std::find(column_names_.begin(), column_names_.end(), name);
    if (it != column_names_.end()) {
        return static_cast<int>(std::distance(column_names_.begin(), it));
    }
    return -1;  // Not found
}

void DataMatrix::resize(size_t rows, size_t cols) {
    n_rows_ = rows;
    n_cols_ = cols;
    data_.resize(rows * cols);
    column_names_.resize(cols);
}

void DataMatrix::standardize_column(size_t col) {
    if (col >= n_cols_) {
        throw std::out_of_range("Column index out of range");
    }
    
    double* column_data = get_column(col);
    
    // Calculate mean
    double mean = 0.0;
    for (size_t i = 0; i < n_rows_; ++i) {
        mean += column_data[i];
    }
    mean /= n_rows_;
    
    // Calculate standard deviation
    double sum_sq_diff = 0.0;
    for (size_t i = 0; i < n_rows_; ++i) {
        double diff = column_data[i] - mean;
        sum_sq_diff += diff * diff;
    }
    double std_dev = std::sqrt(sum_sq_diff / n_rows_);  // Use population std dev to match legacy
    
    if (std_dev == 0.0) {
        std_dev = 1.0;  // Avoid division by zero
    }
    
    // Standardize
    for (size_t i = 0; i < n_rows_; ++i) {
        column_data[i] = (column_data[i] - mean) / std_dev;
    }
}

void DataMatrix::copy_column(size_t source_col, std::vector<double>& dest) const {
    if (source_col >= n_cols_) {
        throw std::out_of_range("Column index out of range");
    }
    
    dest.resize(n_rows_);
    const double* column_data = get_column(source_col);
    std::copy(column_data, column_data + n_rows_, dest.begin());
}
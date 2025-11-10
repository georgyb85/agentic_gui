#pragma once

#include <vector>
#include <string>
#include <stdexcept>

namespace lfs {

// Simple data matrix class for holding feature data
class DataMatrix {
private:
    std::vector<double> data;
    size_t rows;
    size_t cols;
    std::vector<std::string> column_names;
    
public:
    DataMatrix(size_t n_rows, size_t n_cols) 
        : rows(n_rows), cols(n_cols), data(n_rows * n_cols, 0.0) {}
    
    // Access element at (row, col)
    double& operator()(size_t row, size_t col) {
        if (row >= rows || col >= cols) {
            throw std::out_of_range("DataMatrix index out of bounds");
        }
        return data[row * cols + col];
    }
    
    const double& operator()(size_t row, size_t col) const {
        if (row >= rows || col >= cols) {
            throw std::out_of_range("DataMatrix index out of bounds");
        }
        return data[row * cols + col];
    }
    
    // Get raw data pointer
    double* get_data() { return data.data(); }
    const double* get_data() const { return data.data(); }
    
    // Get dimensions
    size_t get_rows() const { return rows; }
    size_t get_cols() const { return cols; }
    
    // Set column names
    void set_column_names(const std::vector<std::string>& names) {
        column_names = names;
    }
    
    // Get column names
    const std::vector<std::string>& get_column_names() const {
        return column_names;
    }
};

} // namespace lfs
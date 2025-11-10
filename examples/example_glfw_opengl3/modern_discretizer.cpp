#include "modern_discretizer.h"
#include "modern_algorithms.h"

template<typename T>
AlignedVector<short int> discretize_exact(
    const chronosflow::ColumnView<T>& column,
    int num_bins
) {
    const int n_cases = static_cast<int>(column.size());
    if (n_cases == 0) return {};
    
    // Convert column data to double vector for partition algorithm
    std::vector<double> data(n_cases);
    const T* raw_data = column.data();
    for (int i = 0; i < n_cases; ++i) {
        data[i] = static_cast<double>(raw_data[i]);
    }
    
    // Prepare output bin array
    AlignedVector<short int> bins(n_cases);
    
    // Call the modern partition algorithm
    int npart = num_bins;
    ModernAlgorithms::partition(n_cases, data, npart, nullptr, bins);
    
    return bins;
}

// Version with bounds output for debugging
template<typename T>
AlignedVector<short int> discretize_with_bounds(
    const chronosflow::ColumnView<T>& column,
    int num_bins,
    std::vector<double>& bounds
) {
    const int n_cases = static_cast<int>(column.size());
    if (n_cases == 0) return {};
    
    // Convert column data to double vector for partition algorithm
    std::vector<double> data(n_cases);
    const T* raw_data = column.data();
    for (int i = 0; i < n_cases; ++i) {
        data[i] = static_cast<double>(raw_data[i]);
    }
    
    // Prepare output bin array
    AlignedVector<short int> bins(n_cases);
    
    // Call the modern partition algorithm with bounds output
    int npart = num_bins;
    ModernAlgorithms::partition(n_cases, data, npart, &bounds, bins);
    
    return bins;
}

// Explicit template instantiations
template AlignedVector<short int> discretize_exact<double>(
    const chronosflow::ColumnView<double>&, int);

template AlignedVector<short int> discretize_with_bounds<double>(
    const chronosflow::ColumnView<double>&, int, std::vector<double>&);
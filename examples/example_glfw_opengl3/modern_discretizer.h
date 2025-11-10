#pragma once
#include "column_view.h"
#include "aligned_allocator.h"
#include <vector>

// Modern C++ discretizer using the exact partition algorithm
template<typename T>
AlignedVector<short int> discretize_exact(
    const chronosflow::ColumnView<T>& column,
    int num_bins
);

// Version that also returns bounds for debugging
template<typename T>
AlignedVector<short int> discretize_with_bounds(
    const chronosflow::ColumnView<T>& column,
    int num_bins,
    std::vector<double>& bounds
);
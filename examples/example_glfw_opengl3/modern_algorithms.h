#pragma once
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "aligned_allocator.h"

namespace ModernAlgorithms {

// Modern C++ implementation of the partition algorithm
// Maintains exact same logic as legacy PART.CPP for identical results
void partition(
    int n,                                    // Number of cases
    const std::vector<double>& data,         // Input data
    int& npart,                              // In/Out: number of partitions
    std::vector<double>* bounds,            // Output: upper bounds (can be nullptr)
    AlignedVector<short int>& bins          // Output: bin assignments
);

// Modern C++ implementation of qsortdsi (quicksort with index tracking)
// Sorts data array and maintains corresponding slave indices
void qsortdsi(
    int first,
    int last,
    std::vector<double>& data,
    std::vector<int>& indices
);

// Modern implementations of compute_mi and uncert_reduc
double compute_mi(
    int ncases,
    int nbins_pred,
    const int* pred1_bin,
    const int* pred2_bin,
    int nbins_target,
    const int* target_bin,
    const double* target_marginal,
    int* bin_counts
);

void uncert_reduc(
    int ncases,
    int nbins_pred,
    const int* pred1_bin,
    const int* pred2_bin,
    int nbins_target,
    const int* target_bin,
    const double* target_marginal,
    double* row_dep,
    double* col_dep,
    double* sym,
    int* rmarg,
    int* bin_counts
);

} // namespace ModernAlgorithms
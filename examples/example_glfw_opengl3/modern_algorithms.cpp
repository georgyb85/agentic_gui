#include "modern_algorithms.h"
#include <cstring>

namespace ModernAlgorithms {

// Modern quicksort with index tracking - identical logic to legacy qsortdsi
void qsortdsi(int first, int last, std::vector<double>& data, std::vector<int>& indices) {
    if (first >= last) return;
    
    double split = data[(first + last) / 2];
    int lower = first;
    int upper = last;
    
    do {
        while (split > data[lower])
            ++lower;
        while (split < data[upper])
            --upper;
            
        if (lower == upper) {
            ++lower;
            --upper;
        }
        else if (lower < upper) {
            // Swap data
            double temp = data[lower];
            data[lower] = data[upper];
            data[upper] = temp;
            
            // Swap indices
            int itemp = indices[lower];
            indices[lower] = indices[upper];
            indices[upper] = itemp;
            
            ++lower;
            --upper;
        }
    } while (lower <= upper);
    
    if (first < upper)
        qsortdsi(first, upper, data, indices);
    if (lower < last)
        qsortdsi(lower, last, data, indices);
}

// Modern partition algorithm - exact reimplementation of legacy PART.CPP
void partition(
    int n,
    const std::vector<double>& data,
    int& npart,
    std::vector<double>* bounds,
    AlignedVector<short int>& bins
) {
    if (npart > n)  // Defend against invalid input
        npart = n;
    
    int np = npart;  // Working number of partitions
    
    // Allocate working arrays - ensure bin_end has enough space for splits
    std::vector<double> x(n);
    std::vector<int> ix(n);
    std::vector<int> indices(n);
    std::vector<int> bin_end(n);  // Allocate max possible size like legacy
    
    // Copy data and initialize indices
    for (int i = 0; i < n; i++) {
        x[i] = data[i];
        indices[i] = i;
    }
    
    // Sort the data with index tracking
    qsortdsi(0, n - 1, x, indices);
    
    // Compute integer rank array that identifies ties
    ix[0] = 0;
    int k = 0;
    for (int i = 1; i < n; i++) {
        if (x[i] - x[i-1] >= 1.e-12 * (1.0 + std::fabs(x[i]) + std::fabs(x[i-1])))
            ++k;  // Not a tie, advance unique value counter
        ix[i] = k;
    }
    
    // Compute initial bounds based on equal number of cases per bin
    k = 0;
    for (int i = 0; i < np; i++) {
        int j = (n - k) / (np - i);  // Number of cases in this partition
        k += j;
        bin_end[i] = k - 1;  // Upper bound of this bin
    }
    
    // Iterate until no partition boundary splits a tie
    bool tie_found;
    do {
        tie_found = false;
        
        // Check for boundaries that split ties
        for (int ibound = 0; ibound < np - 1; ibound++) {
            if (ix[bin_end[ibound]] == ix[bin_end[ibound] + 1]) {  // Splits a tie?
                // Remove this bound
                for (int i = ibound + 1; i < np; i++)
                    bin_end[i - 1] = bin_end[i];
                --np;
                tie_found = true;
                break;
            }
        }
        
        if (!tie_found)
            break;
        
        // Try splitting each remaining bin
        int istart = 0;
        int nbest = -1;
        int ibound_best = -1;
        int isplit_best = -1;
        
        for (int ibound = 0; ibound < np; ibound++) {
            int istop = bin_end[ibound];
            // Process bin from istart through istop
            for (int i = istart; i < istop; i++) {
                if (ix[i] == ix[i + 1])  // Skip if splits a tie
                    continue;
                int nleft = i - istart + 1;
                int nright = istop - i;
                if (nleft < nright) {
                    if (nleft > nbest) {
                        nbest = nleft;
                        ibound_best = ibound;
                        isplit_best = i;
                    }
                }
                else {
                    if (nright > nbest) {
                        nbest = nright;
                        ibound_best = ibound;
                        isplit_best = i;
                    }
                }
            }
            istart = istop + 1;
        }
        
        // Split at the best location found
        if (nbest > 0) {
            // Shift bounds up to make room - exact logic from legacy
            for (int ibound = np - 1; ibound >= ibound_best; ibound--)
                bin_end[ibound + 1] = bin_end[ibound];
            bin_end[ibound_best] = isplit_best;
            ++np;
        }
        
    } while (tie_found);
    
    // Set the actual number of partitions found
    npart = np;
    
    // Compute bounds if requested
    if (bounds != nullptr) {
        bounds->resize(np);
        for (int i = 0; i < np; i++) {
            (*bounds)[i] = x[bin_end[i]];
        }
    }
    
    // Assign cases to bins - exact logic from legacy
    bins.resize(n);
    int istart = 0;
    for (int ibound = 0; ibound < np; ibound++) {
        int istop = bin_end[ibound];
        for (int i = istart; i <= istop; i++)
            bins[indices[i]] = static_cast<short int>(ibound);
        istart = istop + 1;
    }
}

// Modern implementation of compute_mi - exact logic from legacy
double compute_mi(
    int ncases,
    int nbins_pred,
    const int* pred1_bin,
    const int* pred2_bin,
    int nbins_target,
    const int* target_bin,
    const double* target_marginal,
    int* bin_counts
) {
    int nbins_pred_squared = nbins_pred * nbins_pred;
    
    // Zero all bin counts
    std::memset(bin_counts, 0, nbins_pred_squared * nbins_target * sizeof(int));
    
    // Compute bin counts for bivariate predictor and full table
    for (int i = 0; i < ncases; i++) {
        int k = pred1_bin[i] * nbins_pred + pred2_bin[i];
        ++bin_counts[k * nbins_target + target_bin[i]];
    }
    
    // Compute mutual information
    double MI = 0.0;
    for (int i = 0; i < nbins_pred_squared; i++) {
        int k = 0;
        for (int j = 0; j < nbins_target; j++)  // Sum across target bins
            k += bin_counts[i * nbins_target + j];
        double px = static_cast<double>(k) / static_cast<double>(ncases);
        
        for (int j = 0; j < nbins_target; j++) {
            double py = target_marginal[j];
            double pxy = static_cast<double>(bin_counts[i * nbins_target + j]) / 
                        static_cast<double>(ncases);
            if (pxy > 0.0)
                MI += pxy * std::log(pxy / (px * py));
        }
    }
    
    // Normalize 0-1
    if (nbins_pred_squared <= nbins_target)
        MI /= std::log(static_cast<double>(nbins_pred_squared));
    else
        MI /= std::log(static_cast<double>(nbins_target));
    
    return MI;
}

// Modern implementation of uncert_reduc - exact logic from legacy
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
) {
    int nbins_pred_squared = nbins_pred * nbins_pred;
    
    // Zero all bin counts
    std::memset(bin_counts, 0, nbins_pred_squared * nbins_target * sizeof(int));
    
    // Compute bin counts
    for (int i = 0; i < ncases; i++) {
        int k = pred1_bin[i] * nbins_pred + pred2_bin[i];
        ++bin_counts[k * nbins_target + target_bin[i]];
    }
    
    int total = 0;
    for (int irow = 0; irow < nbins_pred_squared; irow++) {
        rmarg[irow] = 0;
        for (int icol = 0; icol < nbins_target; icol++)
            rmarg[irow] += bin_counts[irow * nbins_target + icol];
        total += rmarg[irow];
    }
    
    // Compute intermediate quantities
    double Urow = 0.0;
    for (int irow = 0; irow < nbins_pred_squared; irow++) {
        if (rmarg[irow]) {
            double p = static_cast<double>(rmarg[irow]) / static_cast<double>(total);
            Urow -= p * std::log(p);
        }
    }
    
    double Ucol = 0.0;
    for (int icol = 0; icol < nbins_target; icol++) {
        if (target_marginal[icol] > 0.0) {
            double p = target_marginal[icol];
            Ucol -= p * std::log(p);
        }
    }
    
    double Ujoint = 0.0;
    for (int irow = 0; irow < nbins_pred_squared; irow++) {
        for (int icol = 0; icol < nbins_target; icol++) {
            if (bin_counts[irow * nbins_target + icol]) {
                double p = static_cast<double>(bin_counts[irow * nbins_target + icol]) / 
                          static_cast<double>(total);
                Ujoint -= p * std::log(p);
            }
        }
    }
    
    double numer = Urow + Ucol - Ujoint;
    *row_dep = (Urow > 0.0) ? numer / Urow : 0.0;
    *col_dep = (Ucol > 0.0) ? numer / Ucol : 0.0;
    *sym = (Urow + Ucol > 0.0) ? 2.0 * numer / (Urow + Ucol) : 0.0;
}

} // namespace ModernAlgorithms
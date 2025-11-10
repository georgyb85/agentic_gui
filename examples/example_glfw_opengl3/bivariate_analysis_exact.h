#pragma once
#include <vector>
#include <string>
#include "analytics_dataframe.h"

struct BivariateResult {
    std::string pred1_name;
    std::string pred2_name;
    double criterion;
    double p_value_solo;     // individual p-value (mcpt_solo equivalent)  
    double p_value_bestof;   // family-wise p-value (mcpt_bestof equivalent)
    int n_permutations;      // number of permutations used (0 if not computed)
    int mcpt_type;           // 1=complete, 2=cyclic, 0=none
    int criterion_type;      // 1=mutual_information, 2=uncertainty_reduction
    bool is_significant;     // true if individual p < 0.05
};

// The new, modern computational engine.
// Takes pointers to individual, pre-binned columns. ZERO-COPY.
std::vector<BivariateResult> run_analysis_on_binned_data(
    int n_cases,
    const std::vector<std::string>& predictor_names,
    const std::vector<const short int*>& predictor_bins_ptrs,
    const short int* target_bin,
    int nbins_pred,
    int nbins_target,
    int criterion_type = 1,     // 1=mutual_information, 2=uncertainty_reduction
    int mcpt_type = 0,          // 0=none, 1=complete, 2=cyclic
    int n_permutations = 0      // Number of MCPT replications
);

// The high-level orchestrator. This is the primary function to call from main().
std::vector<BivariateResult> screen_bivariate(
    const chronosflow::AnalyticsDataFrame& df,
    const std::vector<std::string>& predictor_names,
    const std::string& target_name,
    int nbins_pred,
    int nbins_target,
    int criterion_type = 1,     // 1=mutual_information, 2=uncertainty_reduction
    int mcpt_type = 0,          // 0=none, 1=complete, 2=cyclic
    int n_permutations = 0      // Number of MCPT replications
);
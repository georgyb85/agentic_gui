#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace tssb;
using namespace tssb::validation;

void analyze_cmma(const std::vector<OHLCVBar>& bars, const std::vector<TSBBIndicatorBar>& tssb_bars,
                 const std::string& name, int lookback, int atr_length) {

    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "DETAILED ANALYSIS: " << name << "\n";
    std::cout << "Params: lookback=" << lookback << ", atr_length=" << atr_length << "\n";
    std::cout << std::string(70, '=') << "\n\n";

    // Get CSV values
    SingleMarketSeries series = OHLCVParser::to_series(bars);
    auto csv_values = TSBBOutputParser::extract_indicator_aligned(tssb_bars, bars, name);

    // Compute our values
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::CloseMinusMovingAverage;
    req.name = name;
    req.params[0] = lookback;
    req.params[1] = atr_length;
    auto result = compute_single_indicator(series, req);

    // Find first valid bar
    size_t first_valid = 0;
    for (size_t i = 0; i < bars.size(); ++i) {
        if (std::isfinite(csv_values[i])) {
            first_valid = i;
            break;
        }
    }

    // Collect statistics
    std::vector<double> csv_vals, computed_vals, ratios, errors;
    std::vector<size_t> indices;

    for (size_t i = first_valid; i < bars.size(); ++i) {
        if (std::isfinite(csv_values[i]) && std::isfinite(result.values[i])) {
            double csv_val = csv_values[i];
            double comp_val = result.values[i];

            csv_vals.push_back(csv_val);
            computed_vals.push_back(comp_val);
            errors.push_back(comp_val - csv_val);

            // Calculate ratio (avoid division by values near zero)
            if (std::abs(csv_val) > 0.1) {
                ratios.push_back(comp_val / csv_val);
                indices.push_back(i);
            }
        }
    }

    // Calculate percentiles
    auto csv_sorted = csv_vals;
    auto comp_sorted = computed_vals;
    auto ratio_sorted = ratios;
    std::sort(csv_sorted.begin(), csv_sorted.end());
    std::sort(comp_sorted.begin(), comp_sorted.end());
    std::sort(ratio_sorted.begin(), ratio_sorted.end());

    double csv_min = csv_sorted.front();
    double csv_max = csv_sorted.back();
    double csv_p05 = csv_sorted[csv_sorted.size() * 5 / 100];
    double csv_p25 = csv_sorted[csv_sorted.size() / 4];
    double csv_p50 = csv_sorted[csv_sorted.size() / 2];
    double csv_p75 = csv_sorted[csv_sorted.size() * 3 / 4];
    double csv_p95 = csv_sorted[csv_sorted.size() * 95 / 100];

    double comp_min = comp_sorted.front();
    double comp_max = comp_sorted.back();
    double comp_p05 = comp_sorted[comp_sorted.size() * 5 / 100];
    double comp_p25 = comp_sorted[comp_sorted.size() / 4];
    double comp_p50 = comp_sorted[comp_sorted.size() / 2];
    double comp_p75 = comp_sorted[comp_sorted.size() * 3 / 4];
    double comp_p95 = comp_sorted[comp_sorted.size() * 95 / 100];

    double ratio_min = ratio_sorted.front();
    double ratio_max = ratio_sorted.back();
    double ratio_p05 = ratio_sorted[ratio_sorted.size() * 5 / 100];
    double ratio_p25 = ratio_sorted[ratio_sorted.size() / 4];
    double ratio_p50 = ratio_sorted[ratio_sorted.size() / 2];
    double ratio_p75 = ratio_sorted[ratio_sorted.size() * 3 / 4];
    double ratio_p95 = ratio_sorted[ratio_sorted.size() * 95 / 100];

    std::cout << "VALUE DISTRIBUTION:\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "                Min      P05      P25      P50      P75      P95      Max\n";
    std::cout << "CSV:    " << std::setw(8) << csv_min << std::setw(9) << csv_p05
              << std::setw(9) << csv_p25 << std::setw(9) << csv_p50
              << std::setw(9) << csv_p75 << std::setw(9) << csv_p95 << std::setw(9) << csv_max << "\n";
    std::cout << "Computed:" << std::setw(8) << comp_min << std::setw(9) << comp_p05
              << std::setw(9) << comp_p25 << std::setw(9) << comp_p50
              << std::setw(9) << comp_p75 << std::setw(9) << comp_p95 << std::setw(9) << comp_max << "\n\n";

    std::cout << "RATIO DISTRIBUTION (Computed/CSV, excluding near-zero values):\n";
    std::cout << "Min:  " << std::setw(8) << ratio_min << "  (most compressed)\n";
    std::cout << "P05:  " << std::setw(8) << ratio_p05 << "\n";
    std::cout << "P25:  " << std::setw(8) << ratio_p25 << "\n";
    std::cout << "P50:  " << std::setw(8) << ratio_p50 << "  (median ratio)\n";
    std::cout << "P75:  " << std::setw(8) << ratio_p75 << "\n";
    std::cout << "P95:  " << std::setw(8) << ratio_p95 << "\n";
    std::cout << "Max:  " << std::setw(8) << ratio_max << "  (most expanded)\n\n";

    // Analyze ratio by value magnitude
    std::cout << "RATIO BY VALUE MAGNITUDE:\n";
    std::cout << std::setw(20) << "CSV Range" << std::setw(12) << "Count"
              << std::setw(12) << "Avg Ratio" << std::setw(12) << "Std Ratio" << "\n";
    std::cout << std::string(56, '-') << "\n";

    struct RangeBin {
        double min_val, max_val;
        std::vector<double> ratios;
    };

    std::vector<RangeBin> bins = {
        {-50, -20, {}},
        {-20, -10, {}},
        {-10, -5, {}},
        {-5, -2, {}},
        {-2, -1, {}},
        {-1, 0, {}},
        {0, 1, {}},
        {1, 2, {}},
        {2, 5, {}},
        {5, 10, {}},
        {10, 20, {}},
        {20, 50, {}}
    };

    for (size_t i = 0; i < csv_vals.size(); ++i) {
        double csv_val = csv_vals[i];
        double ratio = (std::abs(csv_val) > 0.1) ? computed_vals[i] / csv_val : 0;

        for (auto& bin : bins) {
            if (csv_val >= bin.min_val && csv_val < bin.max_val) {
                bin.ratios.push_back(ratio);
                break;
            }
        }
    }

    for (const auto& bin : bins) {
        if (bin.ratios.empty()) continue;

        double sum = 0, sum_sq = 0;
        for (double r : bin.ratios) {
            sum += r;
            sum_sq += r * r;
        }
        double avg = sum / bin.ratios.size();
        double variance = (sum_sq / bin.ratios.size()) - (avg * avg);
        double std = std::sqrt(std::max(0.0, variance));

        std::cout << "[" << std::setw(6) << bin.min_val << ", " << std::setw(6) << bin.max_val << ")"
                  << std::setw(12) << bin.ratios.size()
                  << std::setw(12) << avg
                  << std::setw(12) << std << "\n";
    }

    // Find most extreme mismatches
    std::cout << "\nMOST EXTREME MISMATCHES (top 10 by absolute error):\n";
    std::cout << std::setw(6) << "Bar" << std::setw(12) << "CSV" << std::setw(12) << "Computed"
              << std::setw(12) << "Error" << std::setw(12) << "Ratio" << "\n";
    std::cout << std::string(54, '-') << "\n";

    std::vector<size_t> error_indices;
    for (size_t i = 0; i < errors.size(); ++i) {
        error_indices.push_back(i);
    }
    std::sort(error_indices.begin(), error_indices.end(),
              [&errors](size_t a, size_t b) { return std::abs(errors[a]) > std::abs(errors[b]); });

    for (int i = 0; i < 10 && i < (int)error_indices.size(); ++i) {
        size_t idx = error_indices[i];
        size_t bar_idx = first_valid + idx;
        double csv_val = csv_vals[idx];
        double comp_val = computed_vals[idx];
        double error = errors[idx];
        double ratio = (std::abs(csv_val) > 0.1) ? comp_val / csv_val : 0;

        std::cout << std::setw(6) << bar_idx
                  << std::setw(12) << csv_val
                  << std::setw(12) << comp_val
                  << std::setw(12) << error
                  << std::setw(12) << ratio << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "CMMA DETAILED DIAGNOSTIC ANALYSIS\n";
    std::cout << "==================================\n";

    analyze_cmma(ohlcv_bars, tssb_bars, "CMMA_S", 10, 250);
    analyze_cmma(ohlcv_bars, tssb_bars, "CMMA_M", 50, 250);
    analyze_cmma(ohlcv_bars, tssb_bars, "CMMA_L", 120, 250);

    return 0;
}

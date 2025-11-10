#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>

using namespace tssb;
using namespace tssb::validation;

struct IndicatorTest {
    std::string name;
    std::string csv_column;
    SingleIndicatorId id;
    std::vector<double> params;
};

void run_validation(const SingleMarketSeries& series,
                   const std::vector<OHLCVBar>& ohlcv_bars,
                   const std::vector<TSBBIndicatorBar>& tssb_bars,
                   const IndicatorTest& test)
{
    // Extract expected values from CSV
    std::vector<double> expected = TSBBOutputParser::extract_indicator_aligned(
        tssb_bars, ohlcv_bars, test.csv_column);

    // Compute indicator
    SingleIndicatorRequest req;
    req.id = test.id;
    req.name = test.name;
    for (size_t i = 0; i < test.params.size(); ++i) {
        req.params[i] = test.params[i];
    }

    auto result = compute_single_indicator(series, req);

    if (!result.success) {
        std::cout << std::setw(20) << std::left << test.name
                  << "  ERROR: " << result.error_message << "\n";
        return;
    }

    // Compute metrics
    double sum_error = 0.0;
    double sum_abs_error = 0.0;
    double sum_squared_error = 0.0;
    double max_error = 0.0;
    int valid_count = 0;
    int sign_mismatches = 0;

    for (std::size_t i = 0; i < std::min(expected.size(), result.values.size()); ++i) {
        if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
            double error = result.values[i] - expected[i];
            double abs_error = std::fabs(error);

            sum_error += error;
            sum_abs_error += abs_error;
            sum_squared_error += error * error;
            max_error = std::max(max_error, abs_error);
            valid_count++;

            // Check sign mismatches (for debugging)
            if ((expected[i] > 0) != (result.values[i] > 0)) {
                sign_mismatches++;
            }
        }
    }

    double mean_error = valid_count > 0 ? sum_error / valid_count : 0.0;
    double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;
    double rmse = valid_count > 0 ? std::sqrt(sum_squared_error / valid_count) : 0.0;
    double sign_mismatch_pct = valid_count > 0 ? 100.0 * sign_mismatches / valid_count : 0.0;

    // Print results
    std::cout << std::setw(20) << std::left << test.name;
    std::cout << std::setw(12) << std::right << std::fixed << std::setprecision(4) << mae;
    std::cout << std::setw(12) << mean_error;
    std::cout << std::setw(12) << max_error;
    std::cout << std::setw(12) << rmse;
    std::cout << std::setw(10) << std::setprecision(1) << sign_mismatch_pct << "%";

    // Status indicator
    if (mae < 0.01) {
        std::cout << "  ✓✓✓ PERFECT";
    } else if (mae < 0.1) {
        std::cout << "  ✓✓ EXCELLENT";
    } else if (mae < 1.0) {
        std::cout << "  ✓ GOOD";
    } else if (mae < 10.0) {
        std::cout << "  ⚠ NEEDS WORK";
    } else {
        std::cout << "  ❌ FAIL";
    }
    std::cout << "\n";

    // Print sample values for high-priority indicators
    if (test.name == "R_PROD_MORLET" || test.name == "REAL_MORLET_10" || test.name == "REAL_MORLET_20") {
        std::cout << "\n  Sample values for " << test.name << ":\n";
        std::cout << "  " << std::setw(8) << "Bar"
                  << std::setw(14) << "Expected"
                  << std::setw(14) << "Computed"
                  << std::setw(14) << "Error\n";
        std::cout << "  " << std::string(48, '-') << "\n";

        std::vector<size_t> sample_indices = {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};
        for (size_t idx : sample_indices) {
            if (idx < expected.size() && idx < result.values.size()) {
                if (std::isfinite(expected[idx]) && std::isfinite(result.values[idx])) {
                    double error = result.values[idx] - expected[idx];
                    std::cout << "  " << std::setw(8) << idx
                              << std::setw(14) << std::setprecision(6) << expected[idx]
                              << std::setw(14) << result.values[idx]
                              << std::setw(14) << error << "\n";
                }
            }
        }
        std::cout << "\n";
    }
}

int main() {
    // Load data
    auto ohlcv_bars = OHLCVParser::parse_file("/mnt/c/masters/timothy masters/btc25_3.txt");
    auto tssb_bars = TSBBOutputParser::parse_file("/mnt/c/masters/timothy masters/BTC25_3 HM.CSV");
    auto series = OHLCVParser::to_series(ohlcv_bars);

    std::cout << "Loaded " << ohlcv_bars.size() << " OHLCV bars\n";
    std::cout << "Loaded " << tssb_bars.size() << " TSSB indicator bars\n\n";

    // Define tests - prioritize R_PROD_MORLET and REAL_MORLET
    std::vector<IndicatorTest> tests = {
        // PRIORITY: Real Morlet indicators
        {"REAL_MORLET_10", "REAL_MORLET_10", SingleIndicatorId::RealMorlet, {10}},
        {"REAL_MORLET_20", "REAL_MORLET_20", SingleIndicatorId::RealMorlet, {20}},

        // PRIORITY: Product indicator (reported as broken)
        {"R_PROD_MORLET", "R_PROD_MORLET", SingleIndicatorId::RealProductMorlet, {10}},

        // Other Morlet variants
        {"IMAG_MORLET_10", "IMAG_MORLET_10", SingleIndicatorId::ImagMorlet, {10}},
        {"IMAG_MORLET_20", "IMAG_MORLET_20", SingleIndicatorId::ImagMorlet, {20}},
        {"R_DIFF_MORLET", "R_DIFF_MORLET", SingleIndicatorId::RealDiffMorlet, {10}},

        // Daubechies wavelets
        {"DAUB_MEAN_32_2", "DAUB_MEAN_32_2", SingleIndicatorId::DaubMean, {32, 2}},
        {"DAUB_MIN_32", "DAUB_MIN_32", SingleIndicatorId::DaubMin, {32, 2}},
        {"DAUB_MAX_32", "DAUB_MAX_32", SingleIndicatorId::DaubMax, {32, 2}},
        {"DAUB_STD_32", "DAUB_STD_32", SingleIndicatorId::DaubStd, {32, 2}},
        {"DAUB_ENERGY_32", "DAUB_ENERGY_32", SingleIndicatorId::DaubEnergy, {32, 2}},
        {"DAUB_NL_ENERGY", "DAUB_NL_ENERGY", SingleIndicatorId::DaubNlEnergy, {32, 2}},
        {"DAUB_CURVE", "DAUB_CURVE", SingleIndicatorId::DaubCurve, {32, 2}},
    };

    std::cout << "Wavelet Indicator Validation - MAE-Based Analysis\n";
    std::cout << std::string(100, '=') << "\n\n";

    std::cout << std::setw(20) << std::left << "Indicator";
    std::cout << std::setw(12) << std::right << "MAE";
    std::cout << std::setw(12) << "Mean Error";
    std::cout << std::setw(12) << "Max Error";
    std::cout << std::setw(12) << "RMSE";
    std::cout << std::setw(10) << "SignFlip%";
    std::cout << "  Status\n";
    std::cout << std::string(100, '-') << "\n";

    for (const auto& test : tests) {
        run_validation(series, ohlcv_bars, tssb_bars, test);
    }

    std::cout << "\nLegend:\n";
    std::cout << "  MAE < 0.01:    ✓✓✓ PERFECT\n";
    std::cout << "  MAE < 0.1:     ✓✓ EXCELLENT\n";
    std::cout << "  MAE < 1.0:     ✓ GOOD\n";
    std::cout << "  MAE < 10.0:    ⚠ NEEDS WORK\n";
    std::cout << "  MAE >= 10.0:   ❌ FAIL\n";

    return 0;
}

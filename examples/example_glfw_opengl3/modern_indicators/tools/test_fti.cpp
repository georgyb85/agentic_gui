#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "====================================================================\n";
    std::cout << "FTI INDICATOR VALIDATION\n";
    std::cout << "====================================================================\n\n";

    // Convert to series
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    struct IndicatorTest {
        std::string name;
        std::string csv_col;
        SingleIndicatorId id;
        std::vector<double> params;
    };

    // Parameters from var.txt:
    // FTI_MAJOR_LP: FTI MAJOR LOWPASS 40 10 5 15
    // FTI_LARGEST: FTI LARGEST FTI 30 8 5 12
    // FTI_CRAT: FTI CRAT 35 10 6 15
    // FTI_BEST_CRAT: FTI MINOR BEST CRAT 40 12 4 20
    // FTILOW: FTI LOWPASS 6 4 6
    // FTIMINLP: FTI MINOR LOWPASS 26 6 5 10
    // FTI10: FTI FTI 36 6 10

    std::vector<IndicatorTest> tests = {
        // FTI indicators: BlockSize HalfLength Period (or LowPeriod HighPeriod)
        {"FTILOW", "FTILOW", SingleIndicatorId::FtiLowpass, {6, 4, 6}},
        {"FTI10", "FTI10", SingleIndicatorId::FtiBestFti, {36, 6, 10}},
        {"FTIMINLP", "FTIMINLP", SingleIndicatorId::FtiMinorLowpass, {26, 6, 5, 10}},
        {"FTI_MAJOR_LP", "FTI_MAJOR_LP", SingleIndicatorId::FtiMajorLowpass, {40, 10, 5, 15}},
        {"FTI_LARGEST", "FTI_LARGEST", SingleIndicatorId::FtiLargest, {30, 8, 5, 12}},
        {"FTI_CRAT", "FTI_CRAT", SingleIndicatorId::FtiCrat, {35, 10, 6, 15}},
        {"FTI_BEST_CRAT", "FTI_BEST_CRAT", SingleIndicatorId::FtiMinorBestCrat, {40, 12, 4, 20}},
    };

    for (const auto& test : tests) {
        std::cout << "====================================================================\n";
        std::cout << test.name << " (BlockSize=" << test.params[0]
                  << ", HalfLength=" << test.params[1];
        if (test.params.size() == 3) {
            std::cout << ", Period=" << test.params[2];
        } else {
            std::cout << ", LowPeriod=" << test.params[2]
                      << ", HighPeriod=" << test.params[3];
        }
        std::cout << ")\n";
        std::cout << "====================================================================\n";

        // Get expected values
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_col);

        // Compute our values
        SingleIndicatorRequest req;
        req.id = test.id;
        req.name = test.name;
        for (size_t i = 0; i < test.params.size(); ++i) {
            req.params[i] = test.params[i];
        }

        auto result = compute_single_indicator(series, req);

        if (!result.success) {
            std::cerr << "ERROR computing " << test.name << ": " << result.error_message << "\n";
            continue;
        }

        // Find first valid bar
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i]) &&
                std::fabs(expected[i]) > 1e-6) {
                first_valid = i;
                break;
            }
        }

        std::cout << "First valid bar: " << first_valid << " (date: "
                  << ohlcv_bars[first_valid].date << " " << ohlcv_bars[first_valid].time << ")\n\n";

        // Show first 15 comparisons
        std::cout << "First 15 bars comparison:\n";
        std::cout << std::setw(8) << "Bar"
                  << std::setw(14) << "Expected"
                  << std::setw(14) << "Computed"
                  << std::setw(14) << "Error"
                  << std::setw(14) << "Error %\n";
        std::cout << std::string(64, '-') << "\n";

        for (int j = 0; j < 15 && first_valid + j < ohlcv_bars.size(); ++j) {
            size_t i = first_valid + j;
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
                double error = result.values[i] - expected[i];
                double error_pct = (std::fabs(expected[i]) > 1e-6) ?
                    100.0 * error / expected[i] : 0.0;

                std::cout << std::setw(8) << i
                          << std::setw(14) << std::setprecision(6) << std::fixed << expected[i]
                          << std::setw(14) << result.values[i]
                          << std::setw(14) << error
                          << std::setw(14) << std::setprecision(2) << error_pct << "%\n";
            }
        }

        // Compute statistics for all bars
        double sum_abs_error = 0.0;
        double max_abs_error = 0.0;
        double sum_rel_error = 0.0;
        int valid_count = 0;
        int under_1_pct = 0;
        int under_5_pct = 0;
        int under_10_pct = 0;

        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i]) &&
                std::fabs(expected[i]) > 1e-6) {
                double abs_error = std::fabs(result.values[i] - expected[i]);
                double rel_error = 100.0 * abs_error / std::fabs(expected[i]);

                sum_abs_error += abs_error;
                sum_rel_error += rel_error;
                if (abs_error > max_abs_error) max_abs_error = abs_error;

                if (rel_error < 1.0) under_1_pct++;
                if (rel_error < 5.0) under_5_pct++;
                if (rel_error < 10.0) under_10_pct++;

                valid_count++;
            }
        }

        double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;
        double mean_rel_error = valid_count > 0 ? sum_rel_error / valid_count : 0.0;

        std::cout << "\nSummary:\n";
        std::cout << "  Valid bars: " << valid_count << "\n";
        std::cout << "  MAE: " << std::setprecision(4) << mae << "\n";
        std::cout << "  Max Error: " << std::setprecision(4) << max_abs_error << "\n";
        std::cout << "  Mean Relative Error: " << std::setprecision(2) << mean_rel_error << "%\n";
        std::cout << "  Bars with error < 1%: " << under_1_pct
                  << " (" << std::setprecision(1) << (100.0 * under_1_pct / valid_count) << "%)\n";
        std::cout << "  Bars with error < 5%: " << under_5_pct
                  << " (" << (100.0 * under_5_pct / valid_count) << "%)\n";
        std::cout << "  Bars with error < 10%: " << under_10_pct
                  << " (" << (100.0 * under_10_pct / valid_count) << "%)\n";

        if (mean_rel_error < 0.1) {
            std::cout << "  Status: ✓✓✓ PERFECT MATCH!\n";
        } else if (mean_rel_error < 1.0) {
            std::cout << "  Status: ✓✓ EXCELLENT\n";
        } else if (mean_rel_error < 5.0) {
            std::cout << "  Status: ✓ GOOD\n";
        } else {
            std::cout << "  Status: ✗ NEEDS INVESTIGATION\n";
        }
        std::cout << "\n";
    }

    return 0;
}

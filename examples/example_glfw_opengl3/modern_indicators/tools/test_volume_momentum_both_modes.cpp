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
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    std::cout << "====================================================================\n";
    std::cout << "VOLUME MOMENTUM: DUAL MODE VALIDATION\n";
    std::cout << "====================================================================\n\n";

    std::cout << "Testing both formula modes:\n";
    std::cout << "  Mode 0 (default): TSSB executable formula (no cube root division)\n";
    std::cout << "  Mode 1: Book/source code formula (with cube root division)\n\n";

    struct TestCase {
        std::string name;
        std::string csv_col;
        int short_length;
        int mult;
    };

    std::vector<TestCase> tests = {
        {"VOL_MOM_S", "VOL_MOM_S", 10, 5},
        {"VOL_MOM_L", "VOL_MOM_L", 50, 4}
    };

    for (const auto& test : tests) {
        std::cout << "====================================================================\n";
        std::cout << test.name << " (short_length=" << test.short_length
                  << ", mult=" << test.mult << ")\n";
        std::cout << "====================================================================\n";

        // Get expected values from CSV (should match mode 0)
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_col);

        // MODE 0: TSSB executable formula
        SingleIndicatorRequest req_mode0;
        req_mode0.id = SingleIndicatorId::VolumeMomentum;
        req_mode0.name = test.name + "_MODE0";
        req_mode0.params[0] = test.short_length;
        req_mode0.params[1] = test.mult;
        req_mode0.params[2] = 0;  // TSSB executable mode

        auto result_mode0 = compute_single_indicator(series, req_mode0);

        // MODE 1: Book/source formula
        SingleIndicatorRequest req_mode1;
        req_mode1.id = SingleIndicatorId::VolumeMomentum;
        req_mode1.name = test.name + "_MODE1";
        req_mode1.params[0] = test.short_length;
        req_mode1.params[1] = test.mult;
        req_mode1.params[2] = 1;  // Book formula mode

        auto result_mode1 = compute_single_indicator(series, req_mode1);

        // Find first valid bar
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::cout << "\nFirst valid CSV bar: " << first_valid << " (date: "
                  << ohlcv_bars[first_valid].date << " " << ohlcv_bars[first_valid].time << ")\n\n";

        // Show first 10 comparisons
        std::cout << "First 10 bars - comparing both modes:\n";
        std::cout << std::setw(8) << "Bar"
                  << std::setw(14) << "TSSB CSV"
                  << std::setw(14) << "Mode 0"
                  << std::setw(14) << "Mode 1"
                  << std::setw(14) << "Diff(0-1)"
                  << std::setw(14) << "Err(Mode0)"
                  << std::setw(14) << "Err(Mode1)\n";
        std::cout << std::string(92, '-') << "\n";

        for (int j = 0; j < 10 && first_valid + j < ohlcv_bars.size(); ++j) {
            size_t i = first_valid + j;
            if (std::isfinite(expected[i]) && std::isfinite(result_mode0.values[i])
                && std::isfinite(result_mode1.values[i])) {

                double error_mode0 = result_mode0.values[i] - expected[i];
                double error_mode1 = result_mode1.values[i] - expected[i];
                double diff_modes = result_mode0.values[i] - result_mode1.values[i];

                std::cout << std::setw(8) << i
                          << std::setw(14) << std::setprecision(6) << std::fixed << expected[i]
                          << std::setw(14) << result_mode0.values[i]
                          << std::setw(14) << result_mode1.values[i]
                          << std::setw(14) << diff_modes
                          << std::setw(14) << error_mode0
                          << std::setw(14) << error_mode1 << "\n";
            }
        }

        // Compute statistics for Mode 0
        double sum_abs_error_0 = 0.0;
        double max_abs_error_0 = 0.0;
        int valid_count = 0;
        int under_0_1_mode0 = 0;

        // Compute statistics for Mode 1
        double sum_abs_error_1 = 0.0;
        double max_abs_error_1 = 0.0;
        int under_0_1_mode1 = 0;

        // Compute mode differences
        double sum_abs_diff = 0.0;
        double max_abs_diff = 0.0;

        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(result_mode0.values[i])
                && std::isfinite(result_mode1.values[i])) {

                double abs_error_0 = std::fabs(result_mode0.values[i] - expected[i]);
                double abs_error_1 = std::fabs(result_mode1.values[i] - expected[i]);
                double abs_diff = std::fabs(result_mode0.values[i] - result_mode1.values[i]);

                sum_abs_error_0 += abs_error_0;
                sum_abs_error_1 += abs_error_1;
                sum_abs_diff += abs_diff;

                if (abs_error_0 > max_abs_error_0) max_abs_error_0 = abs_error_0;
                if (abs_error_1 > max_abs_error_1) max_abs_error_1 = abs_error_1;
                if (abs_diff > max_abs_diff) max_abs_diff = abs_diff;

                if (abs_error_0 < 0.1) under_0_1_mode0++;
                if (abs_error_1 < 0.1) under_0_1_mode1++;

                valid_count++;
            }
        }

        double mae_0 = valid_count > 0 ? sum_abs_error_0 / valid_count : 0.0;
        double mae_1 = valid_count > 0 ? sum_abs_error_1 / valid_count : 0.0;
        double mean_diff = valid_count > 0 ? sum_abs_diff / valid_count : 0.0;

        std::cout << "\n" << std::string(92, '=') << "\n";
        std::cout << "SUMMARY STATISTICS:\n";
        std::cout << std::string(92, '=') << "\n";
        std::cout << "Valid bars tested: " << valid_count << "\n\n";

        std::cout << "MODE 0 (TSSB Executable - no cube root division):\n";
        std::cout << "  MAE vs CSV: " << std::setprecision(4) << mae_0 << "\n";
        std::cout << "  Max Error: " << std::setprecision(4) << max_abs_error_0 << "\n";
        std::cout << "  Bars with error < 0.1: " << under_0_1_mode0
                  << " (" << (100.0 * under_0_1_mode0 / valid_count) << "%)\n";
        if (mae_0 < 0.01) {
            std::cout << "  Status: ✓✓✓ PERFECT MATCH WITH TSSB CSV!\n";
        } else if (mae_0 < 0.1) {
            std::cout << "  Status: ✓✓ EXCELLENT\n";
        } else if (mae_0 < 1.0) {
            std::cout << "  Status: ✓ GOOD\n";
        } else {
            std::cout << "  Status: ✗ HIGH ERROR\n";
        }

        std::cout << "\nMODE 1 (Book/Source Formula - with cube root division):\n";
        std::cout << "  MAE vs CSV: " << std::setprecision(4) << mae_1 << "\n";
        std::cout << "  Max Error: " << std::setprecision(4) << max_abs_error_1 << "\n";
        std::cout << "  Bars with error < 0.1: " << under_0_1_mode1
                  << " (" << (100.0 * under_0_1_mode1 / valid_count) << "%)\n";
        if (mae_1 < 0.01) {
            std::cout << "  Status: ✓✓✓ PERFECT MATCH WITH TSSB CSV!\n";
        } else if (mae_1 < 0.1) {
            std::cout << "  Status: ✓✓ EXCELLENT\n";
        } else if (mae_1 < 1.0) {
            std::cout << "  Status: ✓ GOOD\n";
        } else {
            std::cout << "  Status: ✗ HIGH ERROR - Expected (book formula != TSSB executable)\n";
        }

        std::cout << "\nDIFFERENCE BETWEEN MODES:\n";
        std::cout << "  Mean Absolute Difference: " << std::setprecision(4) << mean_diff << "\n";
        std::cout << "  Max Absolute Difference: " << std::setprecision(4) << max_abs_diff << "\n";
        double avg_pct = 100.0 * mean_diff / std::fabs(sum_abs_error_0 / valid_count + 1e-10);
        std::cout << "  Average difference as % of Mode 0 magnitude: "
                  << std::setprecision(1) << avg_pct << "%\n";

        std::cout << "\n";
    }

    std::cout << "\n====================================================================\n";
    std::cout << "INTERPRETATION:\n";
    std::cout << "====================================================================\n";
    std::cout << "Mode 0 (params[2]=0): Matches TSSB executable behavior\n";
    std::cout << "  - Use this for compatibility with TSSB CSV files\n";
    std::cout << "  - Use this for models trained on TSSB data\n";
    std::cout << "  - This is the DEFAULT mode\n\n";
    std::cout << "Mode 1 (params[2]=1): Implements book/source formula\n";
    std::cout << "  - Use this for theoretical correctness per book documentation\n";
    std::cout << "  - Values are dampened by cube root of multiplier (~1.7x smaller)\n";
    std::cout << "  - Does NOT match TSSB CSV output\n\n";
    std::cout << "Cube root divisor: ∛5 = 1.710, ∛4 = 1.587\n";
    std::cout << "Expected difference: ~40-60% in indicator magnitude\n";

    return 0;
}

#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    std::string ohlcv_file = "../../btc25_3.txt";
    std::string tssb_file = "../../BTC25_3 HM.CSV";

    if (argc >= 3) {
        ohlcv_file = argv[1];
        tssb_file = argv[2];
    }

    std::cout << "Loading OHLCV data from: " << ohlcv_file << "\n";
    std::cout << "Loading TSSB data from: " << tssb_file << "\n";

    auto ohlcv_bars = OHLCVParser::parse_file(ohlcv_file);
    auto tssb_bars = TSBBOutputParser::parse_file(tssb_file);

    std::cout << "Loaded " << ohlcv_bars.size() << " OHLCV bars and "
              << tssb_bars.size() << " TSSB bars\n\n";

    std::cout << "====================================================================\n";
    std::cout << "NEWLY IMPLEMENTED INDICATORS VALIDATION\n";
    std::cout << "====================================================================\n\n";

    // Convert to series
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    struct IndicatorTest {
        std::string name;
        std::string csv_col;
        SingleIndicatorId id;
        std::vector<double> params;
    };

    std::vector<IndicatorTest> tests = {
        {"MAX_CVR", "MAX_CVR", SingleIndicatorId::MaxChangeVarianceRatio, {10, 3, 20}},
        {"ATR_RATIO_S", "ATR_RATIO_S", SingleIndicatorId::AtrRatio, {10, 2.5}},
        {"ATR_RATIO_M", "ATR_RATIO_M", SingleIndicatorId::AtrRatio, {50, 5.0}},
        {"VOL_MAX_PS", "VOL_MAX_PS", SingleIndicatorId::MaxPriceVarianceRatio, {1, 20, 50}},
    };

    for (const auto& test : tests) {
        std::cout << "====================================================================\n";
        std::cout << test.name << " (params:";
        for (const auto& p : test.params) {
            std::cout << " " << p;
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
        std::cout << "  MAE: " << std::setprecision(6) << mae << "\n";
        std::cout << "  Max Error: " << std::setprecision(6) << max_abs_error << "\n";
        std::cout << "  Mean Relative Error: " << std::setprecision(2) << mean_rel_error << "%\n";
        std::cout << "  Bars with error < 1%: " << under_1_pct
                  << " (" << std::setprecision(1) << (100.0 * under_1_pct / valid_count) << "%)\n";
        std::cout << "  Bars with error < 5%: " << under_5_pct
                  << " (" << (100.0 * under_5_pct / valid_count) << "%)\n";
        std::cout << "  Bars with error < 10%: " << under_10_pct
                  << " (" << (100.0 * under_10_pct / valid_count) << "%)\n";

        if (mae < 0.001) {
            std::cout << "  Status: ✓✓✓ PERFECT MATCH!\n";
        } else if (mae < 0.01) {
            std::cout << "  Status: ✓✓ EXCELLENT\n";
        } else if (mae < 0.1) {
            std::cout << "  Status: ✓ GOOD\n";
        } else {
            std::cout << "  Status: ✗ NEEDS INVESTIGATION\n";
        }
        std::cout << "\n";
    }

    return 0;
}

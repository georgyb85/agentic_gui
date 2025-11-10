#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    if (ohlcv_bars.empty()) {
        std::cerr << "ERROR: " << OHLCVParser::get_last_error() << "\n";
        return 1;
    }

    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    if (tssb_bars.empty()) {
        std::cerr << "ERROR: " << TSBBOutputParser::get_last_error() << "\n";
        return 1;
    }

    std::cout << "====================================================================\n";
    std::cout << "ADX INDICATOR VALIDATION\n";
    std::cout << "====================================================================\n\n";
    std::cout << "OHLCV bars: " << ohlcv_bars.size() << "\n";
    std::cout << "CSV bars: " << tssb_bars.size() << "\n\n";

    // Convert to series
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Test configurations: {name, lookback}
    struct ADXTest {
        std::string csv_name;
        std::string our_name;
        int lookback;
    };

    std::vector<ADXTest> tests = {
        {"ADX_S", "ADX_S", 14},
        {"ADX_M", "ADX_M", 50},
        {"ADX_L", "ADX_L", 120}
    };

    for (const auto& test : tests) {
        // Get expected values
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_name);

        // Compute our values
        SingleIndicatorRequest req;
        req.id = SingleIndicatorId::Adx;
        req.name = test.our_name;
        req.params[0] = test.lookback;

        auto result = compute_single_indicator(series, req);

        if (!result.success) {
            std::cerr << "ERROR computing " << test.our_name << ": " << result.error_message << "\n";
            continue;
        }

        // Compare values
        double sum_abs_error = 0.0;
        double max_abs_error = 0.0;
        int valid_count = 0;
        int under_0_1 = 0;
        int under_1_0 = 0;

        // Find first valid bar in CSV
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::cout << "====================================================================\n";
        std::cout << test.csv_name << " (lookback=" << test.lookback << ")\n";
        std::cout << "====================================================================\n";
        std::cout << "First valid CSV bar: " << first_valid << " (date: "
                  << ohlcv_bars[first_valid].date << " " << ohlcv_bars[first_valid].time << ")\n\n";

        // Show first 10 comparisons
        std::cout << "First 10 bars comparison:\n";
        std::cout << std::setw(8) << "Bar"
                  << std::setw(12) << "Expected"
                  << std::setw(12) << "Computed"
                  << std::setw(12) << "Error\n";
        std::cout << std::string(44, '-') << "\n";

        for (int j = 0; j < 10 && first_valid + j < ohlcv_bars.size(); ++j) {
            size_t i = first_valid + j;
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
                double error = result.values[i] - expected[i];
                double abs_error = std::fabs(error);

                std::cout << std::setw(8) << i
                          << std::setw(12) << std::setprecision(6) << std::fixed << expected[i]
                          << std::setw(12) << result.values[i]
                          << std::setw(12) << error << "\n";
            }
        }

        // Compute statistics for all bars
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
                double abs_error = std::fabs(result.values[i] - expected[i]);
                sum_abs_error += abs_error;
                if (abs_error > max_abs_error) max_abs_error = abs_error;
                if (abs_error < 0.1) under_0_1++;
                if (abs_error < 1.0) under_1_0++;
                valid_count++;
            }
        }

        double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;

        std::cout << "\nSummary:\n";
        std::cout << "  Valid bars: " << valid_count << "\n";
        std::cout << "  MAE: " << std::setprecision(4) << mae << "\n";
        std::cout << "  Max Error: " << std::setprecision(4) << max_abs_error << "\n";
        std::cout << "  Bars with error < 0.1: " << under_0_1
                  << " (" << (100.0 * under_0_1 / valid_count) << "%)\n";
        std::cout << "  Bars with error < 1.0: " << under_1_0
                  << " (" << (100.0 * under_1_0 / valid_count) << "%)\n";

        if (mae < 0.01) {
            std::cout << "  Status: ✓✓✓ PERFECT MATCH!\n";
        } else if (mae < 0.1) {
            std::cout << "  Status: ✓✓ EXCELLENT\n";
        } else if (mae < 1.0) {
            std::cout << "  Status: ✓ GOOD\n";
        } else {
            std::cout << "  Status: ✗ HIGH ERROR\n";
        }
        std::cout << "\n";
    }

    return 0;
}

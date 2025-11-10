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
    std::cout << "ADX DUAL-METHOD VALIDATION\n";
    std::cout << "====================================================================\n\n";
    std::cout << "Testing both ADX computation methods:\n";
    std::cout << "  Method 0 (default): Simple Moving Average (matches TSSB CSV)\n";
    std::cout << "  Method 1: Wilder's Exponential Smoothing (from book)\n\n";

    // Convert to series
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Test ADX_S with lookback=14
    std::cout << "====================================================================\n";
    std::cout << "ADX_S (lookback=14)\n";
    std::cout << "====================================================================\n\n";

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");

    // Test Method 0 (SMA - default)
    {
        SingleIndicatorRequest req;
        req.id = SingleIndicatorId::Adx;
        req.name = "ADX_S_SMA";
        req.params[0] = 14;  // lookback
        req.params[1] = 0;   // method = SMA

        auto result = compute_single_indicator(series, req);

        if (!result.success) {
            std::cerr << "ERROR computing ADX with SMA method: " << result.error_message << "\n";
            return 1;
        }

        // Calculate MAE
        double mae = 0.0;
        double max_error = 0.0;
        int count = 0;

        for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                double error = std::abs(result.values[i] - expected[i]);
                mae += error;
                max_error = std::max(max_error, error);
                count++;
            }
        }
        mae /= count;

        std::cout << "Method 0 (SMA - default):\n";
        std::cout << "  MAE: " << std::fixed << std::setprecision(6) << mae << "\n";
        std::cout << "  Max Error: " << max_error << "\n";
        if (mae < 0.01) {
            std::cout << "  Status: ✓✓✓ PERFECT MATCH!\n";
        } else {
            std::cout << "  Status: ✗ HIGH ERROR\n";
        }

        std::cout << "\n  First 5 bars:\n";
        std::cout << "  " << std::setw(6) << "Bar"
                  << std::setw(12) << "CSV"
                  << std::setw(12) << "Computed"
                  << std::setw(12) << "Error" << "\n";
        std::cout << "  " << std::string(42, '-') << "\n";

        for (int j = 0; j < 5; ++j) {
            size_t i = 1078 + j;
            double error = result.values[i] - expected[i];
            std::cout << "  " << std::setw(6) << i
                      << std::fixed << std::setprecision(4)
                      << std::setw(12) << expected[i]
                      << std::setw(12) << result.values[i]
                      << std::setw(12) << error << "\n";
        }
        std::cout << "\n";
    }

    // Test Method 1 (Wilder's exponential smoothing)
    {
        SingleIndicatorRequest req;
        req.id = SingleIndicatorId::Adx;
        req.name = "ADX_S_Wilder";
        req.params[0] = 14;  // lookback
        req.params[1] = 1;   // method = Wilder's

        auto result = compute_single_indicator(series, req);

        if (!result.success) {
            std::cerr << "ERROR computing ADX with Wilder's method: " << result.error_message << "\n";
            return 1;
        }

        // Calculate MAE
        double mae = 0.0;
        double max_error = 0.0;
        int count = 0;

        for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                double error = std::abs(result.values[i] - expected[i]);
                mae += error;
                max_error = std::max(max_error, error);
                count++;
            }
        }
        mae /= count;

        std::cout << "Method 1 (Wilder's Exponential Smoothing):\n";
        std::cout << "  MAE: " << std::fixed << std::setprecision(6) << mae << "\n";
        std::cout << "  Max Error: " << max_error << "\n";
        std::cout << "  Status: Alternative method from book/COMP_VAR.CPP\n";

        std::cout << "\n  First 5 bars:\n";
        std::cout << "  " << std::setw(6) << "Bar"
                  << std::setw(12) << "CSV"
                  << std::setw(12) << "Computed"
                  << std::setw(12) << "Error" << "\n";
        std::cout << "  " << std::string(42, '-') << "\n";

        for (int j = 0; j < 5; ++j) {
            size_t i = 1078 + j;
            double error = result.values[i] - expected[i];
            std::cout << "  " << std::setw(6) << i
                      << std::fixed << std::setprecision(4)
                      << std::setw(12) << expected[i]
                      << std::setw(12) << result.values[i]
                      << std::setw(12) << error << "\n";
        }
        std::cout << "\n";
    }

    // Test default (no method parameter specified)
    {
        SingleIndicatorRequest req;
        req.id = SingleIndicatorId::Adx;
        req.name = "ADX_S_Default";
        req.params[0] = 14;  // lookback only, no method specified

        auto result = compute_single_indicator(series, req);

        if (!result.success) {
            std::cerr << "ERROR computing ADX with default method: " << result.error_message << "\n";
            return 1;
        }

        // Calculate MAE
        double mae = 0.0;
        int count = 0;

        for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                double error = std::abs(result.values[i] - expected[i]);
                mae += error;
                count++;
            }
        }
        mae /= count;

        std::cout << "Default (no method parameter):\n";
        std::cout << "  MAE: " << std::fixed << std::setprecision(6) << mae << "\n";
        if (mae < 0.01) {
            std::cout << "  Status: ✓ Correctly defaults to SMA method\n";
        } else {
            std::cout << "  Status: ✗ Default method incorrect\n";
        }
        std::cout << "\n";
    }

    std::cout << "====================================================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "====================================================================\n";
    std::cout << "Both ADX methods are available in the library:\n";
    std::cout << "  - Default (params[1]=0 or omitted): SMA method matches TSSB CSV\n";
    std::cout << "  - Wilder's (params[1]=1): Exponential smoothing from book\n";
    std::cout << "====================================================================\n";

    return 0;
}

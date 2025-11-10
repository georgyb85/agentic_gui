#include "IndicatorEngine.hpp"
#include "IndicatorId.hpp"
#include "IndicatorRequest.hpp"
#include "Series.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

// Test SCALING vs NORMALIZATION compression formulas
// (Functions now provided by MathUtils.hpp)

// Compute MA DIFF raw values
std::vector<double> compute_ma_diff_raw(
    const std::vector<OHLCVBar>& bars,
    int short_len,
    int long_len,
    int lag)
{
    const size_t n = bars.size();
    std::vector<double> result(n, 0.0);

    std::vector<double> close(n);
    std::vector<double> open(n);
    std::vector<double> high(n);
    std::vector<double> low(n);
    for (size_t i = 0; i < n; ++i) {
        close[i] = bars[i].close;
        open[i] = bars[i].open;
        high[i] = bars[i].high;
        low[i] = bars[i].low;
    }

    const int atr_length = long_len + lag;

    for (size_t i = atr_length; i < n; ++i) {
        // Short MA
        double short_ma = 0.0;
        for (int j = 0; j < short_len; ++j) {
            short_ma += close[i - j];
        }
        short_ma /= short_len;

        // Long MA (lagged)
        double long_ma = 0.0;
        for (int j = 0; j < long_len; ++j) {
            size_t idx = i - lag - j;
            long_ma += close[idx];
        }
        long_ma /= long_len;

        // Difference / ATR
        double diff = short_ma - long_ma;
        double atr_val = atr(false, {open.data(), n}, {high.data(), n},
                            {low.data(), n}, {close.data(), n}, i, atr_length);

        if (atr_val > 1e-10) {
            diff /= atr_val;
        }

        result[i] = diff;
    }

    return result;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc245.txt> <BTC245 HM.CSV>\n";
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "COMPRESSION FORMULA TEST: SCALING vs NORMALIZATION\n";
    std::cout << "================================================================================\n\n";

    // Load data
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

    std::cout << "Computing MA_DIFF_S raw values...\n";
    auto raw = compute_ma_diff_raw(ohlcv_bars, 10, 20, 10);

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");

    // Test different compression approaches
    std::cout << "\nTesting SCALING (no median) vs NORMALIZATION (with median):\n";
    std::cout << "============================================================\n\n";

    struct TestCase {
        std::string name;
        int lookback;
        double c;
        bool use_scaling;  // true = SCALING (no median), false = NORMALIZATION (with median)
    };

    std::vector<TestCase> tests = {
        {"NORMALIZATION c=0.48, lookback=250", 250, 0.48, false},
        {"NORMALIZATION c=0.49, lookback=250", 250, 0.49, false},
        {"NORMALIZATION c=0.50, lookback=250", 250, 0.50, false},
        {"NORMALIZATION c=0.51, lookback=250", 250, 0.51, false},
        {"NORMALIZATION c=0.52, lookback=250", 250, 0.52, false},
    };

    for (const auto& test : tests) {
        std::vector<double> compressed(raw.size(), 0.0);

        for (size_t i = test.lookback; i < raw.size(); ++i) {
            // Build historical window (EXCLUDING current bar)
            std::vector<double> history;
            for (int j = 1; j <= test.lookback; ++j) {
                size_t idx = i - j;
                if (std::isfinite(raw[idx]) && raw[idx] != 0.0) {
                    history.push_back(raw[idx]);
                }
            }

            if (history.size() < 10) {
                compressed[i] = raw[i];
                continue;
            }

            double median = compute_median(history);
            double iqr = compute_iqr(history);

            if (test.use_scaling) {
                compressed[i] = compress_scaling(raw[i], iqr, test.c);
            } else {
                compressed[i] = compress_to_range(raw[i], median, iqr, test.c);
            }
        }

        // Compare with TSSB (CSV starts at OHLCV bar 1078, NOT 1079!)
        size_t csv_start = 1078;
        std::cout << test.name << ":\n";
        std::cout << "  First 5 values:\n";

        for (int i = 0; i < 5; ++i) {
            size_t idx = csv_start + i;
            double diff = compressed[idx] - expected[idx];
            std::cout << "    Bar " << idx << ": TSSB=" << std::setw(10) << std::fixed << std::setprecision(4)
                      << expected[idx] << "  Ours=" << std::setw(10) << compressed[idx]
                      << "  Diff=" << std::setw(8) << diff << "\n";
        }

        // Compute correlation and MAE
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0, sum_yy = 0;
        double sum_abs_error = 0;
        double max_abs_error = 0;
        int count = 0;

        for (size_t i = csv_start; i < std::min(compressed.size(), expected.size()); ++i) {
            if (std::isfinite(compressed[i]) && std::isfinite(expected[i])) {
                double error = std::abs(compressed[i] - expected[i]);
                sum_abs_error += error;
                max_abs_error = std::max(max_abs_error, error);

                sum_x += compressed[i];
                sum_y += expected[i];
                sum_xy += compressed[i] * expected[i];
                sum_xx += compressed[i] * compressed[i];
                sum_yy += expected[i] * expected[i];
                ++count;
            }
        }

        if (count > 1) {
            double mean_x = sum_x / count;
            double mean_y = sum_y / count;
            double cov = sum_xy / count - mean_x * mean_y;
            double std_x = std::sqrt(sum_xx / count - mean_x * mean_x);
            double std_y = std::sqrt(sum_yy / count - mean_y * mean_y);
            double corr = cov / (std_x * std_y + 1e-10);
            double mae = sum_abs_error / count;

            std::cout << "  Correlation: " << std::setprecision(6) << corr << "\n";
            std::cout << "  MAE: " << std::setprecision(4) << mae << "\n";
            std::cout << "  Max error: " << max_abs_error << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "================================================================================\n";
    std::cout << "Done!\n";
    std::cout << "================================================================================\n";

    return 0;
}

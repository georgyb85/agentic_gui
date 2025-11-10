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
    std::cout << "DYNAMIC LOOKBACK TEST: Using ALL available history\n";
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

    // Test with different c values, using ALL available history at each bar
    std::cout << "\nTesting with ALL available history (NO CAP):\n";
    std::cout << "============================================\n\n";

    std::vector<double> c_values = {0.49, 0.50, 0.51};

    for (double c : c_values) {
        std::vector<double> compressed(raw.size(), 0.0);

        for (size_t i = 100; i < raw.size(); ++i) {  // Need at least 100 bars
            // Use ALL available history (NO CAP), exclude current bar
            int lookback = static_cast<int>(i);

            std::vector<double> history;
            history.reserve(lookback);
            // EXCLUDE current bar (no future leak)
            for (int j = 1; j <= lookback; ++j) {
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
            compressed[i] = compress_to_range(raw[i], median, iqr, c);
        }

        // Compare with TSSB (CSV starts at OHLCV bar 1078!)
        size_t csv_start = 1078;
        std::cout << "c=" << std::fixed << std::setprecision(2) << c << ":\n";
        std::cout << "  First 5 values:\n";

        for (int i = 0; i < 5; ++i) {
            size_t idx = csv_start + i;
            double diff = compressed[idx] - expected[idx];
            std::cout << "    Bar " << idx << ": TSSB=" << std::setw(10) << std::setprecision(4)
                      << expected[idx] << "  Ours=" << std::setw(10) << compressed[idx]
                      << "  Diff=" << std::setw(8) << diff << "\n";
        }

        // Compute MAE
        double sum_abs_error = 0;
        int count = 0;

        for (size_t i = csv_start; i < std::min(compressed.size(), expected.size()); ++i) {
            if (std::isfinite(compressed[i]) && std::isfinite(expected[i])) {
                sum_abs_error += std::abs(compressed[i] - expected[i]);
                ++count;
            }
        }

        double mae = count > 0 ? sum_abs_error / count : 0;
        std::cout << "  MAE: " << std::setprecision(4) << mae << "\n\n";
    }

    std::cout << "================================================================================\n";
    std::cout << "Done!\n";
    std::cout << "================================================================================\n";

    return 0;
}

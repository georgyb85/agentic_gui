#include "IndicatorEngine.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>

using namespace tssb;
using namespace tssb::validation;

std::vector<double> compute_ma_diff_raw(const std::vector<OHLCVBar>& bars, int short_len, int long_len, int lag) {
    const size_t n = bars.size();
    std::vector<double> result(n, 0.0);
    std::vector<double> close(n), open(n), high(n), low(n);
    for (size_t i = 0; i < n; ++i) {
        close[i] = bars[i].close;
        open[i] = bars[i].open;
        high[i] = bars[i].high;
        low[i] = bars[i].low;
    }
    const int atr_length = long_len + lag;
    for (size_t i = atr_length; i < n; ++i) {
        double short_ma = 0.0;
        for (int j = 0; j < short_len; ++j) short_ma += close[i - j];
        short_ma /= short_len;
        double long_ma = 0.0;
        for (int j = 0; j < long_len; ++j) long_ma += close[i - lag - j];
        long_ma /= long_len;
        double diff = short_ma - long_ma;
        double atr_val = atr(false, {open.data(), n}, {high.data(), n}, {low.data(), n}, {close.data(), n}, i, atr_length);
        if (atr_val > 1e-10) diff /= atr_val;
        result[i] = diff;
    }
    return result;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc245.txt> <BTC245 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    auto raw = compute_ma_diff_raw(ohlcv_bars, 10, 20, 10);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");

    std::cout << "==============================================================================\n";
    std::cout << "PERIODIC UPDATE TEST: Update median/IQR every N bars\n";
    std::cout << "==============================================================================\n\n";

    size_t csv_start = 1078;
    double c = 0.50;
    int lookback = 250;

    // Test different update periods
    std::vector<int> update_periods = {1, 6, 12, 24, 48};  // 1=every bar, 6=every 6 hours, 24=daily, etc.

    for (int update_period : update_periods) {
        std::vector<double> compressed(raw.size(), 0.0);

        double cached_median = 0.0;
        double cached_iqr = 0.0;
        size_t last_update_idx = 0;

        for (size_t csv_idx = 0; csv_idx < std::min(size_t(100), tssb_bars.size()); ++csv_idx) {
            size_t ohlcv_idx = csv_start + csv_idx;

            // Update median/IQR every N bars
            if (csv_idx % update_period == 0) {
                std::vector<double> history;
                for (int j = 1; j <= lookback; ++j) {
                    size_t idx = ohlcv_idx - j;
                    if (std::isfinite(raw[idx]) && raw[idx] != 0.0) {
                        history.push_back(raw[idx]);
                    }
                }

                if (history.size() >= 10) {
                    cached_median = compute_median(history);
                    cached_iqr = compute_iqr(history);
                    last_update_idx = csv_idx;
                }
            }

            // Use cached values for compression
            if (cached_iqr > 1e-10) {
                compressed[ohlcv_idx] = compress_to_range(raw[ohlcv_idx], cached_median, cached_iqr, c);
            } else {
                compressed[ohlcv_idx] = raw[ohlcv_idx];
            }
        }

        // Compute MAE for first 100 bars
        double sum_abs_error = 0;
        int count = 0;
        for (size_t csv_idx = 0; csv_idx < std::min(size_t(100), tssb_bars.size()); ++csv_idx) {
            size_t ohlcv_idx = csv_start + csv_idx;
            if (std::isfinite(compressed[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
                sum_abs_error += std::abs(compressed[ohlcv_idx] - expected[ohlcv_idx]);
                ++count;
            }
        }

        double mae = count > 0 ? sum_abs_error / count : 0;

        std::cout << "Update period = " << std::setw(2) << update_period << " bars";
        if (update_period == 24) std::cout << " (daily for hourly data)";
        else if (update_period == 1) std::cout << " (every bar)";
        std::cout << ":  MAE = " << std::fixed << std::setprecision(4) << mae << "\n";

        // Show first few values for the best result
        if (mae < 5.0) {
            std::cout << "  First 5 values:\n";
            for (int i = 0; i < 5; ++i) {
                size_t idx = csv_start + i;
                double error = compressed[idx] - expected[idx];
                std::cout << "    Bar " << idx << ": TSSB=" << std::setw(10) << expected[idx]
                          << ", Ours=" << std::setw(10) << compressed[idx]
                          << ", Err=" << std::setw(8) << error << "\n";
            }
        }
    }

    std::cout << "\n==============================================================================\n";

    return 0;
}

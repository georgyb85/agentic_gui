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

    std::cout << "Testing wider range of c values...\n\n";

    size_t csv_start = 1078;
    int lookback = 250;

    // Test c from 0.40 to 1.00 in steps of 0.05
    double best_c = 0.50;
    double best_mae = 1e10;

    for (double c = 0.40; c <= 1.00; c += 0.05) {
        std::vector<double> compressed(raw.size(), 0.0);

        for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
            size_t ohlcv_idx = csv_start + csv_idx;

            std::vector<double> history;
            for (int j = 1; j <= lookback; ++j) {
                size_t idx = ohlcv_idx - j;
                if (std::isfinite(raw[idx]) && raw[idx] != 0.0) {
                    history.push_back(raw[idx]);
                }
            }

            if (history.size() < 10) {
                compressed[ohlcv_idx] = raw[ohlcv_idx];
                continue;
            }

            double median = compute_median(history);
            double iqr = compute_iqr(history);
            compressed[ohlcv_idx] = compress_to_range(raw[ohlcv_idx], median, iqr, c);
        }

        // Compute MAE
        double sum_abs_error = 0;
        int count = 0;
        for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
            size_t ohlcv_idx = csv_start + csv_idx;
            if (std::isfinite(compressed[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
                sum_abs_error += std::abs(compressed[ohlcv_idx] - expected[ohlcv_idx]);
                ++count;
            }
        }

        double mae = count > 0 ? sum_abs_error / count : 0;

        std::cout << "c=" << std::fixed << std::setprecision(2) << c
                  << ":  MAE=" << std::setprecision(4) << mae;

        if (mae < best_mae) {
            best_mae = mae;
            best_c = c;
            std::cout << "  <-- BEST SO FAR";
        }

        std::cout << "\n";
    }

    std::cout << "\nBest: c=" << std::setprecision(2) << best_c
              << " with MAE=" << std::setprecision(4) << best_mae << "\n";

    return 0;
}

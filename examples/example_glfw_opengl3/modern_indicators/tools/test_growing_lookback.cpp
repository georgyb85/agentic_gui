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
    if (ohlcv_bars.empty()) {
        std::cerr << "ERROR: " << OHLCVParser::get_last_error() << "\n";
        return 1;
    }

    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    if (tssb_bars.empty()) {
        std::cerr << "ERROR: " << TSBBOutputParser::get_last_error() << "\n";
        return 1;
    }

    auto raw = compute_ma_diff_raw(ohlcv_bars, 10, 20, 10);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");

    std::cout << "Testing with GROWING lookback (starts from all available, caps at 250):\n\n";

    double c = 0.50;
    std::vector<double> compressed(raw.size(), 0.0);

    for (size_t i = 100; i < raw.size(); ++i) {
        // Use min(available_history, 250)
        int lookback = std::min(static_cast<int>(i), 250);

        std::vector<double> history;
        for (int j = 1; j <= lookback; ++j) {
            if (std::isfinite(raw[i - j]) && raw[i - j] != 0.0) {
                history.push_back(raw[i - j]);
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

    size_t csv_start = 1078;
    std::cout << "c=0.50, lookback=min(available,250):\n";
    std::cout << "  Lookback at bar 1078: " << std::min(1078, 250) << "\n";
    std::cout << "  Lookback at bar 1079: " << std::min(1079, 250) << "\n\n";

    std::cout << "  First 10 values:\n";
    for (int i = 0; i < 10; ++i) {
        size_t idx = csv_start + i;
        double err = compressed[idx] - expected[idx];
        std::cout << "    Bar " << idx << ": TSSB=" << std::fixed << std::setprecision(4)
                  << expected[idx] << " Ours=" << compressed[idx]
                  << " Diff=" << err << "\n";
    }

    double sum_abs_error = 0;
    int count = 0;
    for (size_t i = csv_start; i < std::min(compressed.size(), expected.size()); ++i) {
        if (std::isfinite(compressed[i]) && std::isfinite(expected[i])) {
            sum_abs_error += std::abs(compressed[i] - expected[i]);
            ++count;
        }
    }

    std::cout << "\n  MAE: " << (sum_abs_error / count) << "\n";

    return 0;
}

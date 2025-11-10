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

    std::cout << "================================================================================\n";
    std::cout << "COMPRESSION DIAGNOSTICS - Median/IQR Analysis\n";
    std::cout << "================================================================================\n\n";

    auto raw = compute_ma_diff_raw(ohlcv_bars, 10, 20, 10);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");

    double c = 0.50;
    int lookback = 250;
    size_t csv_start = 1078;

    // Analyze bars 1078-1082 in detail
    std::cout << "Analyzing first 5 CSV bars (OHLCV bars " << csv_start << "-" << (csv_start+4) << "):\n\n";

    for (int b = 0; b < 5; ++b) {
        size_t i = csv_start + b;

        // Build historical window (EXCLUDING current bar)
        std::vector<double> history;
        for (int j = 1; j <= lookback; ++j) {
            size_t idx = i - j;
            if (std::isfinite(raw[idx]) && raw[idx] != 0.0) {
                history.push_back(raw[idx]);
            }
        }

        if (history.size() < 10) {
            std::cout << "Bar " << i << ": insufficient history\n\n";
            continue;
        }

        double median = compute_median(history);
        double iqr = compute_iqr(history);
        double compressed = compress_to_range(raw[i], median, iqr, c);

        // Compute what the error would be
        double error = compressed - expected[i];

        std::cout << "Bar " << i << ":\n";
        std::cout << "  Raw value:      " << std::fixed << std::setprecision(6) << raw[i] << "\n";
        std::cout << "  History size:   " << history.size() << "\n";
        std::cout << "  Median:         " << median << "\n";
        std::cout << "  IQR:            " << iqr << "\n";
        std::cout << "  (raw-median):   " << (raw[i] - median) << "\n";
        std::cout << "  (raw-med)/IQR:  " << ((raw[i] - median) / iqr) << "\n";
        std::cout << "  Compressed:     " << compressed << "\n";
        std::cout << "  TSSB Expected:  " << expected[i] << "\n";
        std::cout << "  Error:          " << error << "\n";

        // Show min/max of history to understand range
        auto minmax = std::minmax_element(history.begin(), history.end());
        std::cout << "  History range:  [" << *minmax.first << ", " << *minmax.second << "]\n";

        // Show first and last 3 values in history to verify it's sliding
        std::cout << "  First 3 history values: ";
        for (int k = 0; k < std::min(3, (int)history.size()); ++k) {
            std::cout << history[k];
            if (k < std::min(3, (int)history.size()) - 1) std::cout << ", ";
        }
        std::cout << "\n";
        std::cout << "  Last 3 history values:  ";
        for (int k = std::max(0, (int)history.size() - 3); k < (int)history.size(); ++k) {
            std::cout << history[k];
            if (k < (int)history.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";

        // Show which OHLCV bars are included
        std::cout << "  History from OHLCV bars " << (i - lookback) << " to " << (i - 1) << "\n";
        std::cout << "\n";
    }

    return 0;
}

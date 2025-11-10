#include "IndicatorEngine.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>

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

    std::cout << "Checking for zeros and invalid values in raw MA_DIFF data...\n\n";

    // Check the range that would be used for bar 1078 lookback
    size_t csv_start = 1078;
    int lookback = 250;

    int zero_count = 0;
    int nan_count = 0;
    int inf_count = 0;

    for (int j = 1; j <= lookback; ++j) {
        size_t idx = csv_start - j;
        double val = raw[idx];

        if (!std::isfinite(val)) {
            if (std::isnan(val)) nan_count++;
            else inf_count++;
        } else if (val == 0.0) {
            zero_count++;
        }
    }

    std::cout << "In lookback window for bar " << csv_start << " (bars " << (csv_start - lookback) << " to " << (csv_start - 1) << "):\n";
    std::cout << "  Exact zeros: " << zero_count << "\n";
    std::cout << "  NaN values: " << nan_count << "\n";
    std::cout << "  Inf values: " << inf_count << "\n";
    std::cout << "  Valid non-zero: " << (lookback - zero_count - nan_count - inf_count) << "\n\n";

    // Now build history WITH zeros vs WITHOUT zeros
    std::cout << "Building history WITH zeros:\n";
    std::vector<double> history_with_zeros;
    for (int j = 1; j <= lookback; ++j) {
        size_t idx = csv_start - j;
        if (std::isfinite(raw[idx])) {  // Include zeros
            history_with_zeros.push_back(raw[idx]);
        }
    }

    std::cout << "Building history WITHOUT zeros:\n";
    std::vector<double> history_without_zeros;
    for (int j = 1; j <= lookback; ++j) {
        size_t idx = csv_start - j;
        if (std::isfinite(raw[idx]) && raw[idx] != 0.0) {  // Exclude zeros
            history_without_zeros.push_back(raw[idx]);
        }
    }

    std::cout << "  History size WITH zeros: " << history_with_zeros.size() << "\n";
    std::cout << "  History size WITHOUT zeros: " << history_without_zeros.size() << "\n\n";

    if (history_with_zeros.size() != history_without_zeros.size()) {
        double median_with = compute_median(history_with_zeros);
        double iqr_with = compute_iqr(history_with_zeros);
        double median_without = compute_median(history_without_zeros);
        double iqr_without = compute_iqr(history_without_zeros);

        std::cout << "DIFFERENCE DETECTED!\n";
        std::cout << "  With zeros:    median=" << median_with << ", IQR=" << iqr_with << "\n";
        std::cout << "  Without zeros: median=" << median_without << ", IQR=" << iqr_without << "\n\n";

        double compressed_with = compress_to_range(raw[csv_start], median_with, iqr_with, 0.50);
        double compressed_without = compress_to_range(raw[csv_start], median_without, iqr_without, 0.50);

        std::cout << "  Compressed WITH zeros:    " << compressed_with << "\n";
        std::cout << "  Compressed WITHOUT zeros: " << compressed_without << "\n";
    } else {
        std::cout << "No difference - no zeros found in lookback window.\n";
    }

    return 0;
}

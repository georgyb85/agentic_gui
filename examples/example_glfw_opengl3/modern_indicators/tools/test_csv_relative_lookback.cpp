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
    std::cout << "CSV-RELATIVE LOOKBACK TEST\n";
    std::cout << "Hypothesis: Lookback is based on CSV position, not absolute OHLCV position\n";
    std::cout << "================================================================================\n\n";

    auto raw = compute_ma_diff_raw(ohlcv_bars, 10, 20, 10);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");

    size_t csv_start = 1078;
    double c = 0.50;
    int max_lookback = 250;

    std::vector<double> compressed(raw.size(), 0.0);

    // For each CSV row, use lookback = min(csv_row_index, 250)
    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;

        // Lookback based on CSV position (how many CSV rows came before this one)
        int lookback = std::min((int)csv_idx, max_lookback);

        if (lookback < 10) {
            // Not enough history - use raw value
            compressed[ohlcv_idx] = raw[ohlcv_idx];
            continue;
        }

        // Build history from previous CSV rows (in OHLCV space)
        std::vector<double> history;
        for (int j = 1; j <= lookback; ++j) {
            size_t hist_ohlcv_idx = ohlcv_idx - j;
            if (std::isfinite(raw[hist_ohlcv_idx]) && raw[hist_ohlcv_idx] != 0.0) {
                history.push_back(raw[hist_ohlcv_idx]);
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

    // Show results for first 10 CSV rows
    std::cout << "First 10 CSV rows (c=0.50, lookback=min(csv_row, 250)):\n\n";

    for (int i = 0; i < 10; ++i) {
        size_t ohlcv_idx = csv_start + i;
        int lookback_used = std::min(i, max_lookback);
        double error = compressed[ohlcv_idx] - expected[ohlcv_idx];

        std::cout << "CSV row " << std::setw(2) << i
                  << " (OHLCV " << ohlcv_idx << "):"
                  << " lookback=" << std::setw(3) << lookback_used
                  << ", TSSB=" << std::fixed << std::setprecision(4) << std::setw(10) << expected[ohlcv_idx]
                  << ", Ours=" << std::setw(10) << compressed[ohlcv_idx]
                  << ", Err=" << std::setw(8) << error << "\n";
    }

    // Compute overall MAE
    double sum_abs_error = 0;
    int count = 0;
    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;
        if (std::isfinite(compressed[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
            sum_abs_error += std::abs(compressed[ohlcv_idx] - expected[ohlcv_idx]);
            ++count;
        }
    }

    std::cout << "\nOverall MAE: " << std::setprecision(4) << (sum_abs_error / count) << "\n";

    std::cout << "\n================================================================================\n";

    return 0;
}

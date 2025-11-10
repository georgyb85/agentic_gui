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

    std::cout << "=============================================================================\n";
    std::cout << "FULL HISTORY TEST: Using ALL available history for compression\n";
    std::cout << "=============================================================================\n\n";

    size_t csv_start = 1078;
    size_t atr_warmup = 30;  // MA_DIFF starts producing values from bar 30
    double c = 0.50;

    std::vector<double> compressed(raw.size(), 0.0);

    // For each CSV bar, use ALL available history from atr_warmup to current-1
    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;

        // Use ALL history from atr_warmup to current bar (excluding current)
        std::vector<double> history;
        for (size_t j = atr_warmup; j < ohlcv_idx; ++j) {
            if (std::isfinite(raw[j]) && raw[j] != 0.0) {
                history.push_back(raw[j]);
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
    std::cout << "First 10 CSV rows (c=0.50, full history):\n\n";

    for (int i = 0; i < 10; ++i) {
        size_t ohlcv_idx = csv_start + i;
        int history_size = ohlcv_idx - atr_warmup;
        double error = compressed[ohlcv_idx] - expected[ohlcv_idx];

        std::cout << "CSV row " << std::setw(2) << i
                  << " (OHLCV " << ohlcv_idx << "):"
                  << " history_size=" << std::setw(4) << history_size
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

    std::cout << "\n=============================================================================\n";

    return 0;
}

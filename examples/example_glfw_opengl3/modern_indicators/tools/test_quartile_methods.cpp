#include "IndicatorEngine.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>

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

// Alternative quartile calculation - linear interpolation (R-7 method)
std::pair<double, double> compute_quartiles_linear(const std::vector<double>& sorted_values) {
    size_t n = sorted_values.size();

    // Q1 position: (n+1) * 0.25
    // Q3 position: (n+1) * 0.75
    double q1_pos = (n + 1) * 0.25 - 1;  // -1 for 0-indexing
    double q3_pos = (n + 1) * 0.75 - 1;

    // Linear interpolation
    int q1_lower = std::max(0, (int)std::floor(q1_pos));
    int q1_upper = std::min((int)n - 1, (int)std::ceil(q1_pos));
    double q1_frac = q1_pos - std::floor(q1_pos);
    double q1 = sorted_values[q1_lower] * (1 - q1_frac) + sorted_values[q1_upper] * q1_frac;

    int q3_lower = std::max(0, (int)std::floor(q3_pos));
    int q3_upper = std::min((int)n - 1, (int)std::ceil(q3_pos));
    double q3_frac = q3_pos - std::floor(q3_pos);
    double q3 = sorted_values[q3_lower] * (1 - q3_frac) + sorted_values[q3_upper] * q3_frac;

    return {q1, q3};
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

    std::cout << "Testing different quartile calculation methods...\n\n";

    size_t csv_start = 1078;
    int lookback = 250;
    double c = 0.50;

    // Build history for bar 1078
    std::vector<double> history;
    for (int j = 1; j <= lookback; ++j) {
        size_t idx = csv_start - j;
        if (std::isfinite(raw[idx]) && raw[idx] != 0.0) {
            history.push_back(raw[idx]);
        }
    }

    std::sort(history.begin(), history.end());

    // Method 1: Our current method
    double median_current = compute_median(history);
    double iqr_current = compute_iqr(history);

    // Method 2: Linear interpolation (R-7)
    auto [q1_linear, q3_linear] = compute_quartiles_linear(history);
    double iqr_linear = q3_linear - q1_linear;

    // Method 3: What IQR would we NEED to get the correct answer?
    // Expected: -19.884167, Raw: -1.352113, Median: median_current, c: 0.50
    // -19.884167 = 100 * Φ(0.5 * (-1.352113 - median) / IQR_needed) - 50
    // 30.115833 = 100 * Φ(0.5 * (-1.352113 - median) / IQR_needed)
    // 0.301158 = Φ(0.5 * (-1.352113 - median) / IQR_needed)
    // Φ^(-1)(0.301158) = 0.5 * (-1.352113 - median) / IQR_needed
    double target_cdf = (expected[csv_start] + 50.0) / 100.0;
    double target_z = inverse_normal_cdf(target_cdf);
    double iqr_needed = c * (raw[csv_start] - median_current) / target_z;

    std::cout << "Bar 1078 analysis:\n";
    std::cout << "  Raw value: " << std::fixed << std::setprecision(6) << raw[csv_start] << "\n";
    std::cout << "  Expected compressed: " << expected[csv_start] << "\n\n";

    std::cout << "Method 1 (Current - n/4 method):\n";
    std::cout << "  Median: " << median_current << "\n";
    std::cout << "  IQR: " << iqr_current << "\n";
    double compressed_current = compress_to_range(raw[csv_start], median_current, iqr_current, c);
    std::cout << "  Compressed: " << compressed_current << "\n";
    std::cout << "  Error: " << (compressed_current - expected[csv_start]) << "\n\n";

    std::cout << "Method 2 (Linear interpolation - R-7):\n";
    std::cout << "  Median: " << median_current << " (same)\n";
    std::cout << "  Q1: " << q1_linear << "\n";
    std::cout << "  Q3: " << q3_linear << "\n";
    std::cout << "  IQR: " << iqr_linear << "\n";
    double compressed_linear = compress_to_range(raw[csv_start], median_current, iqr_linear, c);
    std::cout << "  Compressed: " << compressed_linear << "\n";
    std::cout << "  Error: " << (compressed_linear - expected[csv_start]) << "\n\n";

    std::cout << "Method 3 (Reverse-engineered):\n";
    std::cout << "  IQR needed: " << iqr_needed << "\n";
    std::cout << "  Difference from current: " << (iqr_needed - iqr_current) << "\n";
    std::cout << "  Difference from linear: " << (iqr_needed - iqr_linear) << "\n\n";

    // Test bar 1079 for comparison
    std::cout << "Bar 1079 for comparison:\n";
    history.clear();
    for (int j = 1; j <= lookback; ++j) {
        size_t idx = (csv_start + 1) - j;
        if (std::isfinite(raw[idx]) && raw[idx] != 0.0) {
            history.push_back(raw[idx]);
        }
    }
    std::sort(history.begin(), history.end());

    double median_1079 = compute_median(history);
    double iqr_1079 = compute_iqr(history);
    double compressed_1079 = compress_to_range(raw[csv_start + 1], median_1079, iqr_1079, c);
    std::cout << "  IQR: " << iqr_1079 << "\n";
    std::cout << "  Compressed: " << compressed_1079 << "\n";
    std::cout << "  Expected: " << expected[csv_start + 1] << "\n";
    std::cout << "  Error: " << (compressed_1079 - expected[csv_start + 1]) << "\n";

    return 0;
}

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

// Simple MA DIFFERENCE implementation for testing
// Formula: (short_MA - lagged_long_MA) / ATR, compressed to [-50, 50]
std::vector<double> compute_ma_diff_simple(
    const std::vector<OHLCVBar>& bars,
    int short_len,
    int long_len,
    int lag)
{
    const size_t n = bars.size();
    std::vector<double> result(n, 0.0);

    // Extract close prices
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
        // 1. Compute short MA (ending at current bar)
        double short_ma = 0.0;
        for (int j = 0; j < short_len; ++j) {
            short_ma += close[i - j];
        }
        short_ma /= short_len;

        // 2. Compute long MA (ending at lag bars ago)
        double long_ma = 0.0;
        for (int j = 0; j < long_len; ++j) {
            size_t idx = i - lag - j;
            long_ma += close[idx];
        }
        long_ma /= long_len;

        // 3. Difference
        double diff = short_ma - long_ma;

        // 4. Normalize by ATR
        double atr_val = atr(false, {open.data(), n}, {high.data(), n},
                            {low.data(), n}, {close.data(), n}, i, atr_length);

        if (atr_val > 1e-10) {
            diff /= atr_val;
        }

        // 5. Store RAW normalized value (before compression)
        result[i] = diff;
    }

    return result;
}

// Apply compression to raw values
std::vector<double> apply_compression(
    const std::vector<double>& raw_values,
    int lookback,
    double c)
{
    const size_t n = raw_values.size();
    std::vector<double> compressed(n, 0.0);

    for (size_t i = lookback; i < n; ++i) {
        // Build historical window
        std::vector<double> history;
        for (int j = 0; j < lookback; ++j) {
            size_t idx = i - j;
            if (std::isfinite(raw_values[idx]) && raw_values[idx] != 0.0) {
                history.push_back(raw_values[idx]);
            }
        }

        if (history.size() < 10) {
            compressed[i] = raw_values[i];
            continue;
        }

        // Compute median and IQR
        double median = compute_median(history);
        double iqr = compute_iqr(history);

        // Apply compression
        compressed[i] = compress_to_range(raw_values[i], median, iqr, c);
    }

    return compressed;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc245.txt> <BTC245 HM.CSV>\n";
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "MA DIFFERENCE SCALING TEST\n";
    std::cout << "================================================================================\n\n";

    // Load OHLCV data
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    if (ohlcv_bars.empty()) {
        std::cerr << "ERROR: " << OHLCVParser::get_last_error() << "\n";
        return 1;
    }
    std::cout << "Loaded " << ohlcv_bars.size() << " OHLCV bars\n";

    // Load TSSB output
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    if (tssb_bars.empty()) {
        std::cerr << "ERROR: " << TSBBOutputParser::get_last_error() << "\n";
        return 1;
    }
    std::cout << "Loaded " << tssb_bars.size() << " TSSB bars\n\n";

    // Test MA_DIFF_S: MA DIFFERENCE 10 20 10
    std::cout << "Computing MA_DIFF_S (short=10, long=20, lag=10)...\n";
    auto raw_s = compute_ma_diff_simple(ohlcv_bars, 10, 20, 10);

    // Try different compression parameters
    struct TestCase {
        int lookback;
        double c;
        std::string name;
    };

    std::vector<TestCase> tests = {
        {100, 0.25, "lookback=100, c=0.25"},
        {250, 0.25, "lookback=250, c=0.25"},
        {500, 0.5, "lookback=500, c=0.5"},
        {1000, 0.6, "lookback=1000, c=0.6"},
        {0, 0.0, "NO COMPRESSION (raw)"},
    };

    // Get TSSB expected values (aligned by date/time)
    auto expected_s = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");

    std::cout << "\nTesting different compression parameters:\n";
    std::cout << "==========================================\n";

    for (const auto& test : tests) {
        std::vector<double> computed;

        if (test.lookback == 0) {
            // No compression - use raw values
            computed = raw_s;
        } else {
            computed = apply_compression(raw_s, test.lookback, test.c);
        }

        // Compare with TSSB at CSV start (bar 1079)
        size_t csv_start = 1079;
        std::cout << "\n" << test.name << ":\n";
        std::cout << "  At CSV start (bar 1079):\n";
        std::cout << "    TSSB value:     " << std::fixed << std::setprecision(8) << expected_s[csv_start] << "\n";
        std::cout << "    Our value:      " << computed[csv_start] << "\n";
        std::cout << "    Raw value:      " << raw_s[csv_start] << "\n";
        std::cout << "    Difference:     " << (computed[csv_start] - expected_s[csv_start]) << "\n";

        // Show first few valid values
        std::cout << "  First 5 rows after CSV start:\n";
        for (int i = 0; i < 5; ++i) {
            size_t idx = csv_start + i;
            std::cout << "    Bar " << idx << ": TSSB=" << std::setw(12) << expected_s[idx]
                      << "  Ours=" << std::setw(12) << computed[idx]
                      << "  Diff=" << std::setw(10) << (computed[idx] - expected_s[idx]) << "\n";
        }

        // Compute correlation for valid overlapping region
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0, sum_yy = 0;
        int count = 0;

        for (size_t i = csv_start; i < std::min(computed.size(), expected_s.size()); ++i) {
            if (std::isfinite(computed[i]) && std::isfinite(expected_s[i])) {
                sum_x += computed[i];
                sum_y += expected_s[i];
                sum_xy += computed[i] * expected_s[i];
                sum_xx += computed[i] * computed[i];
                sum_yy += expected_s[i] * expected_s[i];
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

            std::cout << "  Correlation: " << std::setprecision(6) << corr << " (" << count << " valid bars)\n";
        }
    }

    // Export for manual analysis
    std::cout << "\n\nExporting values to MA_DIFF_ANALYSIS.csv...\n";
    std::ofstream out("MA_DIFF_ANALYSIS.csv");
    out << "Date,Time,TSSB_MA_DIFF_S,Raw,C100_025,C250_025,C500_05,C1000_06\n";

    auto c100 = apply_compression(raw_s, 100, 0.25);
    auto c250 = apply_compression(raw_s, 250, 0.25);
    auto c500 = apply_compression(raw_s, 500, 0.5);
    auto c1000 = apply_compression(raw_s, 1000, 0.6);

    for (size_t i = 1079; i < std::min(ohlcv_bars.size(), size_t(1179)); ++i) {
        out << ohlcv_bars[i].date << "," << ohlcv_bars[i].time << ","
            << expected_s[i] << "," << raw_s[i] << ","
            << c100[i] << "," << c250[i] << "," << c500[i] << "," << c1000[i] << "\n";
    }
    out.close();

    std::cout << "\n================================================================================\n";
    std::cout << "Done! Check MA_DIFF_ANALYSIS.csv for detailed comparison.\n";
    std::cout << "================================================================================\n";

    return 0;
}

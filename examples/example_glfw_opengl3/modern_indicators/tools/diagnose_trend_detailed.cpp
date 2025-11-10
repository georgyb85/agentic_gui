#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

void diagnose_bar(const std::vector<OHLCVBar>& bars, size_t test_bar, int lookback, int atr_length, double expected) {
    std::cout << "\n========================================\n";
    std::cout << "Diagnosing bar " << test_bar << "\n";
    std::cout << "Date/Time: " << bars[test_bar].date << " " << bars[test_bar].time << "\n";
    std::cout << "========================================\n";

    // Compute Legendre coefficients
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    // Build arrays
    std::vector<double> log_prices(lookback);
    double mean = 0.0;
    for (int k = 0; k < lookback; ++k) {
        size_t idx = test_bar - lookback + 1 + k;
        log_prices[k] = std::log(bars[idx].close);
        mean += log_prices[k];
    }
    mean /= lookback;

    // Compute dot product
    double dot_prod = 0.0;
    for (int k = 0; k < lookback; ++k) {
        dot_prod += log_prices[k] * c1[k];
    }

    // Compute ATR
    std::vector<double> open_vec(bars.size()), high_vec(bars.size()), low_vec(bars.size()), close_vec(bars.size());
    for (size_t i = 0; i < bars.size(); ++i) {
        open_vec[i] = bars[i].open;
        high_vec[i] = bars[i].high;
        low_vec[i] = bars[i].low;
        close_vec[i] = bars[i].close;
    }

    double atr_val = atr(true, {open_vec.data(), open_vec.size()}, {high_vec.data(), high_vec.size()},
                         {low_vec.data(), low_vec.size()}, {close_vec.data(), close_vec.size()},
                         test_bar, atr_length);

    // Compute denominator
    int k_factor = lookback - 1;
    if (lookback == 2) k_factor = 2;
    double denom = atr_val * k_factor;

    // Raw indicator
    double raw_indicator = dot_prod * 2.0 / (denom + 1.e-60);

    // Compute R-squared
    double yss = 0.0;
    double rsq_sum = 0.0;
    for (int k = 0; k < lookback; ++k) {
        double diff = log_prices[k] - mean;
        yss += diff * diff;
        double pred = dot_prod * c1[k];
        double error = diff - pred;
        rsq_sum += error * error;
    }
    double rsq = 1.0 - rsq_sum / (yss + 1.e-60);
    if (rsq < 0.0) rsq = 0.0;

    // After R-squared
    double after_rsq = raw_indicator * rsq;

    // Final compression
    double final_val = 100.0 * normal_cdf(after_rsq) - 50.0;

    std::cout << std::fixed << std::setprecision(8);
    std::cout << "Dot product: " << dot_prod << "\n";
    std::cout << "ATR (log): " << atr_val << "\n";
    std::cout << "k_factor: " << k_factor << "\n";
    std::cout << "Denominator: " << denom << "\n";
    std::cout << "Raw indicator: " << raw_indicator << "\n";
    std::cout << "R-squared: " << rsq << "\n";
    std::cout << "After R-sq: " << after_rsq << "\n";
    std::cout << "Final value: " << final_val << "\n";
    std::cout << "Expected: " << expected << "\n";
    std::cout << "Error: " << (final_val - expected) << "\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
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

    // Get expected values for TREND_S100
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");

    // Find first valid bar
    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            first_valid = i;
            break;
        }
    }

    std::cout << "First valid CSV bar: " << first_valid << "\n";

    // Diagnose bars 1078, 1079, 1080, 1085 (first, second, third, and one with high error)
    std::vector<size_t> test_bars = {1078, 1079, 1080, 1081, 1085};

    for (size_t bar : test_bars) {
        diagnose_bar(ohlcv_bars, bar, 10, 100, expected[bar]);
    }

    return 0;
}

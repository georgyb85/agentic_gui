#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    if (ohlcv_bars.empty()) {
        std::cerr << "ERROR: " << OHLCVParser::get_last_error() << "\n";
        return 1;
    }

    // Test bar 1078
    size_t test_bar = 1078;
    int lookback = 10;

    std::cout << "Testing R-squared calculation at bar " << test_bar << "\n\n";

    // Compute Legendre coefficients
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    // Build log prices
    std::vector<double> log_prices(lookback);
    double mean = 0.0;
    for (int k = 0; k < lookback; ++k) {
        size_t idx = test_bar - lookback + 1 + k;
        log_prices[k] = std::log(ohlcv_bars[idx].close);
        mean += log_prices[k];
    }
    mean /= lookback;

    // Compute dot product
    double dot_prod = 0.0;
    for (int k = 0; k < lookback; ++k) {
        dot_prod += log_prices[k] * c1[k];
    }

    std::cout << "Log prices:\n";
    for (int k = 0; k < lookback; ++k) {
        size_t idx = test_bar - lookback + 1 + k;
        double pred = dot_prod * c1[k];  // Predicted Y offset
        double actual = log_prices[k] - mean;  // Actual Y offset
        std::cout << "  Bar " << idx << ": close=" << ohlcv_bars[idx].close
                  << ", log=" << log_prices[k]
                  << ", offset=" << actual
                  << ", pred_offset=" << pred
                  << ", error=" << (actual - pred) << "\n";
    }

    std::cout << "\nMean: " << mean << "\n";
    std::cout << "Dot product: " << dot_prod << "\n";

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

    std::cout << "\nYSS (total variance): " << yss << "\n";
    std::cout << "RSQ_SUM (error variance): " << rsq_sum << "\n";
    std::cout << "R-squared: " << rsq << "\n";

    // Now compute what TSSB should give
    std::vector<double> open_vec(ohlcv_bars.size()), high_vec(ohlcv_bars.size()),
                        low_vec(ohlcv_bars.size()), close_vec(ohlcv_bars.size());
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        open_vec[i] = ohlcv_bars[i].open;
        high_vec[i] = ohlcv_bars[i].high;
        low_vec[i] = ohlcv_bars[i].low;
        close_vec[i] = ohlcv_bars[i].close;
    }

    double atr_val = atr(true, {open_vec.data(), open_vec.size()}, {high_vec.data(), high_vec.size()},
                         {low_vec.data(), low_vec.size()}, {close_vec.data(), close_vec.size()},
                         test_bar, 100);

    int k_factor = lookback - 1;
    double denom = atr_val * k_factor;
    double raw_indicator = dot_prod * 2.0 / (denom + 1.e-60);

    std::cout << "\nATR: " << atr_val << "\n";
    std::cout << "k_factor: " << k_factor << "\n";
    std::cout << "Denominator: " << denom << "\n";
    std::cout << "Raw indicator: " << raw_indicator << "\n";

    // Different scenarios
    std::cout << "\nScenarios:\n";
    std::cout << "1. TSSB formula (R² then compress c=1): "
              << (100.0 * normal_cdf(raw_indicator * rsq) - 50.0) << "\n";
    std::cout << "2. No R² degradation (c=1): "
              << (100.0 * normal_cdf(raw_indicator) - 50.0) << "\n";
    std::cout << "3. No R² degradation (c=2): "
              << (100.0 * normal_cdf(2.0 * raw_indicator) - 50.0) << "\n";
    std::cout << "4. No R² degradation (c=3): "
              << (100.0 * normal_cdf(3.0 * raw_indicator) - 50.0) << "\n";
    std::cout << "\nExpected from CSV: -4.584\n";

    return 0;
}

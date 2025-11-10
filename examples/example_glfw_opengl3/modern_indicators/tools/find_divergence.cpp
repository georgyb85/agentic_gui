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

    // Test bar 1078 (this one had huge error)
    size_t test_bar = 1078;
    int lookback = 10;
    int atr_length = 100;

    std::cout << std::fixed << std::setprecision(12);
    std::cout << "=============================================================\n";
    std::cout << "DETAILED DIAGNOSIS - Bar " << test_bar << "\n";
    std::cout << "Date/Time: " << ohlcv_bars[test_bar].date << " " << ohlcv_bars[test_bar].time << "\n";
    std::cout << "=============================================================\n\n";

    // Get expected value
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");
    std::cout << "Expected from CSV: " << expected[test_bar] << "\n\n";

    // Compute Legendre coefficients
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    std::cout << "Legendre c1 coefficients (lookback=" << lookback << "):\n";
    for (int i = 0; i < lookback; ++i) {
        std::cout << "  c1[" << i << "] = " << c1[i] << "\n";
    }
    std::cout << "\n";

    // Print prices in window
    std::cout << "Prices in window [" << (test_bar - lookback + 1) << " to " << test_bar << "]:\n";
    std::cout << "  Bar | Close | Log(Close)\n";
    std::vector<double> log_prices(lookback);
    double mean = 0.0;
    for (int k = 0; k < lookback; ++k) {
        size_t idx = test_bar - lookback + 1 + k;
        log_prices[k] = std::log(ohlcv_bars[idx].close);
        mean += log_prices[k];
        std::cout << "  " << idx << " | " << ohlcv_bars[idx].close << " | " << log_prices[k] << "\n";
    }
    mean /= lookback;
    std::cout << "\nMean log price: " << mean << "\n\n";

    // Compute dot product
    double dot_prod = 0.0;
    std::cout << "Dot product computation:\n";
    for (int k = 0; k < lookback; ++k) {
        double term = log_prices[k] * c1[k];
        dot_prod += term;
        std::cout << "  log_price[" << k << "] * c1[" << k << "] = " << log_prices[k] << " * " << c1[k] << " = " << term << "\n";
    }
    std::cout << "Dot product total: " << dot_prod << "\n\n";

    // Compute ATR
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
                         test_bar, atr_length);

    std::cout << "ATR computation (use_log=true, length=" << atr_length << "):\n";
    std::cout << "  ATR value: " << atr_val << "\n\n";

    // Compute k_factor and denominator
    int k_factor = (lookback == 2) ? 2 : (lookback - 1);
    double denom = atr_val * k_factor;
    std::cout << "k_factor: " << k_factor << "\n";
    std::cout << "Denominator (ATR * k_factor): " << denom << "\n\n";

    // Raw indicator
    double raw_indicator = dot_prod * 2.0 / (denom + 1.e-60);
    std::cout << "Raw indicator (dot_prod * 2.0 / denom): " << raw_indicator << "\n\n";

    // Compute R-squared
    double yss = 0.0;
    double rsq_sum = 0.0;
    std::cout << "R-squared computation:\n";
    for (int k = 0; k < lookback; ++k) {
        double diff = log_prices[k] - mean;
        yss += diff * diff;
        double pred = dot_prod * c1[k];
        double error = diff - pred;
        rsq_sum += error * error;
        if (k < 3) {  // Print first 3 for brevity
            std::cout << "  k=" << k << ": diff=" << diff << ", pred=" << pred << ", error=" << error << "\n";
        }
    }
    double rsq = 1.0 - rsq_sum / (yss + 1.e-60);
    if (rsq < 0.0) rsq = 0.0;

    std::cout << "YSS (total variance): " << yss << "\n";
    std::cout << "RSQ_SUM (error variance): " << rsq_sum << "\n";
    std::cout << "R-squared: " << rsq << "\n\n";

    // After R-squared degradation
    double after_rsq = raw_indicator * rsq;
    std::cout << "After R-squared degradation (raw * rsq): " << after_rsq << "\n\n";

    // Final compression
    double final_val = 100.0 * normal_cdf(after_rsq) - 50.0;
    std::cout << "Final value (100 * Φ(after_rsq) - 50): " << final_val << "\n\n";

    std::cout << "=============================================================\n";
    std::cout << "COMPARISON:\n";
    std::cout << "  Our value: " << final_val << "\n";
    std::cout << "  CSV value: " << expected[test_bar] << "\n";
    std::cout << "  Error: " << (final_val - expected[test_bar]) << "\n";
    std::cout << "=============================================================\n";

    return 0;
}

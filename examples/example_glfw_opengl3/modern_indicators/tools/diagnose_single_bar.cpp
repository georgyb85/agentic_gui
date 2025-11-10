#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    // Test bar 1078 (first valid bar in CSV)
    size_t test_bar = 1078;
    int lookback = 10;
    int atr_length = 100;

    std::cout << std::setprecision(15);
    std::cout << "===== TREND_S100 SINGLE BAR DIAGNOSTIC =====\n";
    std::cout << "Test bar: " << test_bar << "\n";
    std::cout << "lookback: " << lookback << ", atr_length: " << atr_length << "\n\n";

    // 1. Compute Legendre coefficients
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    std::cout << "Legendre c1 coefficients:\n";
    for (int i = 0; i < lookback; ++i) {
        std::cout << "  c1[" << i << "] = " << c1[i] << "\n";
    }

    // 2. Compute log prices in window
    std::cout << "\nLog prices in window:\n";
    std::vector<double> log_prices;
    for (int k = 0; k < lookback; ++k) {
        const int idx = test_bar - lookback + 1 + k;
        const double price = ohlcv_bars[idx].close;
        const double log_price = std::log(price);
        log_prices.push_back(log_price);
        std::cout << "  bar[" << idx << "] close=" << price << ", log=" << log_price << "\n";
    }

    // 3. Compute dot product and mean
    double dot_prod = 0.0;
    double mean = 0.0;
    for (int k = 0; k < lookback; ++k) {
        mean += log_prices[k];
        dot_prod += log_prices[k] * c1[k];
    }
    mean /= lookback;

    std::cout << "\nDot product: " << dot_prod << "\n";
    std::cout << "Mean: " << mean << "\n";

    // 4. Compute ATR
    SingleMarketSeries series;
    series.open.resize(ohlcv_bars.size());
    series.high.resize(ohlcv_bars.size());
    series.low.resize(ohlcv_bars.size());
    series.close.resize(ohlcv_bars.size());
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        series.open[i] = ohlcv_bars[i].open;
        series.high[i] = ohlcv_bars[i].high;
        series.low[i] = ohlcv_bars[i].low;
        series.close[i] = ohlcv_bars[i].close;
    }

    std::span<const double> open_span(series.open.data(), series.open.size());
    std::span<const double> high_span(series.high.data(), series.high.size());
    std::span<const double> low_span(series.low.data(), series.low.size());
    std::span<const double> close_span(series.close.data(), series.close.size());

    const double atr_val = atr(true, open_span, high_span, low_span, close_span, test_bar, atr_length);
    std::cout << "ATR (use_log=true, length=" << atr_length << "): " << atr_val << "\n";

    // 5. Compute denominator
    int k_factor = lookback - 1;
    if (lookback == 2) {
        k_factor = 2;
    }
    const double denom = atr_val * k_factor;
    std::cout << "k_factor: " << k_factor << "\n";
    std::cout << "Denominator (ATR * k_factor): " << denom << "\n";

    // 6. Compute raw indicator (before R²)
    const double indicator_before_rsq = dot_prod * 2.0 / (denom + 1.e-60);
    std::cout << "\nIndicator before R²: " << indicator_before_rsq << "\n";

    // 7. Compute R²
    double yss = 0.0;
    double rsq_sum = 0.0;
    for (int k = 0; k < lookback; ++k) {
        const double diff = log_prices[k] - mean;
        yss += diff * diff;
        const double pred = dot_prod * c1[k];
        const double error = diff - pred;
        rsq_sum += error * error;
    }
    double rsq = 1.0 - rsq_sum / (yss + 1.e-60);
    if (rsq < 0.0) {
        rsq = 0.0;
    }

    std::cout << "yss: " << yss << "\n";
    std::cout << "rsq_sum: " << rsq_sum << "\n";
    std::cout << "R²: " << rsq << "\n";

    // 8. Apply R² degradation
    const double indicator_after_rsq = indicator_before_rsq * rsq;
    std::cout << "\nIndicator after R² (no compression): " << indicator_after_rsq << "\n";

    // 9. Apply compression with different c values
    std::cout << "\nWith different compression constants:\n";
    std::cout << "  c=1.0: " << (100.0 * normal_cdf(1.0 * indicator_after_rsq) - 50.0) << "\n";
    std::cout << "  c=2.0: " << (100.0 * normal_cdf(2.0 * indicator_after_rsq) - 50.0) << "\n";
    std::cout << "  c=3.0: " << (100.0 * normal_cdf(3.0 * indicator_after_rsq) - 50.0) << "\n";
    std::cout << "  c=4.0: " << (100.0 * normal_cdf(4.0 * indicator_after_rsq) - 50.0) << "\n";

    // 10. Compare with TSSB
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");
    std::cout << "\nTSSB value: " << expected[test_bar] << "\n";

    return 0;
}

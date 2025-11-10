#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

using namespace tssb;
using namespace tssb::validation;

// Manually compute TREND with detailed logging
void diagnose_trend(const std::vector<OHLCVBar>& bars, int lookback, int atr_length, size_t test_bar) {
    std::cout << "\n=== DIAGNOSING TREND at bar " << test_bar << " ===\n";
    std::cout << "Lookback: " << lookback << ", ATR length: " << atr_length << "\n\n";

    // Compute Legendre coefficients
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    std::cout << "First 5 Legendre c1 coefficients:\n";
    for (int i = 0; i < std::min(5, lookback); ++i) {
        std::cout << "  c1[" << i << "] = " << std::setprecision(8) << c1[i] << "\n";
    }

    // Compute log prices in window
    std::vector<double> log_prices(lookback);
    double mean = 0.0;
    std::cout << "\nLog prices in window:\n";
    for (int k = 0; k < lookback; ++k) {
        size_t idx = test_bar - lookback + 1 + k;
        log_prices[k] = std::log(bars[idx].close);
        mean += log_prices[k];
        if (k < 5) {
            std::cout << "  Bar " << idx << ": close=" << bars[idx].close
                      << ", log=" << std::setprecision(8) << log_prices[k] << "\n";
        }
    }
    mean /= lookback;
    std::cout << "Mean log price: " << mean << "\n";

    // Compute dot product
    double dot_prod = 0.0;
    for (int k = 0; k < lookback; ++k) {
        dot_prod += log_prices[k] * c1[k];
    }
    std::cout << "\nDot product (regression coefficient): " << std::setprecision(10) << dot_prod << "\n";

    // Compute ATR
    std::span<const double> open_span(reinterpret_cast<const double*>(&bars[0].open), bars.size());
    std::span<const double> high_span(reinterpret_cast<const double*>(&bars[0].high), bars.size());
    std::span<const double> low_span(reinterpret_cast<const double*>(&bars[0].low), bars.size());
    std::span<const double> close_span(reinterpret_cast<const double*>(&bars[0].close), bars.size());

    // Need to create contiguous arrays for ATR
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
    std::cout << "ATR (log=true): " << atr_val << "\n";

    // Compute denominator
    int k_factor = lookback - 1;
    if (lookback == 2) k_factor = 2;
    double denom = atr_val * k_factor;
    std::cout << "k_factor: " << k_factor << "\n";
    std::cout << "Denominator (ATR * k_factor): " << denom << "\n";

    // Raw indicator
    double indicator = dot_prod * 2.0 / (denom + 1.e-60);
    std::cout << "\nRaw indicator (dot_prod * 2 / denom): " << indicator << "\n";

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
    std::cout << "YSS: " << yss << "\n";
    std::cout << "RSQ sum: " << rsq_sum << "\n";
    std::cout << "R-squared: " << rsq << "\n";

    // After R-squared
    double after_rsq = indicator * rsq;
    std::cout << "\nAfter R-squared degradation: " << after_rsq << "\n";

    // Final compression
    double final_val = 100.0 * normal_cdf(after_rsq) - 50.0;
    std::cout << "Final value (100 * CDF - 50): " << final_val << "\n";
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

    size_t csv_start = 1078;
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");

    // Test at first valid CSV bar
    size_t test_bar = csv_start;

    std::cout << "TSSB value at bar " << test_bar << ": " << expected[test_bar] << "\n";

    diagnose_trend(ohlcv_bars, 10, 100, test_bar);

    // Also test with our implementation
    SingleMarketSeries series;
    series.open.resize(ohlcv_bars.size());
    series.high.resize(ohlcv_bars.size());
    series.low.resize(ohlcv_bars.size());
    series.close.resize(ohlcv_bars.size());
    series.volume.resize(ohlcv_bars.size());

    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        series.open[i] = ohlcv_bars[i].open;
        series.high[i] = ohlcv_bars[i].high;
        series.low[i] = ohlcv_bars[i].low;
        series.close[i] = ohlcv_bars[i].close;
        series.volume[i] = ohlcv_bars[i].volume;
    }

    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::LinearTrend;
    req.name = "TREND_S100";
    req.params[0] = 10;
    req.params[1] = 100;

    auto result = tssb::compute_single_indicator(series, req);
    std::cout << "\nOur implementation value at bar " << test_bar << ": " << result.values[test_bar] << "\n";
    std::cout << "Difference: " << (result.values[test_bar] - expected[test_bar]) << "\n";

    return 0;
}

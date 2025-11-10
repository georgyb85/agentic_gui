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

struct SeriesSpans {
    std::span<const double> open;
    std::span<const double> high;
    std::span<const double> low;
    std::span<const double> close;
    std::span<const double> volume;
};

// Modified version of compute_polynomial_trend that accepts custom c value
IndicatorResult compute_trend_custom_c(const SingleMarketSeries& series, int lookback, int atr_length, double c_value) {
    IndicatorResult result;
    result.values.resize(series.close.size(), 0.0);

    const size_t n = series.close.size();
    const int front_bad = std::max(lookback - 1, atr_length);

    SeriesSpans spans;
    spans.open = std::span<const double>(series.open.data(), series.open.size());
    spans.high = std::span<const double>(series.high.data(), series.high.size());
    spans.low = std::span<const double>(series.low.data(), series.low.size());
    spans.close = std::span<const double>(series.close.data(), series.close.size());
    spans.volume = std::span<const double>(series.volume.data(), series.volume.size());

    // Compute Legendre coefficients
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);
    const double* coefs = c1.data();  // Linear trend

    for (size_t icase = front_bad; icase < n; ++icase) {
        double dot_prod = 0.0;
        double mean = 0.0;
        for (int k = 0; k < lookback; ++k) {
            const int idx = icase - lookback + 1 + k;
            const double price = std::log(spans.close[idx]);
            mean += price;
            dot_prod += price * coefs[k];
        }
        mean /= lookback;

        int k_factor = lookback - 1;
        if (lookback == 2) {
            k_factor = 2;
        }
        const double denom = atr(true, spans.open, spans.high, spans.low, spans.close, icase, atr_length) * k_factor;
        double indicator = dot_prod * 2.0 / (denom + 1.e-60);

        // Compute R-squared
        double yss = 0.0;
        double rsq_sum = 0.0;
        for (int k = 0; k < lookback; ++k) {
            const int idx = icase - lookback + 1 + k;
            const double price = std::log(spans.close[idx]);
            const double diff = price - mean;
            yss += diff * diff;
            const double pred = dot_prod * coefs[k];
            const double error = diff - pred;
            rsq_sum += error * error;
        }
        double rsq = 1.0 - rsq_sum / (yss + 1.e-60);
        if (rsq < 0.0) {
            rsq = 0.0;
        }

        indicator *= rsq;
        result.values[icase] = 100.0 * normal_cdf(c_value * indicator) - 50.0;  // CUSTOM C VALUE
    }

    return result;
}

void test_c_value(const SingleMarketSeries& series, const std::vector<OHLCVBar>& ohlcv_bars,
                  const std::vector<TSBBIndicatorBar>& tssb_bars, const std::string& name,
                  int lookback, int atr_length, double c_value) {
    auto result = compute_trend_custom_c(series, lookback, atr_length, c_value);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);

    size_t csv_start = 1078;
    double sum_abs_error = 0;
    int count = 0;
    for (size_t i = 0; i < 100; ++i) {
        size_t idx = csv_start + i;
        if (std::isfinite(result.values[idx]) && std::isfinite(expected[idx])) {
            sum_abs_error += std::abs(result.values[idx] - expected[idx]);
            count++;
        }
    }
    double mae = count > 0 ? sum_abs_error / count : 999;

    std::cout << "  c=" << std::setw(5) << std::setprecision(2) << c_value
              << ": MAE=" << std::setprecision(3) << mae << "\n";
}

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

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

    std::cout << "Testing different c values for TREND indicators:\n\n";

    std::cout << "TREND_S100 (lookback=10, atr=100):\n";
    for (double c = 1.0; c <= 10.0; c += 1.0) {
        test_c_value(series, ohlcv_bars, tssb_bars, "TREND_S100", 10, 100, c);
    }

    std::cout << "\nTREND_M100 (lookback=50, atr=100):\n";
    for (double c = 1.0; c <= 10.0; c += 1.0) {
        test_c_value(series, ohlcv_bars, tssb_bars, "TREND_M100", 50, 100, c);
    }

    std::cout << "\nTREND_L100 (lookback=120, atr=100):\n";
    for (double c = 1.0; c <= 10.0; c += 1.0) {
        test_c_value(series, ohlcv_bars, tssb_bars, "TREND_L100", 120, 100, c);
    }

    return 0;
}

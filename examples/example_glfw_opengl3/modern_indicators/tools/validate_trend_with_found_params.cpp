#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <string>

using namespace tssb;
using namespace tssb::validation;

struct TrendParams {
    std::string name;
    int lookback;
    int atr_length;
    double c_mult;
};

IndicatorResult compute_trend_with_params(const SingleMarketSeries& series,
                                          int lookback, int atr_length, double c_mult) {
    IndicatorResult result;
    result.values.resize(series.close.size(), 0.0);

    std::span<const double> open_span(series.open.data(), series.open.size());
    std::span<const double> high_span(series.high.data(), series.high.size());
    std::span<const double> low_span(series.low.data(), series.low.size());
    std::span<const double> close_span(series.close.data(), series.close.size());

    const int front_bad = std::max(lookback - 1, atr_length);
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    for (size_t icase = front_bad; icase < series.close.size(); ++icase) {
        double dot_prod = 0.0, mean = 0.0;
        for (int k = 0; k < lookback; ++k) {
            int idx = icase - lookback + 1 + k;
            double price = std::log(close_span[idx]);
            mean += price;
            dot_prod += price * c1[k];
        }
        mean /= lookback;

        int k_factor = (lookback == 2) ? 2 : (lookback - 1);
        double atr_val = atr(true, open_span, high_span, low_span, close_span, icase, atr_length);
        double indicator = dot_prod * 2.0 / (atr_val * k_factor + 1.e-60);

        double yss = 0.0, rsq_sum = 0.0;
        for (int k = 0; k < lookback; ++k) {
            int idx = icase - lookback + 1 + k;
            double price = std::log(close_span[idx]);
            double diff = price - mean;
            yss += diff * diff;
            double pred = dot_prod * c1[k];
            rsq_sum += (diff - pred) * (diff - pred);
        }
        double rsq = std::max(0.0, 1.0 - rsq_sum / (yss + 1.e-60));
        indicator *= rsq;
        result.values[icase] = 100.0 * normal_cdf(c_mult * indicator) - 50.0;
    }

    return result;
}

void test_params(const SingleMarketSeries& series,
                const std::vector<OHLCVBar>& ohlcv_bars,
                const std::vector<TSBBIndicatorBar>& tssb_bars,
                const TrendParams& params) {

    auto result = compute_trend_with_params(series, params.lookback, params.atr_length, params.c_mult);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, params.name);

    size_t csv_start = 1078;
    double sum_abs_error = 0;
    int count = 0;
    double max_error = 0;

    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;
        if (std::isfinite(result.values[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
            double error = std::abs(result.values[ohlcv_idx] - expected[ohlcv_idx]);
            sum_abs_error += error;
            max_error = std::max(max_error, error);
            ++count;
        }
    }

    double mae = count > 0 ? sum_abs_error / count : 999;

    std::cout << std::setw(15) << params.name
              << ": lookback=" << std::setw(3) << params.lookback
              << ", atr=" << std::setw(3) << params.atr_length
              << ", c=" << std::setprecision(2) << std::setw(4) << params.c_mult
              << " -> MAE=" << std::setprecision(6) << std::setw(10) << mae
              << ", Max=" << std::setw(10) << max_error;

    if (mae < 0.1) {
        std::cout << " ✓ PERFECT\n";
    } else if (mae < 1.0) {
        std::cout << " ✓ EXCELLENT\n";
    } else if (mae < 5.0) {
        std::cout << " ✓ GOOD\n";
    } else {
        std::cout << " ✗ NEEDS WORK\n";
    }
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

    std::cout << "Testing TREND indicators with found parameters on ALL data:\n";
    std::cout << "============================================================\n\n";

    // Test with found parameters
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_S100", 10, 75, 4.0});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_M100", 55, 50, 8.0});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_L100", 125, 75, 1.5});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_M250", 55, 200, 8.0});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_L250", 125, 225, 1.5});

    std::cout << "\nFor comparison, with documented parameters:\n";
    std::cout << "--------------------------------------------\n\n";

    test_params(series, ohlcv_bars, tssb_bars, {"TREND_S100", 10, 100, 1.0});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_M100", 50, 100, 1.0});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_L100", 120, 100, 1.0});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_M250", 50, 250, 1.0});
    test_params(series, ohlcv_bars, tssb_bars, {"TREND_L250", 120, 250, 1.0});

    return 0;
}

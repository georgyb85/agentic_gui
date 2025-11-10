#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

double compute_trend_with_params(const std::vector<OHLCVBar>& ohlcv_bars, size_t icase,
                                  int lookback, int atr_length, double c_mult) {
    std::vector<double> open_arr, high_arr, low_arr, close_arr;
    for (const auto& bar : ohlcv_bars) {
        open_arr.push_back(bar.open);
        high_arr.push_back(bar.high);
        low_arr.push_back(bar.low);
        close_arr.push_back(bar.close);
    }

    std::span<const double> open_span(open_arr.data(), open_arr.size());
    std::span<const double> high_span(high_arr.data(), high_arr.size());
    std::span<const double> low_span(low_arr.data(), low_arr.size());
    std::span<const double> close_span(close_arr.data(), close_arr.size());

    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    double dot_prod = 0.0;
    double mean = 0.0;
    for (int k = 0; k < lookback; ++k) {
        int idx = icase - lookback + 1 + k;
        double price = std::log(close_arr[idx]);
        mean += price;
        dot_prod += price * c1[k];
    }
    mean /= lookback;

    int k_factor = lookback - 1;
    if (lookback == 2) {
        k_factor = 2;
    }
    double atr_val = atr(true, open_span, high_span, low_span, close_span, icase, atr_length);
    double denom = atr_val * k_factor;
    double indicator = dot_prod * 2.0 / (denom + 1.e-60);

    // R-squared
    double yss = 0.0;
    double rsq_sum = 0.0;
    for (int k = 0; k < lookback; ++k) {
        int idx = icase - lookback + 1 + k;
        double price = std::log(close_arr[idx]);
        double diff = price - mean;
        yss += diff * diff;
        double pred = dot_prod * c1[k];
        diff = diff - pred;
        rsq_sum += diff * diff;
    }
    double rsq = 1.0 - rsq_sum / (yss + 1.e-60);
    if (rsq < 0.0) {
        rsq = 0.0;
    }

    indicator *= rsq;
    return 100.0 * normal_cdf(c_mult * indicator) - 50.0;
}

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    size_t test_bar = 1078;
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");
    double target = expected[test_bar];

    std::cout << "Target TSSB value: " << target << "\n\n";
    std::cout << "Searching for parameter combination:\n";
    std::cout << "lookback, atr_length, c_mult -> result (error)\n\n";

    double best_error = 1e10;
    int best_lookback = 0, best_atr = 0;
    double best_c = 0;

    // Test reasonable parameter ranges
    for (int lookback = 5; lookback <= 30; lookback += 5) {
        for (int atr_length = 50; atr_length <= 150; atr_length += 25) {
            for (double c = 1.0; c <= 10.0; c += 0.5) {
                try {
                    double result = compute_trend_with_params(ohlcv_bars, test_bar, lookback, atr_length, c);
                    double error = std::abs(result - target);

                    if (error < best_error) {
                        best_error = error;
                        best_lookback = lookback;
                        best_atr = atr_length;
                        best_c = c;

                        if (error < 1.0) {
                            std::cout << std::setprecision(6);
                            std::cout << "  " << lookback << ", " << atr_length << ", " << c
                                      << " -> " << result << " (error=" << error << ")\n";
                        }
                    }
                } catch (...) {
                    // Skip invalid parameter combinations
                }
            }
        }
    }

    std::cout << "\nBest match:\n";
    std::cout << "  lookback=" << best_lookback << ", atr_length=" << best_atr
              << ", c=" << best_c << ", error=" << best_error << "\n";

    return 0;
}

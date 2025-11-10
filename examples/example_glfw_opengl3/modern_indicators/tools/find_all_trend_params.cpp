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

double compute_trend(const std::vector<OHLCVBar>& ohlcv_bars, size_t icase,
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

    double dot_prod = 0.0, mean = 0.0;
    for (int k = 0; k < lookback; ++k) {
        int idx = icase - lookback + 1 + k;
        double price = std::log(close_arr[idx]);
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
        double price = std::log(close_arr[idx]);
        double diff = price - mean;
        yss += diff * diff;
        double pred = dot_prod * c1[k];
        rsq_sum += (diff - pred) * (diff - pred);
    }
    double rsq = std::max(0.0, 1.0 - rsq_sum / (yss + 1.e-60));
    indicator *= rsq;
    return 100.0 * normal_cdf(c_mult * indicator) - 50.0;
}

void find_params(const std::vector<OHLCVBar>& ohlcv_bars,
                const std::vector<TSBBIndicatorBar>& tssb_bars,
                const std::string& name, int doc_lookback, int doc_atr) {

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);
    size_t test_bar = 1078;
    double target = expected[test_bar];

    std::cout << "\n" << name << " (documented: lookback=" << doc_lookback
              << ", atr=" << doc_atr << "):\n";
    std::cout << "  Target value: " << std::setprecision(10) << target << "\n";

    double best_error = 1e10;
    int best_lookback = 0, best_atr = 0;
    double best_c = 0;

    for (int lb = doc_lookback - 5; lb <= doc_lookback + 5; lb += 5) {
        if (lb < 2) continue;
        for (int atr_len = doc_atr - 50; atr_len <= doc_atr + 50; atr_len += 25) {
            if (atr_len < 10) continue;
            for (double c = 1.0; c <= 10.0; c += 0.5) {
                try {
                    double result = compute_trend(ohlcv_bars, test_bar, lb, atr_len, c);
                    double error = std::abs(result - target);
                    if (error < best_error) {
                        best_error = error;
                        best_lookback = lb;
                        best_atr = atr_len;
                        best_c = c;
                    }
                } catch (...) {}
            }
        }
    }

    std::cout << "  Best params: lookback=" << best_lookback << ", atr=" << best_atr
              << ", c=" << std::setprecision(2) << best_c
              << ", error=" << std::setprecision(6) << best_error << "\n";
}

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "Finding actual parameters for TREND indicators:\n";
    std::cout << "============================================\n";

    find_params(ohlcv_bars, tssb_bars, "TREND_S100", 10, 100);
    find_params(ohlcv_bars, tssb_bars, "TREND_M100", 50, 100);
    find_params(ohlcv_bars, tssb_bars, "TREND_L100", 120, 100);
    find_params(ohlcv_bars, tssb_bars, "TREND_M250", 50, 250);
    find_params(ohlcv_bars, tssb_bars, "TREND_L250", 120, 250);

    return 0;
}

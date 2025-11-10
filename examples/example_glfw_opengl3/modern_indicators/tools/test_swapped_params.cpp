#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

// Test TREND with swapped parameters
double compute_trend_custom(const std::vector<OHLCVBar>& ohlcv_bars, size_t icase, int lookback, int atr_length) {
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
    return 100.0 * normal_cdf(indicator) - 50.0;
}

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    size_t test_bar = 1078;

    std::cout << std::setprecision(10);
    std::cout << "Testing TREND_S100 with different parameter interpretations:\n\n";

    // Test 1: lookback=10, atr=100 (current interpretation)
    double result1 = compute_trend_custom(ohlcv_bars, test_bar, 10, 100);
    std::cout << "lookback=10, atr=100: " << result1 << "\n";

    // Test 2: lookback=100, atr=10 (swapped)
    double result2 = compute_trend_custom(ohlcv_bars, test_bar, 100, 10);
    std::cout << "lookback=100, atr=10: " << result2 << "\n";

    // TSSB value
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");
    std::cout << "\nTSSB value: " << expected[test_bar] << "\n";

    std::cout << "\nErrors:\n";
    std::cout << "  lookback=10, atr=100: " << std::abs(result1 - expected[test_bar]) << "\n";
    std::cout << "  lookback=100, atr=10: " << std::abs(result2 - expected[test_bar]) << "\n";

    return 0;
}

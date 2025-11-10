#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

double compute_trend_bar(const std::vector<OHLCVBar>& ohlcv_bars, size_t icase,
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

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");

    std::cout << "Testing if parameters work consistently across different bars:\n";
    std::cout << "=============================================================\n\n";

    // Test bars: first, middle, last
    std::vector<size_t> test_bars = {1078, 2000, 4000, 6000, 8000, 9000};

    std::cout << "TREND_S100 with (lookback=10, atr=75, c=4.0):\n\n";
    std::cout << "Bar      TSSB Value    Our Value     Error\n";
    std::cout << "---  --------------  -----------  --------\n";

    for (size_t bar : test_bars) {
        if (bar >= ohlcv_bars.size()) continue;

        double our_value = compute_trend_bar(ohlcv_bars, bar, 10, 75, 4.0);
        double tssb_value = expected[bar];
        double error = our_value - tssb_value;

        std::cout << std::setw(4) << bar << "  "
                  << std::setw(14) << std::setprecision(8) << tssb_value << "  "
                  << std::setw(11) << our_value << "  "
                  << std::setw(8) << error << "\n";
    }

    std::cout << "\n\nTesting with documented params (lookback=10, atr=100, c=1.0):\n\n";
    std::cout << "Bar      TSSB Value    Our Value     Error\n";
    std::cout << "---  --------------  -----------  --------\n";

    for (size_t bar : test_bars) {
        if (bar >= ohlcv_bars.size()) continue;

        double our_value = compute_trend_bar(ohlcv_bars, bar, 10, 100, 1.0);
        double tssb_value = expected[bar];
        double error = our_value - tssb_value;

        std::cout << std::setw(4) << bar << "  "
                  << std::setw(14) << std::setprecision(8) << tssb_value << "  "
                  << std::setw(11) << our_value << "  "
                  << std::setw(8) << error << "\n";
    }

    return 0;
}

#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

// Direct replication of TSSB COMP_VAR.CPP lines 553-621
int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    size_t test_bar = 1078;
    int lookback = 10;
    int atr_length = 100;

    std::cout << std::setprecision(15);
    std::cout << "===== EXACT TSSB REPLICATION =====\n";
    std::cout << "Test bar (icase): " << test_bar << "\n";
    std::cout << "lookback: " << lookback << ", atr_length: " << atr_length << "\n\n";

    // Prepare data arrays
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

    // Step 1: Compute Legendre coefficients (TSSB line 561)
    std::vector<double> work1, work2, work3;
    legendre_linear(lookback, work1, work2, work3);

    std::cout << "Legendre work1 (linear) coefficients:\n";
    for (int i = 0; i < lookback; ++i) {
        std::cout << "  work1[" << i << "] = " << work1[i] << "\n";
    }

    // Step 2: Compute dot product and mean (TSSB lines 577-584)
    const double* dptr = work1.data();
    double dot_prod = 0.0;
    double mean = 0.0;

    std::cout << "\nIterating window [" << (test_bar - lookback + 1) << " to " << test_bar << "]:\n";
    for (int k = test_bar - lookback + 1; k <= (int)test_bar; ++k) {
        double price = std::log(close_arr[k]);
        mean += price;
        double contrib = price * (*dptr);
        dot_prod += contrib;
        std::cout << "  k=" << k << ", close=" << close_arr[k]
                  << ", log=" << price
                  << ", coef=" << *dptr
                  << ", contrib=" << contrib << "\n";
        ++dptr;
    }
    mean /= lookback;
    dptr = work1.data(); // Reset pointer (TSSB line 585)

    std::cout << "\ndot_prod = " << dot_prod << "\n";
    std::cout << "mean = " << mean << "\n";

    // Step 3: Compute denominator (TSSB lines 594-597)
    int k = lookback - 1;
    if (lookback == 2) {
        k = 2;
    }
    const double atr_val = atr(true, open_span, high_span, low_span, close_span, test_bar, atr_length);
    const double denom = atr_val * k;

    std::cout << "k_factor = " << k << "\n";
    std::cout << "ATR = " << atr_val << "\n";
    std::cout << "denom = " << denom << "\n";

    // Step 4: Compute basic indicator (TSSB line 598)
    double output = dot_prod * 2.0 / (denom + 1.e-60);
    std::cout << "\nIndicator before R² = " << output << "\n";

    // Step 5: Compute R-squared (TSSB lines 604-616)
    double yss = 0.0;
    double rsq = 0.0;
    for (int k = test_bar - lookback + 1; k <= (int)test_bar; ++k) {
        double price = std::log(close_arr[k]);
        double diff = price - mean;
        yss += diff * diff;
        double pred = dot_prod * (*dptr);
        ++dptr;
        diff = diff - pred;
        rsq += diff * diff;
    }
    rsq = 1.0 - rsq / (yss + 1.e-60);
    if (rsq < 0.0) {
        rsq = 0.0;
    }
    output *= rsq; // TSSB line 616

    std::cout << "yss = " << yss << "\n";
    std::cout << "rsq = " << rsq << "\n";
    std::cout << "Indicator after R² = " << output << "\n";

    // Step 6: Compress (TSSB line 618)
    output = 100.0 * normal_cdf(output) - 50.0;
    std::cout << "\nFinal compressed indicator = " << output << "\n";

    // Compare with TSSB
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");
    std::cout << "TSSB value = " << expected[test_bar] << "\n";
    std::cout << "Error = " << (output - expected[test_bar]) << "\n";

    return 0;
}

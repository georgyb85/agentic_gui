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

// Test different formulas
double compute_trend_formula(double raw_indicator, double rsq, int formula_id) {
    double value;
    switch (formula_id) {
        case 1: // TSSB standard: R² degradation then compress with c=1.0
            value = raw_indicator * rsq;
            return 100.0 * normal_cdf(value) - 50.0;
        case 2: // No R² degradation, compress with c=1.0
            return 100.0 * normal_cdf(raw_indicator) - 50.0;
        case 3: // No R² degradation, compress with c=2.0
            return 100.0 * normal_cdf(2.0 * raw_indicator) - 50.0;
        case 4: // No R² degradation, compress with c=3.0
            return 100.0 * normal_cdf(3.0 * raw_indicator) - 50.0;
        case 5: // Compress FIRST with c=3.0, THEN apply R² degradation
            value = 100.0 * normal_cdf(3.0 * raw_indicator) - 50.0;
            return value * rsq;
        case 6: // No R² degradation, compress with c=1.5
            return 100.0 * normal_cdf(1.5 * raw_indicator) - 50.0;
        default:
            return 0.0;
    }
}

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

    // Get expected values
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");

    // Compute raw indicators and R² for all bars
    int lookback = 10;
    int atr_length = 100;

    std::vector<double> open_vec(ohlcv_bars.size()), high_vec(ohlcv_bars.size()),
                        low_vec(ohlcv_bars.size()), close_vec(ohlcv_bars.size());
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        open_vec[i] = ohlcv_bars[i].open;
        high_vec[i] = ohlcv_bars[i].high;
        low_vec[i] = ohlcv_bars[i].low;
        close_vec[i] = ohlcv_bars[i].close;
    }

    std::vector<double> raw_indicators(ohlcv_bars.size(), 0.0);
    std::vector<double> rsq_values(ohlcv_bars.size(), 0.0);

    // Compute Legendre coefficients once
    std::vector<double> c1, c2, c3;
    legendre_linear(lookback, c1, c2, c3);

    int front_bad = std::max(lookback - 1, atr_length);

    for (size_t icase = front_bad; icase < ohlcv_bars.size(); ++icase) {
        // Compute dot product and mean
        double dot_prod = 0.0;
        double mean = 0.0;
        for (int k = 0; k < lookback; ++k) {
            size_t idx = icase - lookback + 1 + k;
            double price = std::log(ohlcv_bars[idx].close);
            mean += price;
            dot_prod += price * c1[k];
        }
        mean /= lookback;

        // Compute ATR and denominator
        double atr_val = atr(true, {open_vec.data(), open_vec.size()}, {high_vec.data(), high_vec.size()},
                             {low_vec.data(), low_vec.size()}, {close_vec.data(), close_vec.size()},
                             icase, atr_length);
        int k_factor = (lookback == 2) ? 2 : (lookback - 1);
        double denom = atr_val * k_factor;

        // Raw indicator
        raw_indicators[icase] = dot_prod * 2.0 / (denom + 1.e-60);

        // Compute R-squared
        double yss = 0.0;
        double rsq_sum = 0.0;
        for (int k = 0; k < lookback; ++k) {
            size_t idx = icase - lookback + 1 + k;
            double price = std::log(ohlcv_bars[idx].close);
            double diff = price - mean;
            yss += diff * diff;
            double pred = dot_prod * c1[k];
            double error = diff - pred;
            rsq_sum += error * error;
        }
        double rsq = 1.0 - rsq_sum / (yss + 1.e-60);
        if (rsq < 0.0) rsq = 0.0;
        rsq_values[icase] = rsq;
    }

    // Find first valid bar
    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            first_valid = i;
            break;
        }
    }

    std::cout << "================================================================\n";
    std::cout << "TESTING ALL FORMULAS FOR TREND_S100\n";
    std::cout << "================================================================\n\n";

    const char* formula_names[] = {
        "",
        "Formula 1: TSSB standard (R² then c=1.0)",
        "Formula 2: No R², c=1.0",
        "Formula 3: No R², c=2.0",
        "Formula 4: No R², c=3.0",
        "Formula 5: Compress c=3.0 THEN R² degradation",
        "Formula 6: No R², c=1.5"
    };

    for (int formula = 1; formula <= 6; ++formula) {
        double sum_abs_error = 0.0;
        double max_abs_error = 0.0;
        int valid_count = 0;
        int under_0_1 = 0;
        int under_1_0 = 0;

        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                double computed = compute_trend_formula(raw_indicators[i], rsq_values[i], formula);
                double error = computed - expected[i];
                double abs_error = std::fabs(error);

                sum_abs_error += abs_error;
                if (abs_error > max_abs_error) max_abs_error = abs_error;
                if (abs_error < 0.1) under_0_1++;
                if (abs_error < 1.0) under_1_0++;
                valid_count++;
            }
        }

        double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;

        std::cout << formula_names[formula] << "\n";
        std::cout << "  MAE: " << std::fixed << std::setprecision(4) << mae << "\n";
        std::cout << "  Max Error: " << max_abs_error << "\n";
        std::cout << "  Bars < 0.1: " << under_0_1 << " (" << (100.0 * under_0_1 / valid_count) << "%)\n";
        std::cout << "  Bars < 1.0: " << under_1_0 << " (" << (100.0 * under_1_0 / valid_count) << "%)\n";
        if (mae < 0.1) {
            std::cout << "  ✓ PERFECT MATCH!\n";
        } else if (mae < 1.0) {
            std::cout << "  ✓ GOOD\n";
        } else {
            std::cout << "  ✗ HIGH ERROR\n";
        }
        std::cout << "\n";
    }

    return 0;
}

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

// Test different smoothing factors for ADX
double compute_adx_with_factor(const std::vector<OHLCVBar>& bars, int lookback,
                               double smoothing_factor, std::vector<double>& output) {
    int n = bars.size();
    output.resize(n, 0.0);

    double DMSplus = 0, DMSminus = 0, ATR = 0, ADX = 0;

    // Phase 1 - accumulation
    for (int i = 1; i <= lookback && i < n; i++) {
        double DMplus = bars[i].high - bars[i-1].high;
        double DMminus = bars[i-1].low - bars[i].low;
        if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
        if (DMplus < 0) DMplus = 0;
        if (DMminus < 0) DMminus = 0;

        DMSplus += DMplus;
        DMSminus += DMminus;

        double tr = bars[i].high - bars[i].low;
        tr = std::max(tr, bars[i].high - bars[i-1].close);
        tr = std::max(tr, bars[i-1].close - bars[i].low);
        ATR += tr;

        double DIplus = DMSplus / (ATR + 1.e-10);
        double DIminus = DMSminus / (ATR + 1.e-10);
        ADX = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        output[i] = 100.0 * ADX;
    }

    // Phase 2 - start smoothing
    for (int i = lookback + 1; i < 2 * lookback && i < n; i++) {
        double DMplus = bars[i].high - bars[i-1].high;
        double DMminus = bars[i-1].low - bars[i].low;
        if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
        if (DMplus < 0) DMplus = 0;
        if (DMminus < 0) DMminus = 0;

        // Use custom smoothing factor
        DMSplus = smoothing_factor * DMSplus + (1 - smoothing_factor) * DMplus * lookback;
        DMSminus = smoothing_factor * DMSminus + (1 - smoothing_factor) * DMminus * lookback;

        double tr = bars[i].high - bars[i].low;
        tr = std::max(tr, bars[i].high - bars[i-1].close);
        tr = std::max(tr, bars[i-1].close - bars[i].low);
        ATR = smoothing_factor * ATR + (1 - smoothing_factor) * tr * lookback;

        double DIplus = DMSplus / (ATR + 1.e-10);
        double DIminus = DMSminus / (ATR + 1.e-10);
        ADX += std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        output[i] = 100.0 * ADX / (i - lookback + 1);
    }

    if (2 * lookback - 1 < n) ADX /= lookback;

    // Phase 3 - full smoothing
    for (int i = 2 * lookback; i < n; i++) {
        double DMplus = bars[i].high - bars[i-1].high;
        double DMminus = bars[i-1].low - bars[i].low;
        if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
        if (DMplus < 0) DMplus = 0;
        if (DMminus < 0) DMminus = 0;

        DMSplus = smoothing_factor * DMSplus + (1 - smoothing_factor) * DMplus * lookback;
        DMSminus = smoothing_factor * DMSminus + (1 - smoothing_factor) * DMminus * lookback;

        double tr = bars[i].high - bars[i].low;
        tr = std::max(tr, bars[i].high - bars[i-1].close);
        tr = std::max(tr, bars[i-1].close - bars[i].low);
        ATR = smoothing_factor * ATR + (1 - smoothing_factor) * tr * lookback;

        double DIplus = DMSplus / (ATR + 1.e-10);
        double DIminus = DMSminus / (ATR + 1.e-10);
        double term = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);

        // Use different smoothing for ADX itself
        ADX = smoothing_factor * ADX + (1 - smoothing_factor) * term;
        output[i] = 100.0 * ADX;
    }

    return 0.0;  // Will calculate MAE in main
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");

    std::cout << "TESTING DIFFERENT SMOOTHING FACTORS FOR ADX\n";
    std::cout << "============================================\n\n";

    std::cout << "Lookback = 14\n\n";

    // Test different smoothing factors
    std::vector<double> factors = {
        0.9286,  // Standard Wilder: (14-1)/14 = 13/14
        0.9333,  // 14/15
        0.8667,  // 13/15 (EMA style: 1 - 2/(14+1))
        0.8000,  // Less smoothing
        0.7500,  // Even less
        0.7000,  // Much less
        0.6000,  // Very responsive
        0.5000   // Half smoothing
    };

    std::cout << std::setw(20) << "Smoothing Factor"
              << std::setw(15) << "MAE"
              << std::setw(20) << "First Bar Error"
              << std::setw(15) << "Variance" << "\n";
    std::cout << std::string(70, '-') << "\n";

    double best_mae = 1e10;
    double best_factor = 0;

    for (double factor : factors) {
        std::vector<double> output;
        compute_adx_with_factor(ohlcv_bars, 14, factor, output);

        // Calculate MAE
        double mae = 0, variance = 0;
        double first_error = 0;
        int count = 0;

        for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                double error = output[i] - expected[i];
                mae += std::abs(error);
                variance += error * error;
                if (count == 0) first_error = error;
                count++;
            }
        }

        mae /= count;
        variance = variance / count - (mae * mae);

        std::cout << std::fixed << std::setprecision(4)
                  << std::setw(20) << factor
                  << std::setw(15) << mae
                  << std::setw(20) << first_error
                  << std::setw(15) << variance << "\n";

        if (mae < best_mae) {
            best_mae = mae;
            best_factor = factor;
        }
    }

    std::cout << "\n\nBest smoothing factor: " << best_factor << " with MAE: " << best_mae << "\n";

    // Show detailed comparison with best factor
    std::vector<double> best_output;
    compute_adx_with_factor(ohlcv_bars, 14, best_factor, best_output);

    std::cout << "\nFirst 10 bars with best smoothing factor (" << best_factor << "):\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(12) << "CSV"
              << std::setw(12) << "Computed"
              << std::setw(12) << "Error" << "\n";
    std::cout << std::string(42, '-') << "\n";

    for (int i = 0; i < 10; ++i) {
        size_t idx = 1078 + i;
        double error = best_output[idx] - expected[idx];
        std::cout << std::setw(6) << idx
                  << std::fixed << std::setprecision(4)
                  << std::setw(12) << expected[idx]
                  << std::setw(12) << best_output[idx]
                  << std::setw(12) << error << "\n";
    }

    return 0;
}
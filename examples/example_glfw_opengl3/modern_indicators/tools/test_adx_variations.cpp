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

// Test different smoothing and lag variations
void compute_adx_variations(const std::vector<OHLCVBar>& bars, int lookback,
                            std::vector<double>& standard_output,
                            std::vector<double>& shifted_output,
                            std::vector<double>& ema_smooth_output,
                            std::vector<double>& no_smooth_output) {
    int n = bars.size();
    standard_output.resize(n, 0.0);
    shifted_output.resize(n, 0.0);
    ema_smooth_output.resize(n, 0.0);
    no_smooth_output.resize(n, 0.0);

    // VARIATION 1: Standard (our current implementation)
    {
        double DMSplus = 0, DMSminus = 0, ATR = 0, ADX = 0;

        // Phase 1
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
            standard_output[i] = 100.0 * ADX;
        }

        // Phase 2
        for (int i = lookback + 1; i < 2 * lookback && i < n; i++) {
            double DMplus = bars[i].high - bars[i-1].high;
            double DMminus = bars[i-1].low - bars[i].low;
            if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
            if (DMplus < 0) DMplus = 0;
            if (DMminus < 0) DMminus = 0;

            DMSplus = (lookback - 1.0) / lookback * DMSplus + DMplus;
            DMSminus = (lookback - 1.0) / lookback * DMSminus + DMminus;

            double tr = bars[i].high - bars[i].low;
            tr = std::max(tr, bars[i].high - bars[i-1].close);
            tr = std::max(tr, bars[i-1].close - bars[i].low);
            ATR = (lookback - 1.0) / lookback * ATR + tr;

            double DIplus = DMSplus / (ATR + 1.e-10);
            double DIminus = DMSminus / (ATR + 1.e-10);
            ADX += std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
            standard_output[i] = 100.0 * ADX / (i - lookback + 1);
        }

        if (2 * lookback - 1 < n) ADX /= lookback;

        // Phase 3
        for (int i = 2 * lookback; i < n; i++) {
            double DMplus = bars[i].high - bars[i-1].high;
            double DMminus = bars[i-1].low - bars[i].low;
            if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
            if (DMplus < 0) DMplus = 0;
            if (DMminus < 0) DMminus = 0;

            DMSplus = (lookback - 1.0) / lookback * DMSplus + DMplus;
            DMSminus = (lookback - 1.0) / lookback * DMSminus + DMminus;

            double tr = bars[i].high - bars[i].low;
            tr = std::max(tr, bars[i].high - bars[i-1].close);
            tr = std::max(tr, bars[i-1].close - bars[i].low);
            ATR = (lookback - 1.0) / lookback * ATR + tr;

            double DIplus = DMSplus / (ATR + 1.e-10);
            double DIminus = DMSminus / (ATR + 1.e-10);
            double term = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
            ADX = (lookback - 1.0) / lookback * ADX + term / lookback;
            standard_output[i] = 100.0 * ADX;
        }
    }

    // VARIATION 2: Shifted output (output at i-1 instead of i)
    shifted_output = standard_output;
    for (int i = n-1; i > 0; i--) {
        shifted_output[i] = shifted_output[i-1];
    }
    shifted_output[0] = 0;

    // VARIATION 3: EMA-style smoothing (2/(n+1) instead of 1/n)
    {
        double DMSplus = 0, DMSminus = 0, ATR = 0, ADX = 0;
        double alpha = 2.0 / (lookback + 1.0);  // EMA smoothing factor

        // Phase 1 - same as standard
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
            ema_smooth_output[i] = 100.0 * ADX;
        }

        // Phase 2 & 3 with EMA smoothing
        for (int i = lookback + 1; i < n; i++) {
            double DMplus = bars[i].high - bars[i-1].high;
            double DMminus = bars[i-1].low - bars[i].low;
            if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
            if (DMplus < 0) DMplus = 0;
            if (DMminus < 0) DMminus = 0;

            // Use EMA smoothing
            DMSplus = (1 - alpha) * DMSplus + alpha * DMplus * lookback;
            DMSminus = (1 - alpha) * DMSminus + alpha * DMminus * lookback;

            double tr = bars[i].high - bars[i].low;
            tr = std::max(tr, bars[i].high - bars[i-1].close);
            tr = std::max(tr, bars[i-1].close - bars[i].low);
            ATR = (1 - alpha) * ATR + alpha * tr * lookback;

            double DIplus = DMSplus / (ATR + 1.e-10);
            double DIminus = DMSminus / (ATR + 1.e-10);
            double term = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);

            if (i < 2 * lookback) {
                ADX += term;
                ema_smooth_output[i] = 100.0 * ADX / (i - lookback + 1);
                if (i == 2 * lookback - 1) ADX /= lookback;
            } else {
                ADX = (1 - alpha) * ADX + alpha * term;
                ema_smooth_output[i] = 100.0 * ADX;
            }
        }
    }

    // VARIATION 4: No smoothing in phase 3 (direct DX output)
    {
        double DMSplus = 0, DMSminus = 0, ATR = 0;

        // Phases 1 & 2 same as standard
        for (int i = 1; i < 2 * lookback && i < n; i++) {
            // ... same calculation as standard ...
        }

        // Phase 3 without ADX smoothing
        for (int i = 2 * lookback; i < n; i++) {
            double DMplus = bars[i].high - bars[i-1].high;
            double DMminus = bars[i-1].low - bars[i].low;
            if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
            if (DMplus < 0) DMplus = 0;
            if (DMminus < 0) DMminus = 0;

            DMSplus = (lookback - 1.0) / lookback * DMSplus + DMplus;
            DMSminus = (lookback - 1.0) / lookback * DMSminus + DMminus;

            double tr = bars[i].high - bars[i].low;
            tr = std::max(tr, bars[i].high - bars[i-1].close);
            tr = std::max(tr, bars[i-1].close - bars[i].low);
            ATR = (lookback - 1.0) / lookback * ATR + tr;

            double DIplus = DMSplus / (ATR + 1.e-10);
            double DIminus = DMSminus / (ATR + 1.e-10);
            double DX = 100.0 * std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
            no_smooth_output[i] = DX;  // Direct DX without smoothing
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::vector<double> standard, shifted, ema, no_smooth;
    compute_adx_variations(ohlcv_bars, 14, standard, shifted, ema, no_smooth);

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");

    std::cout << "ADX VARIATION TESTS\n";
    std::cout << "===================\n\n";

    // Compare at CSV start
    std::cout << "Comparison at first 10 CSV bars:\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(12) << "CSV"
              << std::setw(12) << "Standard"
              << std::setw(12) << "Shifted"
              << std::setw(12) << "EMA"
              << std::setw(12) << "No Smooth" << "\n";
    std::cout << std::string(66, '-') << "\n";

    for (int i = 0; i < 10; ++i) {
        size_t idx = 1078 + i;
        std::cout << std::setw(6) << idx
                  << std::fixed << std::setprecision(4)
                  << std::setw(12) << expected[idx]
                  << std::setw(12) << standard[idx]
                  << std::setw(12) << shifted[idx]
                  << std::setw(12) << ema[idx]
                  << std::setw(12) << no_smooth[idx] << "\n";
    }

    // Calculate MAE for each variation
    std::cout << "\n\nMean Absolute Error for each variation:\n";
    std::cout << std::string(40, '-') << "\n";

    double mae_standard = 0, mae_shifted = 0, mae_ema = 0, mae_no_smooth = 0;
    int count = 0;

    for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            mae_standard += std::abs(standard[i] - expected[i]);
            mae_shifted += std::abs(shifted[i] - expected[i]);
            mae_ema += std::abs(ema[i] - expected[i]);
            mae_no_smooth += std::abs(no_smooth[i] - expected[i]);
            count++;
        }
    }

    std::cout << "Standard (current): " << (mae_standard / count) << "\n";
    std::cout << "Shifted (lag fix): " << (mae_shifted / count) << "\n";
    std::cout << "EMA smoothing: " << (mae_ema / count) << "\n";
    std::cout << "No smoothing (DX): " << (mae_no_smooth / count) << "\n";

    return 0;
}
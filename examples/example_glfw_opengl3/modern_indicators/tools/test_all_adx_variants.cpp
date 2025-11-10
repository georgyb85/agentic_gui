#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>

using namespace tssb;
using namespace tssb::validation;

// VARIANT 1: Current TSSB implementation (Wilder's original)
// Complex initialization, then Wilder's exponential smoothing
void adx_variant_wilder(const std::vector<OHLCVBar>& bars, int lookback, std::vector<double>& output) {
    int n = bars.size();
    output.assign(n, 0.0);

    double DMSplus = 0, DMSminus = 0, ATR = 0, ADX = 0;

    // Phase 1: Accumulation
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

    // Phase 2: Secondary initialization
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
        output[i] = 100.0 * ADX / (i - lookback + 1);
    }

    if (2 * lookback - 1 < n) ADX /= lookback;

    // Phase 3: Full exponential smoothing
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
        output[i] = 100.0 * ADX;
    }
}

// VARIANT 2: Pure EMA from start (simpler version mentioned in book)
// Jump-start with first values, use EMA throughout
void adx_variant_pure_ema(const std::vector<OHLCVBar>& bars, int lookback, std::vector<double>& output) {
    int n = bars.size();
    output.assign(n, 0.0);

    if (n < 2) return;

    double alpha = 1.0 / lookback;  // Wilder's alpha

    // Initialize with first bar
    double DMplus = bars[1].high - bars[0].high;
    double DMminus = bars[0].low - bars[1].low;
    if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
    if (DMplus < 0) DMplus = 0;
    if (DMminus < 0) DMminus = 0;

    double DMSplus = DMplus * lookback;
    double DMSminus = DMminus * lookback;

    double tr = bars[1].high - bars[1].low;
    tr = std::max(tr, bars[1].high - bars[0].close);
    tr = std::max(tr, bars[0].close - bars[1].low);
    double ATR = tr * lookback;

    double DIplus = DMSplus / (ATR + 1.e-10);
    double DIminus = DMSminus / (ATR + 1.e-10);
    double ADX = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);

    output[0] = 0;
    output[1] = 100.0 * ADX;

    // From bar 2 onward, use pure EMA
    for (int i = 2; i < n; i++) {
        DMplus = bars[i].high - bars[i-1].high;
        DMminus = bars[i-1].low - bars[i].low;
        if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
        if (DMplus < 0) DMplus = 0;
        if (DMminus < 0) DMminus = 0;

        DMSplus = (1 - alpha) * DMSplus + alpha * DMplus * lookback;
        DMSminus = (1 - alpha) * DMSminus + alpha * DMminus * lookback;

        tr = bars[i].high - bars[i].low;
        tr = std::max(tr, bars[i].high - bars[i-1].close);
        tr = std::max(tr, bars[i-1].close - bars[i].low);
        ATR = (1 - alpha) * ATR + alpha * tr * lookback;

        DIplus = DMSplus / (ATR + 1.e-10);
        DIminus = DMSminus / (ATR + 1.e-10);
        double term = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        ADX = (1 - alpha) * ADX + alpha * term;

        output[i] = 100.0 * ADX;
    }
}

// VARIANT 3: Standard EMA (2/(n+1))
// Uses conventional EMA smoothing factor
void adx_variant_standard_ema(const std::vector<OHLCVBar>& bars, int lookback, std::vector<double>& output) {
    int n = bars.size();
    output.assign(n, 0.0);

    if (n < lookback + 1) return;

    double alpha = 2.0 / (lookback + 1.0);  // Standard EMA alpha

    // Initialize with simple average
    double DMSplus = 0, DMSminus = 0, ATR = 0;

    for (int i = 1; i <= lookback; i++) {
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
    }

    DMSplus /= lookback;
    DMSminus /= lookback;
    ATR /= lookback;

    double DIplus = DMSplus / (ATR + 1.e-10);
    double DIminus = DMSminus / (ATR + 1.e-10);
    double ADX = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);

    output[lookback] = 100.0 * ADX;

    // From lookback+1 onward, use standard EMA
    for (int i = lookback + 1; i < n; i++) {
        double DMplus = bars[i].high - bars[i-1].high;
        double DMminus = bars[i-1].low - bars[i].low;
        if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
        if (DMplus < 0) DMplus = 0;
        if (DMminus < 0) DMminus = 0;

        DMSplus = (1 - alpha) * DMSplus + alpha * DMplus;
        DMSminus = (1 - alpha) * DMSminus + alpha * DMminus;

        double tr = bars[i].high - bars[i].low;
        tr = std::max(tr, bars[i].high - bars[i-1].close);
        tr = std::max(tr, bars[i-1].close - bars[i].low);
        ATR = (1 - alpha) * ATR + alpha * tr;

        DIplus = DMSplus / (ATR + 1.e-10);
        DIminus = DMSminus / (ATR + 1.e-10);
        double term = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        ADX = (1 - alpha) * ADX + alpha * term;

        output[i] = 100.0 * ADX;
    }
}

// VARIANT 4: Simple Moving Average (no exponential smoothing)
void adx_variant_sma(const std::vector<OHLCVBar>& bars, int lookback, std::vector<double>& output) {
    int n = bars.size();
    output.assign(n, 0.0);

    std::vector<double> DMplus_series, DMminus_series, TR_series;
    DMplus_series.reserve(n);
    DMminus_series.reserve(n);
    TR_series.reserve(n);

    DMplus_series.push_back(0);
    DMminus_series.push_back(0);
    TR_series.push_back(0);

    // Compute raw series
    for (int i = 1; i < n; i++) {
        double DMplus = bars[i].high - bars[i-1].high;
        double DMminus = bars[i-1].low - bars[i].low;
        if (DMplus >= DMminus) DMminus = 0; else DMplus = 0;
        if (DMplus < 0) DMplus = 0;
        if (DMminus < 0) DMminus = 0;

        double tr = bars[i].high - bars[i].low;
        tr = std::max(tr, bars[i].high - bars[i-1].close);
        tr = std::max(tr, bars[i-1].close - bars[i].low);

        DMplus_series.push_back(DMplus);
        DMminus_series.push_back(DMminus);
        TR_series.push_back(tr);
    }

    // Compute SMA of DM+ DM- and ATR, then DI, then DX, then SMA of DX
    std::vector<double> DX_series;
    DX_series.push_back(0);

    for (int i = 1; i < n; i++) {
        if (i < lookback) {
            DX_series.push_back(0);
            continue;
        }

        double DMSplus = 0, DMSminus = 0, ATR = 0;
        for (int j = i - lookback + 1; j <= i; j++) {
            DMSplus += DMplus_series[j];
            DMSminus += DMminus_series[j];
            ATR += TR_series[j];
        }

        double DIplus = DMSplus / (ATR + 1.e-10);
        double DIminus = DMSminus / (ATR + 1.e-10);
        double DX = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        DX_series.push_back(DX);
    }

    // Finally, SMA of DX to get ADX
    for (int i = 2 * lookback - 1; i < n; i++) {
        double ADX = 0;
        for (int j = i - lookback + 1; j <= i; j++) {
            ADX += DX_series[j];
        }
        ADX /= lookback;
        output[i] = 100.0 * ADX;
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");

    std::cout << "TESTING ALL ADX VARIANTS (lookback=14)\n";
    std::cout << "======================================\n\n";

    struct Variant {
        std::string name;
        std::vector<double> output;
    };

    std::vector<Variant> variants(4);
    variants[0].name = "Wilder (Current TSSB)";
    variants[1].name = "Pure EMA";
    variants[2].name = "Standard EMA (2/(n+1))";
    variants[3].name = "Simple Moving Average";

    adx_variant_wilder(ohlcv_bars, 14, variants[0].output);
    adx_variant_pure_ema(ohlcv_bars, 14, variants[1].output);
    adx_variant_standard_ema(ohlcv_bars, 14, variants[2].output);
    adx_variant_sma(ohlcv_bars, 14, variants[3].output);

    // Calculate MAE for each variant
    std::cout << std::setw(30) << "Variant"
              << std::setw(15) << "MAE"
              << std::setw(15) << "Max Error"
              << std::setw(15) << "First Error" << "\n";
    std::cout << std::string(75, '=') << "\n";

    for (auto& v : variants) {
        double mae = 0, max_error = 0, first_error = 0;
        int count = 0;

        for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(v.output[i])) {
                double error = std::abs(v.output[i] - expected[i]);
                mae += error;
                max_error = std::max(max_error, error);
                if (count == 0) first_error = v.output[i] - expected[i];
                count++;
            }
        }

        mae /= count;

        std::cout << std::setw(30) << v.name
                  << std::fixed << std::setprecision(6)
                  << std::setw(15) << mae
                  << std::setw(15) << max_error
                  << std::setw(15) << first_error;

        if (mae < 0.001) {
            std::cout << "  ✓✓✓ PERFECT MATCH!";
        } else if (mae < 0.01) {
            std::cout << "  ✓✓ EXCELLENT";
        } else if (mae < 0.1) {
            std::cout << "  ✓ GOOD";
        }
        std::cout << "\n";
    }

    // Find best match
    double best_mae = 1e10;
    int best_idx = -1;
    for (size_t i = 0; i < variants.size(); ++i) {
        double mae = 0;
        int count = 0;
        for (size_t j = 1078; j < ohlcv_bars.size(); ++j) {
            if (std::isfinite(expected[j]) && std::isfinite(variants[i].output[j])) {
                mae += std::abs(variants[i].output[j] - expected[j]);
                count++;
            }
        }
        mae /= count;
        if (mae < best_mae) {
            best_mae = mae;
            best_idx = i;
        }
    }

    std::cout << "\n\nBest match: " << variants[best_idx].name << " (MAE: " << best_mae << ")\n";

    // Show detailed comparison for best variant
    std::cout << "\nFirst 10 bars comparison for best variant:\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(12) << "CSV"
              << std::setw(12) << "Computed"
              << std::setw(12) << "Error" << "\n";
    std::cout << std::string(42, '-') << "\n";

    for (int i = 0; i < 10; ++i) {
        size_t idx = 1078 + i;
        double error = variants[best_idx].output[idx] - expected[idx];
        std::cout << std::setw(6) << idx
                  << std::fixed << std::setprecision(4)
                  << std::setw(12) << expected[idx]
                  << std::setw(12) << variants[best_idx].output[idx]
                  << std::setw(12) << error << "\n";
    }

    return 0;
}
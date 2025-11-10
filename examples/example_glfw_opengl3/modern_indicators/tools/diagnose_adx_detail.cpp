#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

// Test different ADX calculation approaches
void test_adx_variations(const std::vector<OHLCVBar>& ohlcv, int lookback, int target_bar) {
    std::cout << "\n====================================================================\n";
    std::cout << "TESTING ADX VARIATIONS (lookback=" << lookback << ")\n";
    std::cout << "====================================================================\n\n";

    // Variation 1: Our current implementation
    double DMSplus = 0.0, DMSminus = 0.0, ATR = 0.0, ADX = 0.0;

    // Phase 1
    for (int icase = 1; icase <= lookback; ++icase) {
        double DMplus = ohlcv[icase].high - ohlcv[icase - 1].high;
        double DMminus = ohlcv[icase - 1].low - ohlcv[icase].low;

        if (DMplus >= DMminus) {
            DMminus = 0.0;
        } else {
            DMplus = 0.0;
        }
        if (DMplus < 0.0) DMplus = 0.0;
        if (DMminus < 0.0) DMminus = 0.0;

        DMSplus += DMplus;
        DMSminus += DMminus;

        double tr = ohlcv[icase].high - ohlcv[icase].low;
        tr = std::max(tr, ohlcv[icase].high - ohlcv[icase - 1].close);
        tr = std::max(tr, ohlcv[icase - 1].close - ohlcv[icase].low);
        ATR += tr;

        double DIplus = DMSplus / (ATR + 1.e-10);
        double DIminus = DMSminus / (ATR + 1.e-10);
        ADX = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
    }

    std::cout << "After Phase 1 (bar " << lookback << "):\n";
    std::cout << "  DMSplus=" << DMSplus << ", DMSminus=" << DMSminus << ", ATR=" << ATR << "\n";
    std::cout << "  ADX=" << ADX << " (100*ADX=" << (100.0 * ADX) << ")\n\n";

    // Variation 2: Test if we should divide accumulated values by lookback
    std::cout << "VARIATION 2: Divide by lookback after Phase 1:\n";
    double avg_DMSplus = DMSplus / lookback;
    double avg_DMSminus = DMSminus / lookback;
    double avg_ATR = ATR / lookback;
    double DIplus2 = avg_DMSplus / (avg_ATR + 1.e-10);
    double DIminus2 = avg_DMSminus / (avg_ATR + 1.e-10);
    double ADX2 = std::fabs(DIplus2 - DIminus2) / (DIplus2 + DIminus2 + 1.e-10);
    std::cout << "  avg_DMSplus=" << avg_DMSplus << ", avg_DMSminus=" << avg_DMSminus << ", avg_ATR=" << avg_ATR << "\n";
    std::cout << "  ADX2=" << ADX2 << " (100*ADX2=" << (100.0 * ADX2) << ")\n\n";

    // Variation 3: Test Wilder's smoothing immediately
    std::cout << "VARIATION 3: Apply Wilder's smoothing factor:\n";
    double wilder_DMSplus = DMSplus * 1.0 / lookback;
    double wilder_DMSminus = DMSminus * 1.0 / lookback;
    double wilder_ATR = ATR * 1.0 / lookback;
    double DIplus3 = wilder_DMSplus / (wilder_ATR + 1.e-10);
    double DIminus3 = wilder_DMSminus / (wilder_ATR + 1.e-10);
    double ADX3 = std::fabs(DIplus3 - DIminus3) / (DIplus3 + DIminus3 + 1.e-10);
    std::cout << "  wilder_DMSplus=" << wilder_DMSplus << ", wilder_DMSminus=" << wilder_DMSminus << ", wilder_ATR=" << wilder_ATR << "\n";
    std::cout << "  ADX3=" << ADX3 << " (100*ADX3=" << (100.0 * ADX3) << ")\n\n";

    // Continue to target bar with our implementation
    // Phase 2
    for (int icase = lookback + 1; icase < 2 * lookback && icase < target_bar; ++icase) {
        double DMplus = ohlcv[icase].high - ohlcv[icase - 1].high;
        double DMminus = ohlcv[icase - 1].low - ohlcv[icase].low;

        if (DMplus >= DMminus) {
            DMminus = 0.0;
        } else {
            DMplus = 0.0;
        }
        if (DMplus < 0.0) DMplus = 0.0;
        if (DMminus < 0.0) DMminus = 0.0;

        DMSplus = (lookback - 1.0) / lookback * DMSplus + DMplus;
        DMSminus = (lookback - 1.0) / lookback * DMSminus + DMminus;

        double tr = ohlcv[icase].high - ohlcv[icase].low;
        tr = std::max(tr, ohlcv[icase].high - ohlcv[icase - 1].close);
        tr = std::max(tr, ohlcv[icase - 1].close - ohlcv[icase].low);
        ATR = (lookback - 1.0) / lookback * ATR + tr;

        double DIplus = DMSplus / (ATR + 1.e-10);
        double DIminus = DMSminus / (ATR + 1.e-10);
        ADX += std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
    }

    if (2 * lookback - 1 < target_bar) {
        ADX /= lookback;
        std::cout << "After Phase 2 (bar " << (2*lookback-1) << "):\n";
        std::cout << "  DMSplus=" << DMSplus << ", DMSminus=" << DMSminus << ", ATR=" << ATR << "\n";
        std::cout << "  ADX=" << ADX << " (100*ADX=" << (100.0 * ADX) << ")\n\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "ADX DETAILED INVESTIGATION\n";
    std::cout << "==========================\n\n";

    // Get expected value
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");

    // Check first few bars where ADX becomes valid
    std::cout << "ADX_S expected values at key bars:\n";
    for (int i = 27; i <= 30; ++i) {  // 2*lookback-1 = 27 for lookback=14
        if (std::isfinite(expected[i])) {
            std::cout << "  Bar " << i << ": " << expected[i] << "\n";
        }
    }
    std::cout << "  Bar 1078 (first CSV): " << expected[1078] << "\n\n";

    // Test variations at bar 28 (first fully valid bar)
    test_adx_variations(ohlcv_bars, 14, 28);

    // Check if there's an issue with data before bar 1078
    std::cout << "\n====================================================================\n";
    std::cout << "CHECKING BARS AROUND CSV START\n";
    std::cout << "====================================================================\n\n";

    std::cout << "OHLCV data around bar 1078:\n";
    for (int i = 1076; i <= 1080; ++i) {
        const auto& bar = ohlcv_bars[i];
        std::cout << "  Bar " << i << " (" << bar.date << " " << bar.time << "): "
                  << "O=" << bar.open << ", H=" << bar.high << ", L=" << bar.low
                  << ", C=" << bar.close << ", V=" << bar.volume << "\n";
    }

    // Compute ADX using our library
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::Adx;
    req.name = "ADX_S";
    req.params[0] = 14;

    auto result = compute_single_indicator(series, req);

    std::cout << "\nOur computed ADX values:\n";
    for (int i = 27; i <= 30; ++i) {
        std::cout << "  Bar " << i << ": " << result.values[i] << "\n";
    }
    std::cout << "  Bar 1078: " << result.values[1078] << "\n";

    // Check pattern of errors
    std::cout << "\n====================================================================\n";
    std::cout << "ERROR PATTERN ANALYSIS\n";
    std::cout << "====================================================================\n\n";

    std::cout << "First 100 bars with CSV data:\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(12) << "Expected"
              << std::setw(12) << "Computed"
              << std::setw(12) << "Error"
              << std::setw(10) << "Ratio" << "\n";
    std::cout << std::string(52, '-') << "\n";

    for (int i = 0; i < 20; ++i) {
        size_t idx = 1078 + i;
        if (std::isfinite(expected[idx]) && std::isfinite(result.values[idx])) {
            double error = result.values[idx] - expected[idx];
            double ratio = result.values[idx] / expected[idx];

            std::cout << std::setw(6) << idx
                      << std::fixed << std::setprecision(4)
                      << std::setw(12) << expected[idx]
                      << std::setw(12) << result.values[idx]
                      << std::setw(12) << error
                      << std::setw(10) << ratio << "\n";
        }
    }

    return 0;
}
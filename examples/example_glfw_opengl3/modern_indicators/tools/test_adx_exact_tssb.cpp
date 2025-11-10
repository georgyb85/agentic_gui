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

// Exact TSSB implementation from COMP_VAR.CPP lines 674-794
void compute_adx_tssb_exact(const std::vector<OHLCVBar>& bars, int lookback, std::vector<double>& output) {
    int n = bars.size();
    output.resize(n, 0.0);

    double DMplus, DMminus, DMSplus, DMSminus, DIplus, DIminus, term, ATR, ADX;

    // Line 679: output[0] = 0
    output[0] = 0.0;

    // Lines 681-715: Primary initialization
    DMSplus = DMSminus = ATR = 0.0;
    for (int icase = 1; icase <= lookback && icase < n; icase++) {
        // Lines 686-695
        DMplus = bars[icase].high - bars[icase-1].high;
        DMminus = bars[icase-1].low - bars[icase].low;
        if (DMplus >= DMminus)
            DMminus = 0.0;
        else
            DMplus = 0.0;
        if (DMplus < 0.0)
            DMplus = 0.0;
        if (DMminus < 0.0)
            DMminus = 0.0;

        // Lines 696-697
        DMSplus += DMplus;
        DMSminus += DMminus;

        // Lines 698-703
        term = bars[icase].high - bars[icase].low;
        if (bars[icase].high - bars[icase-1].close > term)
            term = bars[icase].high - bars[icase-1].close;
        if (bars[icase-1].close - bars[icase].low > term)
            term = bars[icase-1].close - bars[icase].low;
        ATR += term;

        // Lines 705-708
        DIplus = DMSplus / (ATR + 1.e-10);
        DIminus = DMSminus / (ATR + 1.e-10);
        ADX = fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        output[icase] = 100.0 * ADX;
    }

    // Lines 717-750: Secondary initialization
    for (int icase = lookback + 1; icase < 2 * lookback && icase < n; icase++) {
        // Lines 722-731
        DMplus = bars[icase].high - bars[icase-1].high;
        DMminus = bars[icase-1].low - bars[icase].low;
        if (DMplus >= DMminus)
            DMminus = 0.0;
        else
            DMplus = 0.0;
        if (DMplus < 0.0)
            DMplus = 0.0;
        if (DMminus < 0.0)
            DMminus = 0.0;

        // Lines 732-733
        DMSplus = (lookback - 1.0) / lookback * DMSplus + DMplus;
        DMSminus = (lookback - 1.0) / lookback * DMSminus + DMminus;

        // Lines 734-739
        term = bars[icase].high - bars[icase].low;
        if (bars[icase].high - bars[icase-1].close > term)
            term = bars[icase].high - bars[icase-1].close;
        if (bars[icase-1].close - bars[icase].low > term)
            term = bars[icase-1].close - bars[icase].low;
        ATR = (lookback - 1.0) / lookback * ATR + term;

        // Lines 740-743
        DIplus = DMSplus / (ATR + 1.e-10);
        DIminus = DMSminus / (ATR + 1.e-10);
        ADX += fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        output[icase] = 100.0 * ADX / (icase - lookback + 1);
    }

    // Line 750
    if (2 * lookback - 1 < n) {
        ADX /= lookback;
    }

    // Lines 760-793: Main computation
    for (int icase = 2 * lookback; icase < n; icase++) {
        // Lines 761-770
        DMplus = bars[icase].high - bars[icase-1].high;
        DMminus = bars[icase-1].low - bars[icase].low;
        if (DMplus >= DMminus)
            DMminus = 0.0;
        else
            DMplus = 0.0;
        if (DMplus < 0.0)
            DMplus = 0.0;
        if (DMminus < 0.0)
            DMminus = 0.0;

        // Lines 771-772
        DMSplus = (lookback - 1.0) / lookback * DMSplus + DMplus;
        DMSminus = (lookback - 1.0) / lookback * DMSminus + DMminus;

        // Lines 773-778
        term = bars[icase].high - bars[icase].low;
        if (bars[icase].high - bars[icase-1].close > term)
            term = bars[icase].high - bars[icase-1].close;
        if (bars[icase-1].close - bars[icase].low > term)
            term = bars[icase-1].close - bars[icase].low;
        ATR = (lookback - 1.0) / lookback * ATR + term;

        // Lines 779-780, 786-787, 792
        DIplus = DMSplus / (ATR + 1.e-10);
        DIminus = DMSminus / (ATR + 1.e-10);
        term = fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
        ADX = (lookback - 1.0) / lookback * ADX + term / lookback;
        output[icase] = 100.0 * ADX;
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "EXACT TSSB ADX IMPLEMENTATION TEST\n";
    std::cout << "===================================\n\n";

    // Get expected values from CSV
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");

    // Compute using exact TSSB implementation
    std::vector<double> tssb_exact;
    compute_adx_tssb_exact(ohlcv_bars, 14, tssb_exact);

    // Compute using our library
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::Adx;
    req.name = "ADX_S";
    req.params[0] = 14;
    auto our_result = compute_single_indicator(series, req);

    // Compare at key bars
    std::cout << "Comparison at key bars:\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(14) << "CSV Expected"
              << std::setw(14) << "TSSB Exact"
              << std::setw(14) << "Our Library"
              << std::setw(14) << "Exact-CSV"
              << std::setw(14) << "Our-CSV" << "\n";
    std::cout << std::string(90, '-') << "\n";

    std::vector<int> test_bars = {27, 28, 100, 200, 500, 1000, 1078, 1079, 1080};

    for (int bar : test_bars) {
        double csv_val = expected[bar];
        double exact_val = tssb_exact[bar];
        double our_val = our_result.values[bar];
        double exact_error = exact_val - csv_val;
        double our_error = our_val - csv_val;

        std::cout << std::setw(6) << bar
                  << std::fixed << std::setprecision(6)
                  << std::setw(14) << csv_val
                  << std::setw(14) << exact_val
                  << std::setw(14) << our_val
                  << std::setw(14) << exact_error
                  << std::setw(14) << our_error << "\n";
    }

    // Check if exact matches our implementation
    bool implementations_match = true;
    double max_diff = 0.0;
    for (int i = 0; i < ohlcv_bars.size(); ++i) {
        double diff = std::abs(tssb_exact[i] - our_result.values[i]);
        if (diff > 1e-10) {
            implementations_match = false;
            max_diff = std::max(max_diff, diff);
        }
    }

    std::cout << "\n";
    if (implementations_match) {
        std::cout << "✓ EXACT TSSB and our library produce IDENTICAL results\n";
    } else {
        std::cout << "✗ Implementations differ! Max difference: " << max_diff << "\n";
    }

    // Compute MAE for both
    double exact_mae = 0.0, our_mae = 0.0;
    int count = 0;
    for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            exact_mae += std::abs(tssb_exact[i] - expected[i]);
            our_mae += std::abs(our_result.values[i] - expected[i]);
            count++;
        }
    }

    if (count > 0) {
        exact_mae /= count;
        our_mae /= count;
        std::cout << "\nMAE on CSV data (bars 1078+):\n";
        std::cout << "  TSSB Exact: " << exact_mae << "\n";
        std::cout << "  Our Library: " << our_mae << "\n";
    }

    return 0;
}
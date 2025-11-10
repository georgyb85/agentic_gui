#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>

using namespace tssb;
using namespace tssb::validation;

// Manual ADX computation with detailed output
void compute_adx_verbose(const std::vector<OHLCVBar>& ohlcv, int lookback, int target_bar) {
    std::cout << "\n====================================================================\n";
    std::cout << "VERBOSE ADX COMPUTATION (lookback=" << lookback << ")\n";
    std::cout << "====================================================================\n\n";

    const int front_bad = 2 * lookback - 1;
    std::cout << "front_bad = " << front_bad << "\n";
    std::cout << "target_bar = " << target_bar << " (" << ohlcv[target_bar].date << " " << ohlcv[target_bar].time << ")\n\n";

    // Phase 1: Primary initialization
    double DMSplus = 0.0;
    double DMSminus = 0.0;
    double ATR = 0.0;
    double ADX = 0.0;

    std::cout << "PHASE 1: Bars 1 to " << lookback << "\n";
    std::cout << std::string(80, '-') << "\n";

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

        if (icase >= lookback - 3) {
            std::cout << "Bar " << std::setw(4) << icase
                      << ": DM+=" << std::setw(8) << std::setprecision(4) << DMplus
                      << ", DM-=" << std::setw(8) << DMminus
                      << ", DMS+=" << std::setw(10) << DMSplus
                      << ", DMS-=" << std::setw(10) << DMSminus
                      << ", TR=" << std::setw(8) << tr
                      << ", ATR=" << std::setw(10) << ATR << "\n";
            std::cout << "       DI+=" << std::setw(10) << std::setprecision(6) << DIplus
                      << ", DI-=" << std::setw(10) << DIminus
                      << ", ADX=" << std::setw(10) << ADX
                      << ", output=" << (100.0 * ADX) << "\n";
        }
    }

    std::cout << "\nEnd of Phase 1: DMSplus=" << DMSplus << ", DMSminus=" << DMSminus
              << ", ATR=" << ATR << ", ADX=" << ADX << "\n\n";

    // Phase 2: Secondary initialization
    std::cout << "PHASE 2: Bars " << (lookback + 1) << " to " << (2 * lookback - 1) << "\n";
    std::cout << std::string(80, '-') << "\n";

    for (int icase = lookback + 1; icase < 2 * lookback; ++icase) {
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

        if (icase >= 2 * lookback - 4) {
            std::cout << "Bar " << std::setw(4) << icase
                      << ": DM+=" << std::setw(8) << std::setprecision(4) << DMplus
                      << ", DM-=" << std::setw(8) << DMminus
                      << ", DMS+=" << std::setw(10) << DMSplus
                      << ", DMS-=" << std::setw(10) << DMSminus
                      << ", TR=" << std::setw(8) << tr
                      << ", ATR=" << std::setw(10) << ATR << "\n";
            std::cout << "       DI+=" << std::setw(10) << std::setprecision(6) << DIplus
                      << ", DI-=" << std::setw(10) << DIminus
                      << ", ADX(cumul)=" << std::setw(10) << ADX
                      << ", output=" << (100.0 * ADX / (icase - lookback + 1)) << "\n";
        }
    }

    ADX /= lookback;
    std::cout << "\nEnd of Phase 2: DMSplus=" << DMSplus << ", DMSminus=" << DMSminus
              << ", ATR=" << ATR << ", ADX=" << ADX << "\n\n";

    // Phase 3: Compute target bar
    if (target_bar >= 2 * lookback) {
        std::cout << "PHASE 3: Computing bars " << (2 * lookback) << " to " << target_bar << "\n";
        std::cout << std::string(80, '-') << "\n";

        for (int icase = 2 * lookback; icase <= target_bar; ++icase) {
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
            double term = std::fabs(DIplus - DIminus) / (DIplus + DIminus + 1.e-10);
            ADX = (lookback - 1.0) / lookback * ADX + term / lookback;

            if (icase >= target_bar - 3 || icase == 2 * lookback) {
                std::cout << "Bar " << std::setw(4) << icase << " (" << ohlcv[icase].date << " " << ohlcv[icase].time << ")\n";
                std::cout << "  DM+=" << std::setw(8) << std::setprecision(4) << DMplus
                          << ", DM-=" << std::setw(8) << DMminus
                          << ", DMS+=" << std::setw(10) << DMSplus
                          << ", DMS-=" << std::setw(10) << DMSminus << "\n";
                std::cout << "  TR=" << std::setw(8) << tr
                          << ", ATR=" << std::setw(10) << ATR << "\n";
                std::cout << "  DI+=" << std::setw(10) << std::setprecision(6) << DIplus
                          << ", DI-=" << std::setw(10) << DIminus
                          << ", term=" << std::setw(10) << term << "\n";
                std::cout << "  ADX=" << std::setw(10) << ADX
                          << ", output=" << (100.0 * ADX) << "\n\n";
            }
        }

        std::cout << "FINAL: Bar " << target_bar << " ADX = " << (100.0 * ADX) << "\n";
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

    std::cout << "OHLCV bars: " << ohlcv_bars.size() << "\n";
    std::cout << "CSV bars: " << tssb_bars.size() << "\n";
    std::cout << "First CSV bar: " << tssb_bars[0].date << " " << tssb_bars[0].time << "\n\n";

    // Find matching OHLCV bar for first CSV bar
    size_t first_csv_ohlcv_idx = SIZE_MAX;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (ohlcv_bars[i].date == tssb_bars[0].date && ohlcv_bars[i].time == tssb_bars[0].time) {
            first_csv_ohlcv_idx = i;
            break;
        }
    }

    std::cout << "First CSV bar corresponds to OHLCV bar " << first_csv_ohlcv_idx << "\n\n";

    // Get expected value for ADX_S
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");
    std::cout << "Expected ADX_S at bar " << first_csv_ohlcv_idx << ": "
              << expected[first_csv_ohlcv_idx] << "\n\n";

    // Compute verbose ADX for first CSV bar
    compute_adx_verbose(ohlcv_bars, 14, first_csv_ohlcv_idx);

    // Also compute using our library function
    std::cout << "\n====================================================================\n";
    std::cout << "LIBRARY COMPUTATION\n";
    std::cout << "====================================================================\n\n";

    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::Adx;
    req.name = "ADX_S";
    req.params[0] = 14;

    auto result = compute_single_indicator(series, req);
    if (result.success) {
        std::cout << "Library computed ADX_S at bar " << first_csv_ohlcv_idx << ": "
                  << result.values[first_csv_ohlcv_idx] << "\n";
        std::cout << "Expected: " << expected[first_csv_ohlcv_idx] << "\n";
        std::cout << "Error: " << (result.values[first_csv_ohlcv_idx] - expected[first_csv_ohlcv_idx]) << "\n";
    } else {
        std::cerr << "ERROR: " << result.error_message << "\n";
    }

    return 0;
}

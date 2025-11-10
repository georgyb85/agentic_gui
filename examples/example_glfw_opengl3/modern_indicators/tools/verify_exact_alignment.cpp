#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "VERIFYING EXACT ALIGNMENT\n";
    std::cout << "=========================\n\n";

    std::cout << "OHLCV file has " << ohlcv_bars.size() << " bars\n";
    std::cout << "CSV file has " << tssb_bars.size() << " bars\n\n";

    // Get expected values
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TREND_S100");

    std::cout << "Checking first few CSV bars:\n";
    for (size_t i = 0; i < std::min(size_t(5), tssb_bars.size()); ++i) {
        std::cout << "CSV bar " << i << ": " << tssb_bars[i].date << " " << tssb_bars[i].time;
        auto it = tssb_bars[i].indicators.find("TREND_S100");
        if (it != tssb_bars[i].indicators.end()) {
            std::cout << ", TREND_S100=" << it->second;
        }
        std::cout << "\n";
    }

    std::cout << "\nFinding matching OHLCV bars:\n";
    for (size_t csv_idx = 0; csv_idx < std::min(size_t(5), tssb_bars.size()); ++csv_idx) {
        const auto& csv_bar = tssb_bars[csv_idx];

        // Find matching OHLCV bar
        size_t ohlcv_idx = SIZE_MAX;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (ohlcv_bars[i].date == csv_bar.date && ohlcv_bars[i].time == csv_bar.time) {
                ohlcv_idx = i;
                break;
            }
        }

        std::cout << "CSV bar " << csv_idx << " (" << csv_bar.date << " " << csv_bar.time << ") ";
        if (ohlcv_idx != SIZE_MAX) {
            std::cout << "matches OHLCV bar " << ohlcv_idx << "\n";
            std::cout << "  OHLCV close: " << ohlcv_bars[ohlcv_idx].close << "\n";
            std::cout << "  expected[" << ohlcv_idx << "] = " << expected[ohlcv_idx] << "\n";
        } else {
            std::cout << "NO MATCH FOUND!\n";
        }
    }

    // Now compute indicator
    std::cout << "\n=========================\n";
    std::cout << "COMPUTING INDICATOR\n";
    std::cout << "=========================\n\n";

    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::LinearTrend;
    req.name = "TREND_S100";
    req.params[0] = 10;  // lookback
    req.params[1] = 100; // atr_length

    auto result = compute_single_indicator(series, req);

    int lookback = 10;
    int atr_length = 100;
    int front_bad = std::max(lookback - 1, atr_length);

    std::cout << "Parameters: lookback=" << lookback << ", atr_length=" << atr_length << "\n";
    std::cout << "front_bad = max(" << (lookback-1) << ", " << atr_length << ") = " << front_bad << "\n";
    std::cout << "First computed value at bar: " << front_bad << "\n\n";

    // For first CSV bar (which is OHLCV bar 1078):
    std::cout << "First CSV bar corresponds to OHLCV bar 1078\n";
    std::cout << "To compute TREND for bar 1078 with lookback=10:\n";
    std::cout << "  Trend window: bars [1069...1078]\n";
    std::cout << "  ATR window: bars [979...1078]\n\n";

    std::cout << "Checking what we computed:\n";
    std::cout << "  result.values[1078] = " << result.values[1078] << "\n";
    std::cout << "  expected[1078] = " << expected[1078] << "\n";
    std::cout << "  Error: " << (result.values[1078] - expected[1078]) << "\n\n";

    // Check bars 1069-1078 used for computation
    std::cout << "Bars used in trend window [1069-1078]:\n";
    for (int i = 1069; i <= 1078; ++i) {
        std::cout << "  Bar " << i << ": " << ohlcv_bars[i].date << " " << ohlcv_bars[i].time
                  << ", close=" << ohlcv_bars[i].close << "\n";
    }

    return 0;
}

#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>

using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc245.txt> <BTC245 HM.CSV>\n";
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "TIMESTAMP ALIGNMENT VERIFICATION\n";
    std::cout << "================================================================================\n\n";

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

    std::cout << "OHLCV file: " << ohlcv_bars.size() << " bars\n";
    std::cout << "  First bar: " << ohlcv_bars[0].date << " " << ohlcv_bars[0].time << "\n";
    std::cout << "  Last bar:  " << ohlcv_bars.back().date << " " << ohlcv_bars.back().time << "\n\n";

    std::cout << "CSV file: " << tssb_bars.size() << " bars\n";
    std::cout << "  First bar: " << tssb_bars[0].date << " " << tssb_bars[0].time << "\n";
    std::cout << "  Last bar:  " << tssb_bars.back().date << " " << tssb_bars.back().time << "\n\n";

    // Find where CSV starts in OHLCV
    std::cout << "Finding CSV start in OHLCV...\n";
    size_t csv_start_idx = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (ohlcv_bars[i].date == tssb_bars[0].date &&
            ohlcv_bars[i].time == tssb_bars[0].time) {
            csv_start_idx = i;
            break;
        }
    }

    std::cout << "CSV starts at OHLCV bar index: " << csv_start_idx << "\n";
    std::cout << "  Timestamp: " << ohlcv_bars[csv_start_idx].date << " "
              << ohlcv_bars[csv_start_idx].time << "\n\n";

    std::cout << "Verifying alignment for first 10 CSV rows:\n";
    std::cout << "CSV_Row  OHLCV_Bar  CSV_Timestamp        OHLCV_Timestamp      Match\n";
    std::cout << "-------  ---------  -------------------  -------------------  -----\n";

    for (int i = 0; i < 10 && i < static_cast<int>(tssb_bars.size()); ++i) {
        size_t ohlcv_idx = csv_start_idx + i;
        bool match = (ohlcv_bars[ohlcv_idx].date == tssb_bars[i].date &&
                     ohlcv_bars[ohlcv_idx].time == tssb_bars[i].time);

        std::cout << std::setw(7) << (i + 1) << "  "
                  << std::setw(9) << ohlcv_idx << "  "
                  << tssb_bars[i].date << " " << tssb_bars[i].time << "  "
                  << ohlcv_bars[ohlcv_idx].date << " " << ohlcv_bars[ohlcv_idx].time << "  "
                  << (match ? "YES" : "NO") << "\n";
    }

    std::cout << "\n";
    std::cout << "When computing indicator for CSV row 1 (OHLCV bar " << csv_start_idx << "):\n";
    std::cout << "  For MA_DIFF 10 20 10, we need ATR lookback = 20+10 = 30 bars\n";
    std::cout << "  So we use OHLCV bars " << (csv_start_idx - 30) << " to " << csv_start_idx << "\n";
    std::cout << "  That's bars: " << ohlcv_bars[csv_start_idx - 30].date << " "
              << ohlcv_bars[csv_start_idx - 30].time << " through "
              << ohlcv_bars[csv_start_idx].date << " " << ohlcv_bars[csv_start_idx].time << "\n\n";

    std::cout << "For compression with lookback=250:\n";
    std::cout << "  We need 250 PREVIOUS raw indicator values\n";
    std::cout << "  Raw values are valid starting from OHLCV bar " << 30 << " (after ATR warmup)\n";
    std::cout << "  At CSV row 1 (OHLCV bar " << csv_start_idx << "):\n";
    std::cout << "    Available raw values: bars 30 to " << (csv_start_idx - 1) << "\n";
    std::cout << "    That's " << (csv_start_idx - 30) << " raw values\n";
    std::cout << "    We use the most recent 250: bars " << (csv_start_idx - 250) << " to "
              << (csv_start_idx - 1) << "\n";
    std::cout << "    (excluding current bar " << csv_start_idx << " to avoid future leak)\n\n";

    // Check if alignment function works correctly
    auto aligned = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");
    std::cout << "extract_indicator_aligned returned " << aligned.size() << " values\n";
    std::cout << "  This should equal OHLCV size (" << ohlcv_bars.size() << "): "
              << (aligned.size() == ohlcv_bars.size() ? "YES" : "NO") << "\n\n";

    std::cout << "Checking aligned values:\n";
    std::cout << "  aligned[" << csv_start_idx << "] = " << std::fixed << std::setprecision(4)
              << aligned[csv_start_idx] << " (should match CSV row 1)\n";
    std::cout << "  CSV row 1 MA_DIFF_S = " << tssb_bars[0].indicators["MA_DIFF_S"] << "\n";
    std::cout << "  Match: " << (std::abs(aligned[csv_start_idx] - tssb_bars[0].indicators["MA_DIFF_S"]) < 0.0001 ? "YES" : "NO") << "\n";

    std::cout << "\n================================================================================\n";
    std::cout << "ALIGNMENT CHECK COMPLETE\n";
    std::cout << "================================================================================\n";

    return 0;
}

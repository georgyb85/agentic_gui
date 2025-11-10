#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV> <bar_index>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    int target_bar = std::atoi(argv[3]);

    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Test TGT_115 with ATRdist=0
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::HitOrMiss;
    req.name = "TGT_115";
    req.params[0] = 1;   // Up
    req.params[1] = 1;   // Down
    req.params[2] = 5;   // Cutoff
    req.params[3] = 0;   // ATRdist = 0 (no normalization)

    std::cout << "Tracing Hit or Miss for bar " << target_bar << "\n";
    std::cout << "Parameters: Up=1, Down=1, Cutoff=5, ATRdist=0\n\n";

    // Show current bar info
    std::cout << "Bar " << target_bar << ": "
              << ohlcv_bars[target_bar].date << " " << ohlcv_bars[target_bar].time << "\n";
    std::cout << "  Open:   " << series.open[target_bar] << "\n";
    std::cout << "  High:   " << series.high[target_bar] << "\n";
    std::cout << "  Low:    " << series.low[target_bar] << "\n";
    std::cout << "  Close:  " << series.close[target_bar] << "\n\n";

    // Show forward bars
    std::cout << "Forward bars (tomorrow = " << (target_bar + 1) << " to cutoff = " << (target_bar + 5) << "):\n";
    for (int j = 1; j <= 5 && target_bar + j < (int)ohlcv_bars.size(); ++j) {
        int idx = target_bar + j;
        std::cout << "  Bar " << idx << " (" << ohlcv_bars[idx].date << " " << ohlcv_bars[idx].time << "):\n";
        std::cout << "    Open:  " << series.open[idx] << "\n";
        std::cout << "    High:  " << series.high[idx] << "\n";
        std::cout << "    Low:   " << series.low[idx] << "\n";
        std::cout << "    Close: " << series.close[idx] << "\n";
    }
    std::cout << "\n";

    // Manual computation with current algorithm
    const double current_open = series.open[target_bar];
    const double tomorrow_open = series.open[target_bar + 1];
    const double up_threshold = 1.0;  // Since ATRdist=0, we don't multiply by ATR
    const double down_threshold = 1.0;

    std::cout << "Threshold tracking (from tomorrow's open = " << tomorrow_open << "):\n";
    std::cout << "  Up threshold:   " << tomorrow_open << " + " << up_threshold << " = " << (tomorrow_open + up_threshold) << "\n";
    std::cout << "  Down threshold: " << tomorrow_open << " - " << down_threshold << " = " << (tomorrow_open - down_threshold) << "\n\n";

    bool hit = false;
    double result = 0.0;
    for (int ahead = 1; ahead <= 5 && target_bar + ahead < (int)ohlcv_bars.size(); ++ahead) {
        int idx = target_bar + ahead;
        double move_to_high = series.high[idx] - tomorrow_open;
        double move_to_low = series.low[idx] - tomorrow_open;

        std::cout << "  Bar " << idx << " (ahead=" << ahead << "):\n";
        std::cout << "    Move to high: " << move_to_high << " (high=" << series.high[idx] << ")\n";
        std::cout << "    Move to low:  " << move_to_low << " (low=" << series.low[idx] << ")\n";

        if (move_to_high >= up_threshold) {
            result = tomorrow_open - current_open;
            std::cout << "    --> HIT upward threshold!\n";
            std::cout << "    Current algorithm: result = tomorrow_open - current_open = " << result << "\n";
            std::cout << "    Correct algorithm: result = open[" << idx << "] - current_open = "
                      << (series.open[idx] - current_open) << "\n";
            hit = true;
            break;
        }
        if (move_to_low <= -down_threshold) {
            result = tomorrow_open - current_open;
            std::cout << "    --> HIT downward threshold!\n";
            std::cout << "    Current algorithm: result = tomorrow_open - current_open = " << result << "\n";
            std::cout << "    Correct algorithm: result = open[" << idx << "] - current_open = "
                      << (series.open[idx] - current_open) << "\n";
            hit = true;
            break;
        }
    }

    if (!hit) {
        int final_idx = target_bar + 5;
        result = series.close[final_idx] - current_open;
        std::cout << "  No threshold hit.\n";
        std::cout << "    result = final_close - current_open = "
                  << series.close[final_idx] << " - " << current_open << " = " << result << "\n";
    }

    std::cout << "\nExpected (from CSV): ";
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "TGT_115");
    std::cout << expected[target_bar] << "\n";
    std::cout << "Computed: " << result << "\n";
    std::cout << "Error: " << (result - expected[target_bar]) << "\n";

    return 0;
}

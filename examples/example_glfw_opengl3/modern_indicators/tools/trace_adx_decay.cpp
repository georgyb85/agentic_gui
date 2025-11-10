#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "TRACING ADX DECAY FROM BAR 27 TO 1078\n";
    std::cout << "======================================\n\n";

    // Compute ADX using our library
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::Adx;
    req.name = "ADX_S";
    req.params[0] = 14;

    auto result = compute_single_indicator(series, req);

    // Show ADX values at key points
    std::cout << "ADX_S values at key milestones:\n";
    std::cout << std::setw(8) << "Bar"
              << std::setw(12) << "ADX"
              << std::setw(20) << "Comment" << "\n";
    std::cout << std::string(40, '-') << "\n";

    std::vector<int> milestones = {27, 28, 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1078};

    for (int bar : milestones) {
        std::string comment = "";
        if (bar == 27) comment = "(end phase 2)";
        if (bar == 28) comment = "(start phase 3)";
        if (bar == 1078) comment = "(CSV starts)";

        std::cout << std::setw(8) << bar
                  << std::fixed << std::setprecision(4)
                  << std::setw(12) << result.values[bar]
                  << std::setw(20) << comment << "\n";
    }

    // Compute average ADX in different regions
    std::cout << "\n\nAVERAGE ADX IN DIFFERENT REGIONS:\n";
    std::cout << std::string(40, '-') << "\n";

    std::vector<std::pair<int, int>> regions = {
        {28, 100},
        {100, 200},
        {200, 500},
        {500, 1000},
        {1000, 1078}
    };

    for (const auto& [start, end] : regions) {
        double sum = 0;
        int count = 0;
        for (int i = start; i < end && i < ohlcv_bars.size(); ++i) {
            sum += result.values[i];
            count++;
        }
        double avg = count > 0 ? sum / count : 0;
        std::cout << "Bars " << std::setw(4) << start << "-" << std::setw(4) << end
                  << ": avg=" << std::fixed << std::setprecision(4) << avg << "\n";
    }

    // Look for any sudden changes
    std::cout << "\n\nLARGEST CHANGES IN ADX:\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << std::setw(8) << "Bar"
              << std::setw(12) << "ADX"
              << std::setw(12) << "Change"
              << std::setw(12) << "% Change" << "\n";

    std::vector<std::tuple<int, double, double>> changes;
    for (int i = 28; i < 1078; ++i) {
        double change = result.values[i] - result.values[i-1];
        double pct_change = 100.0 * change / result.values[i-1];
        changes.push_back({i, change, pct_change});
    }

    // Sort by absolute change
    std::sort(changes.begin(), changes.end(),
        [](const auto& a, const auto& b) {
            return std::abs(std::get<1>(a)) > std::abs(std::get<1>(b));
        });

    // Show top 10 changes
    for (int i = 0; i < 10 && i < changes.size(); ++i) {
        auto [bar, change, pct_change] = changes[i];
        std::cout << std::setw(8) << bar
                  << std::fixed << std::setprecision(4)
                  << std::setw(12) << result.values[bar]
                  << std::setw(12) << change
                  << std::setw(12) << pct_change << "%\n";
    }

    // Check volatility patterns
    std::cout << "\n\nVOLATILITY ANALYSIS:\n";
    std::cout << std::string(60, '-') << "\n";

    // Check True Range at different points
    std::vector<int> check_bars = {100, 500, 1000, 1078};
    for (int bar : check_bars) {
        double tr = ohlcv_bars[bar].high - ohlcv_bars[bar].low;
        tr = std::max(tr, ohlcv_bars[bar].high - ohlcv_bars[bar-1].close);
        tr = std::max(tr, ohlcv_bars[bar-1].close - ohlcv_bars[bar].low);

        double price_level = ohlcv_bars[bar].close;
        double tr_pct = 100.0 * tr / price_level;

        std::cout << "Bar " << std::setw(4) << bar
                  << ": TR=" << std::fixed << std::setprecision(2) << tr
                  << ", Price=" << price_level
                  << ", TR%=" << std::setprecision(4) << tr_pct << "%\n";
    }

    return 0;
}
#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt>\n";
        return 1;
    }

    auto ohlcv = OHLCVParser::parse_file(argv[1]);
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv);

    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::LinearTrend;
    req.params[0] = 10;
    req.params[1] = 100;
    req.name = "TREND_S100";

    auto result = compute_single_indicator(series, req);

    std::cout << "Checking where our computed values start (should be at front_bad=100):\n\n";
    std::cout << std::fixed << std::setprecision(6);

    for (size_t i = 95; i <= 110; i++) {
        std::cout << "Bar " << std::setw(4) << i << " (" << ohlcv[i].date << " " << ohlcv[i].time << "): "
                  << std::setw(12) << result.values[i];
        if (result.values[i] == 0.0) {
            std::cout << " (zero - before front_bad)";
        }
        std::cout << "\n";
    }

    std::cout << "\nChecking bar 1078 (where CSV starts):\n";
    std::cout << "Bar 1078 (" << ohlcv[1078].date << " " << ohlcv[1078].time << "): "
              << result.values[1078] << "\n";

    return 0;
}

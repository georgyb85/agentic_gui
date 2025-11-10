
#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    // Optional order parameter: 0 = down first (default), 1 = up first
    double order_param = (argc >= 3) ? std::atof(argv[2]) : 0.0;

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Compute all three indicators
    SingleIndicatorRequest req115, req315, req555;

    req115.id = SingleIndicatorId::HitOrMiss;
    req115.name = "TGT_115";
    req115.params[0] = 1; req115.params[1] = 1; req115.params[2] = 5; req115.params[3] = 0; req115.params[4] = order_param;

    req315.id = SingleIndicatorId::HitOrMiss;
    req315.name = "TGT_315";
    req315.params[0] = 3; req315.params[1] = 1; req315.params[2] = 5; req315.params[3] = 0; req315.params[4] = order_param;

    req555.id = SingleIndicatorId::HitOrMiss;
    req555.name = "TGT_555";
    req555.params[0] = 5; req555.params[1] = 5; req555.params[2] = 5; req555.params[3] = 0; req555.params[4] = order_param;

    auto result115 = compute_single_indicator(series, req115);
    auto result315 = compute_single_indicator(series, req315);
    auto result555 = compute_single_indicator(series, req555);

    // Output all values
    std::cout << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        std::cout << i << " "
                  << result115.values[i] << " "
                  << result315.values[i] << " "
                  << result555.values[i] << "\n";
    }

    return 0;
}

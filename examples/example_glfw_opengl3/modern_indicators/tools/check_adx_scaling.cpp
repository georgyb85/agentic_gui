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

    std::cout << "Checking ADX scaling factor across first 20 bars\n";
    std::cout << "=================================================\n\n";

    // Get expected values
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "ADX_S");

    // Compute our values
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::Adx;
    req.name = "ADX_S";
    req.params[0] = 14;

    auto result = compute_single_indicator(series, req);

    if (!result.success) {
        std::cerr << "ERROR: " << result.error_message << "\n";
        return 1;
    }

    // Find first valid bar in CSV
    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
            first_valid = i;
            break;
        }
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << std::setw(6) << "Bar"
              << std::setw(14) << "Expected"
              << std::setw(14) << "Computed"
              << std::setw(14) << "Error"
              << std::setw(12) << "Ratio\n";
    std::cout << std::string(60, '-') << "\n";

    for (int i = 0; i < 20 && first_valid + i < ohlcv_bars.size(); ++i) {
        size_t idx = first_valid + i;
        if (std::isfinite(expected[idx]) && std::isfinite(result.values[idx])) {
            double error = result.values[idx] - expected[idx];
            double ratio = result.values[idx] / expected[idx];

            std::cout << std::setw(6) << idx
                      << std::setw(14) << expected[idx]
                      << std::setw(14) << result.values[idx]
                      << std::setw(14) << error
                      << std::setw(12) << ratio << "\n";
        }
    }

    return 0;
}

#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
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
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    std::cout << "====================================================================\n";
    std::cout << "CHECKING INDICATOR IMPLEMENTATIONS\n";
    std::cout << "====================================================================\n\n";

    struct IndicatorTest {
        std::string name;
        std::string csv_col;
        SingleIndicatorId id;
        std::vector<double> params;
    };

    std::vector<IndicatorTest> tests = {
        {"MAX_CVR", "MAX_CVR", SingleIndicatorId::MaxChangeVarianceRatio, {10, 3, 20}},
        {"CMMA_S", "CMMA_S", SingleIndicatorId::CloseMinusMovingAverage, {10, 250, 1}},
        {"ATR_RATIO_S", "ATR_RATIO_S", SingleIndicatorId::AtrRatio, {10, 2.5}},
        {"ATR_RATIO_M", "ATR_RATIO_M", SingleIndicatorId::AtrRatio, {50, 5.0}},
        {"PCO_10_20", "PCO_10_20", SingleIndicatorId::PriceChangeOscillator, {10, 20}},
        {"PVR_10_20", "PVR_10_20", SingleIndicatorId::PriceVarianceRatio, {10, 20}},
        {"VOL_MAX_PS", "VOL_MAX_PS", SingleIndicatorId::MaxPriceVarianceRatio, {1, 20, 50}},
        {"BOL_WIDTH_S", "BOL_WIDTH_S", SingleIndicatorId::BollingerWidth, {20}},
        {"BOL_WIDTH_M", "BOL_WIDTH_M", SingleIndicatorId::BollingerWidth, {60}},
        {"VOL_MOM_S", "VOL_MOM_S", SingleIndicatorId::VolumeMomentum, {10, 5}},
    };

    std::cout << std::setw(15) << "Indicator"
              << std::setw(12) << "Status"
              << std::setw(50) << "Details\n";
    std::cout << std::string(77, '-') << "\n";

    for (const auto& test : tests) {
        SingleIndicatorRequest req;
        req.id = test.id;
        req.name = test.name;
        for (size_t i = 0; i < test.params.size(); ++i) {
            req.params[i] = test.params[i];
        }

        auto result = compute_single_indicator(series, req);

        std::cout << std::setw(15) << test.name;

        if (!result.success) {
            std::cout << std::setw(12) << "❌ NOT IMPL"
                      << std::setw(50) << result.error_message << "\n";
        } else {
            // Check if result has valid values
            int valid_count = 0;
            for (size_t i = 0; i < result.values.size(); ++i) {
                if (std::isfinite(result.values[i]) && std::fabs(result.values[i]) > 1e-10) {
                    valid_count++;
                }
            }

            if (valid_count > 100) {
                std::cout << std::setw(12) << "✓ IMPL"
                          << std::setw(50) << ("Valid values: " + std::to_string(valid_count)) << "\n";
            } else {
                std::cout << std::setw(12) << "⚠ IMPL?"
                          << std::setw(50) << ("Only " + std::to_string(valid_count) + " valid values") << "\n";
            }
        }
    }

    std::cout << "\nLegend:\n";
    std::cout << "  ✓ IMPL     - Implemented and returns valid data\n";
    std::cout << "  ❌ NOT IMPL - Not implemented or returns error\n";
    std::cout << "  ⚠ IMPL?    - Implemented but suspicious (too few valid values)\n";

    return 0;
}

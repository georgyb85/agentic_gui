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

    std::cout << "====================================================================\n";
    std::cout << "VOLUME INDICATOR VALIDATION\n";
    std::cout << "====================================================================\n\n";

    // Convert to series
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    struct IndicatorTest {
        std::string name;
        std::string csv_col;
        SingleIndicatorId id;
        std::vector<double> params;
        std::string status;
    };

    std::vector<IndicatorTest> tests = {
        // Volume Weighted MA Ratio: VOLUME WEIGHTED MA OVER MA <length>
        {"VWMA_RATIO_S", "VWMA_RATIO_S", SingleIndicatorId::VolumeWeightedMaRatio, {20}, "implemented"},
        {"VWMA_RATIO_M", "VWMA_RATIO_M", SingleIndicatorId::VolumeWeightedMaRatio, {50}, "implemented"},
        {"VWMA_RATIO_L", "VWMA_RATIO_L", SingleIndicatorId::VolumeWeightedMaRatio, {100}, "implemented"},

        // Price Volume Fit: PRICE VOLUME FIT <length>
        {"PV_FIT_S", "PV_FIT_S", SingleIndicatorId::PriceVolumeFit, {20}, "implemented"},
        {"PV_FIT_M", "PV_FIT_M", SingleIndicatorId::PriceVolumeFit, {60}, "implemented"},

        // Volume Momentum: VOLUME MOMENTUM <short_length> <multiplier>
        {"VOL_MOM_S", "VOL_MOM_S", SingleIndicatorId::VolumeMomentum, {10, 5}, "implemented"},
        {"VOL_MOM_L", "VOL_MOM_L", SingleIndicatorId::VolumeMomentum, {50, 4}, "implemented"},

        // NOT YET IMPLEMENTED:
        // {"MAX_CVR", "MAX_CVR", SingleIndicatorId::MaxChangeVarianceRatio, {10, 3, 20}, "not_impl"},
        // {"ATR_RATIO_S", "ATR_RATIO_S", SingleIndicatorId::AtrRatio, {10, 2.5}, "not_impl"},
        // {"ATR_RATIO_M", "ATR_RATIO_M", SingleIndicatorId::AtrRatio, {50, 5.0}, "not_impl"},
        // {"ATR_RATIO_L", "ATR_RATIO_L", SingleIndicatorId::AtrRatio, {120, 5.0}, "not_impl"},
    };

    for (const auto& test : tests) {
        std::cout << "====================================================================\n";
        std::cout << test.name << " (";
        for (size_t i = 0; i < test.params.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << test.params[i];
        }
        std::cout << ")\n";
        std::cout << "====================================================================\n";

        // Get expected values
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_col);

        // Compute our values
        SingleIndicatorRequest req;
        req.id = test.id;
        req.name = test.name;
        for (size_t i = 0; i < test.params.size(); ++i) {
            req.params[i] = test.params[i];
        }

        auto result = compute_single_indicator(series, req);

        if (!result.success) {
            std::cerr << "ERROR computing " << test.name << ": " << result.error_message << "\n";
            continue;
        }

        // Find first valid bar
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::cout << "First valid CSV bar: " << first_valid << " (date: "
                  << ohlcv_bars[first_valid].date << " " << ohlcv_bars[first_valid].time << ")\n\n";

        // Show first 10 comparisons
        std::cout << "First 10 bars comparison:\n";
        std::cout << std::setw(8) << "Bar"
                  << std::setw(12) << "Expected"
                  << std::setw(12) << "Computed"
                  << std::setw(12) << "Error\n";
        std::cout << std::string(44, '-') << "\n";

        for (int j = 0; j < 10 && first_valid + j < ohlcv_bars.size(); ++j) {
            size_t i = first_valid + j;
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
                double error = result.values[i] - expected[i];

                std::cout << std::setw(8) << i
                          << std::setw(12) << std::setprecision(6) << std::fixed << expected[i]
                          << std::setw(12) << result.values[i]
                          << std::setw(12) << error << "\n";
            }
        }

        // Compute statistics for all bars
        double sum_abs_error = 0.0;
        double max_abs_error = 0.0;
        int valid_count = 0;
        int under_0_1 = 0;
        int under_1_0 = 0;

        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
                double abs_error = std::fabs(result.values[i] - expected[i]);
                sum_abs_error += abs_error;
                if (abs_error > max_abs_error) max_abs_error = abs_error;
                if (abs_error < 0.1) under_0_1++;
                if (abs_error < 1.0) under_1_0++;
                valid_count++;
            }
        }

        double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;

        std::cout << "\nSummary:\n";
        std::cout << "  Valid bars: " << valid_count << "\n";
        std::cout << "  MAE: " << std::setprecision(4) << mae << "\n";
        std::cout << "  Max Error: " << std::setprecision(4) << max_abs_error << "\n";
        std::cout << "  Bars with error < 0.1: " << under_0_1
                  << " (" << (100.0 * under_0_1 / valid_count) << "%)\n";
        std::cout << "  Bars with error < 1.0: " << under_1_0
                  << " (" << (100.0 * under_1_0 / valid_count) << "%)\n";

        if (mae < 0.01) {
            std::cout << "  Status: ✓✓✓ PERFECT MATCH!\n";
        } else if (mae < 0.1) {
            std::cout << "  Status: ✓✓ EXCELLENT\n";
        } else if (mae < 1.0) {
            std::cout << "  Status: ✓ GOOD\n";
        } else {
            std::cout << "  Status: ✗ HIGH ERROR\n";
        }
        std::cout << "\n";
    }

    std::cout << "\n====================================================================\n";
    std::cout << "NOT YET IMPLEMENTED:\n";
    std::cout << "====================================================================\n";
    std::cout << "- MAX_CVR (MAX CHANGE VARIANCE RATIO 10 3 20)\n";
    std::cout << "- ATR_RATIO_S (ATR RATIO 10 2.5)\n";
    std::cout << "- ATR_RATIO_M (ATR RATIO 50 5.0)\n";
    std::cout << "- ATR_RATIO_L (ATR RATIO 120 5.0)\n";
    std::cout << "\nNOW IMPLEMENTED:\n";
    std::cout << "+ PV_FIT_S (PRICE VOLUME FIT 20)\n";
    std::cout << "+ PV_FIT_M (PRICE VOLUME FIT 60)\n";
    std::cout << "+ VOL_MOM_S (VOLUME MOMENTUM 10 5)\n";
    std::cout << "+ VOL_MOM_L (VOLUME MOMENTUM 50 4)\n";

    return 0;
}

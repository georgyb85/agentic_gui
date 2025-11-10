#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MultiIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

struct IndicatorTest {
    std::string csv_name;
    std::string our_name;
    SingleIndicatorId id;
    std::vector<double> params;
};

void test_indicator(const SingleMarketSeries& series,
                   const std::vector<OHLCVBar>& ohlcv_bars,
                   const std::vector<TSBBIndicatorBar>& tssb_bars,
                   const IndicatorTest& test)
{
    // Get expected values
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_name);

    // Compute our values
    SingleIndicatorRequest req;
    req.id = test.id;
    req.name = test.our_name;
    for (size_t i = 0; i < test.params.size() && i < 8; ++i) {
        req.params[i] = test.params[i];
    }

    auto result = compute_single_indicator(series, req);

    if (!result.success) {
        std::cerr << "ERROR computing " << test.our_name << ": " << result.error_message << "\n";
        return;
    }

    // Find first valid bar
    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            first_valid = i;
            break;
        }
    }

    // Compute statistics
    double sum_abs_error = 0.0;
    double max_abs_error = 0.0;
    int valid_count = 0;
    int under_0_001 = 0;
    int under_0_01 = 0;
    int under_0_1 = 0;

    for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
            double abs_error = std::fabs(result.values[i] - expected[i]);
            sum_abs_error += abs_error;
            if (abs_error > max_abs_error) max_abs_error = abs_error;
            if (abs_error < 0.001) under_0_001++;
            if (abs_error < 0.01) under_0_01++;
            if (abs_error < 0.1) under_0_1++;
            valid_count++;
        }
    }

    double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;

    std::cout << std::left << std::setw(15) << test.csv_name
              << "  MAE: " << std::setw(10) << std::setprecision(6) << std::fixed << mae
              << "  Max: " << std::setw(10) << max_abs_error
              << "  Valid: " << std::setw(5) << valid_count;

    if (mae < 0.001) {
        std::cout << "  ✓✓✓ PERFECT";
    } else if (mae < 0.01) {
        std::cout << "  ✓✓ EXCELLENT";
    } else if (mae < 0.1) {
        std::cout << "  ✓ GOOD";
    } else {
        std::cout << "  ✗ HIGH ERROR";
    }
    std::cout << "\n";

    // Show first 3 bars for verification
    if (mae > 0.001) {
        std::cout << "  First 3 bars:\n";
        for (int j = 0; j < 3 && first_valid + j < ohlcv_bars.size(); ++j) {
            size_t i = first_valid + j;
            if (std::isfinite(expected[i]) && std::isfinite(result.values[i])) {
                std::cout << "    Bar " << i << ": Expected=" << expected[i]
                          << ", Computed=" << result.values[i]
                          << ", Error=" << (result.values[i] - expected[i]) << "\n";
            }
        }
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

    std::cout << "====================================================================\n";
    std::cout << "VERIFYING 'WORKING' INDICATORS\n";
    std::cout << "====================================================================\n\n";
    std::cout << "OHLCV bars: " << ohlcv_bars.size() << "\n";
    std::cout << "CSV bars: " << tssb_bars.size() << "\n\n";

    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Parameters from var.txt
    std::vector<IndicatorTest> tests = {
        // RSI indicators: RSI <length>
        {"RSI_S", "RSI_S", SingleIndicatorId::RSI, {10}},
        {"RSI_M", "RSI_M", SingleIndicatorId::RSI, {50}},
        {"RSI_L", "RSI_L", SingleIndicatorId::RSI, {120}},

        // Detrended RSI: DETRENDED RSI <short_len> <long_len> <atr_len>
        {"DTR_RSI_M", "DTR_RSI_M", SingleIndicatorId::DetrendedRsi, {5, 20, 100}},
        {"DTR_RSI_L", "DTR_RSI_L", SingleIndicatorId::DetrendedRsi, {5, 20, 200}},

        // MA_DIFF indicators: MA DIFFERENCE <short_ma> <long_ma> <atr_len>
        {"MA_DIFF_S", "MA_DIFF_S", SingleIndicatorId::MovingAverageDifference, {10, 20, 10}},
        {"MA_DIFF_M", "MA_DIFF_M", SingleIndicatorId::MovingAverageDifference, {20, 50, 20}},
        {"MA_DIFF_L", "MA_DIFF_L", SingleIndicatorId::MovingAverageDifference, {50, 200, 50}},

        // Price Variance Ratios: PRICE VARIANCE RATIO <short_len> <long_len>
        {"PVR_10_20", "PVR_10_20", SingleIndicatorId::PriceVarianceRatio, {10, 20}},
        {"PVR_10_3", "PVR_10_3", SingleIndicatorId::PriceVarianceRatio, {10, 3}},
        {"PVR_20_4", "PVR_20_4", SingleIndicatorId::PriceVarianceRatio, {20, 4}},

        // Change Variance Ratios: CHANGE VARIANCE RATIO <short_len> <long_len>
        {"CVR_S_2", "CVR_S_2", SingleIndicatorId::ChangeVarianceRatio, {10, 2}},
        {"CVR_10_3", "CVR_10_3", SingleIndicatorId::ChangeVarianceRatio, {10, 3}},
    };

    std::cout << "RSI INDICATORS:\n";
    std::cout << std::string(70, '-') << "\n";
    for (int i = 0; i < 3; ++i) {
        test_indicator(series, ohlcv_bars, tssb_bars, tests[i]);
    }

    std::cout << "\nDETRENDED RSI:\n";
    std::cout << std::string(70, '-') << "\n";
    for (int i = 3; i < 5; ++i) {
        test_indicator(series, ohlcv_bars, tssb_bars, tests[i]);
    }

    std::cout << "\nMA_DIFF INDICATORS:\n";
    std::cout << std::string(70, '-') << "\n";
    for (int i = 5; i < 8; ++i) {
        test_indicator(series, ohlcv_bars, tssb_bars, tests[i]);
    }

    std::cout << "\nPRICE-VOLUME RATIOS:\n";
    std::cout << std::string(70, '-') << "\n";
    for (int i = 8; i < 11; ++i) {
        test_indicator(series, ohlcv_bars, tssb_bars, tests[i]);
    }

    std::cout << "\nCLOSE-VOLUME RATIOS:\n";
    std::cout << std::string(70, '-') << "\n";
    for (int i = 11; i < 13; ++i) {
        test_indicator(series, ohlcv_bars, tssb_bars, tests[i]);
    }

    std::cout << "\n====================================================================\n";

    return 0;
}

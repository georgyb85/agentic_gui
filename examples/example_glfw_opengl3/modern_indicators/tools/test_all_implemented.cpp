#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>

using namespace tssb;
using namespace tssb::validation;

struct TestSpec {
    std::string csv_name;
    SingleIndicatorId id;
    std::vector<double> params;
};

void test_indicator(const SingleMarketSeries& series, const std::vector<OHLCVBar>& ohlcv_bars,
                    const std::vector<TSBBIndicatorBar>& tssb_bars, const TestSpec& spec) {
    std::cout << "\n" << spec.csv_name << ":\n";
    std::cout << std::string(50, '-') << "\n";

    SingleIndicatorRequest req;
    req.id = spec.id;
    req.name = spec.csv_name;
    for (size_t i = 0; i < spec.params.size(); ++i) {
        req.params[i] = spec.params[i];
    }

    auto result = tssb::compute_single_indicator(series, req);
    if (!result.success) {
        std::cerr << "ERROR: " << result.error_message << "\n";
        return;
    }

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, spec.csv_name);

    size_t csv_start = 1078;

    // Compute MAE
    double sum_abs_error = 0;
    int count = 0;
    double max_error = 0;
    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;
        if (std::isfinite(result.values[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
            double error = std::abs(result.values[ohlcv_idx] - expected[ohlcv_idx]);
            sum_abs_error += error;
            max_error = std::max(max_error, error);
            ++count;
        }
    }

    double mae = count > 0 ? sum_abs_error / count : 0;

    // Error distribution
    int under_0_1 = 0, under_1_0 = 0, under_5_0 = 0;
    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;
        if (std::isfinite(result.values[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
            double error = std::abs(result.values[ohlcv_idx] - expected[ohlcv_idx]);
            if (error < 0.1) under_0_1++;
            if (error < 1.0) under_1_0++;
            if (error < 5.0) under_5_0++;
        }
    }

    // Show first 5 values
    std::cout << "First 5 values:\n";
    for (int i = 0; i < 5 && i < count; ++i) {
        size_t idx = csv_start + i;
        double error = result.values[idx] - expected[idx];
        std::cout << "  Bar " << std::setw(4) << idx
                  << ": TSSB=" << std::fixed << std::setprecision(4) << std::setw(10) << expected[idx]
                  << ", Ours=" << std::setw(10) << result.values[idx]
                  << ", Err=" << std::setw(8) << error << "\n";
    }

    std::cout << "\nMAE: " << std::setprecision(6) << mae;
    std::cout << ", Max Error: " << std::setprecision(6) << max_error;
    std::cout << ", <0.1: " << (100.0*under_0_1/count) << "%";
    std::cout << ", <1.0: " << (100.0*under_1_0/count) << "%\n";

    // Status
    if (mae < 0.1) {
        std::cout << "✓ PERFECT\n";
    } else if (mae < 1.0) {
        std::cout << "✓ EXCELLENT\n";
    } else if (mae < 5.0) {
        std::cout << "✓ GOOD\n";
    } else {
        std::cout << "⚠ NEEDS REVIEW (MAE >= 5.0)\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc245.txt> <BTC245 HM.CSV>\n";
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

    SingleMarketSeries series;
    series.open.resize(ohlcv_bars.size());
    series.high.resize(ohlcv_bars.size());
    series.low.resize(ohlcv_bars.size());
    series.close.resize(ohlcv_bars.size());
    series.volume.resize(ohlcv_bars.size());

    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        series.open[i] = ohlcv_bars[i].open;
        series.high[i] = ohlcv_bars[i].high;
        series.low[i] = ohlcv_bars[i].low;
        series.close[i] = ohlcv_bars[i].close;
        series.volume[i] = ohlcv_bars[i].volume;
    }

    std::cout << "=========================================================================\n";
    std::cout << "COMPREHENSIVE INDICATOR VALIDATION\n";
    std::cout << "=========================================================================\n";
    std::cout << "OHLCV bars: " << ohlcv_bars.size() << "\n";
    std::cout << "CSV bars: " << tssb_bars.size() << "\n";

    // Define all test specifications
    std::vector<TestSpec> tests;

    // RSI (3)
    tests.push_back({"RSI_S", SingleIndicatorId::RSI, {10}});
    tests.push_back({"RSI_M", SingleIndicatorId::RSI, {50}});
    tests.push_back({"RSI_L", SingleIndicatorId::RSI, {120}});

    // DETRENDED_RSI (2)
    tests.push_back({"DTR_RSI_M", SingleIndicatorId::DetrendedRsi, {5, 20, 100}});
    tests.push_back({"DTR_RSI_L", SingleIndicatorId::DetrendedRsi, {5, 20, 200}});

    // MA_DIFF (3)
    tests.push_back({"MA_DIFF_S", SingleIndicatorId::MovingAverageDifference, {10, 20, 10}});
    tests.push_back({"MA_DIFF_M", SingleIndicatorId::MovingAverageDifference, {20, 50, 20}});
    tests.push_back({"MA_DIFF_L", SingleIndicatorId::MovingAverageDifference, {50, 200, 50}});

    // TREND (5)
    tests.push_back({"TREND_S100", SingleIndicatorId::LinearTrend, {10, 100}});
    tests.push_back({"TREND_M100", SingleIndicatorId::LinearTrend, {50, 100}});
    tests.push_back({"TREND_L100", SingleIndicatorId::LinearTrend, {120, 100}});
    tests.push_back({"TREND_M250", SingleIndicatorId::LinearTrend, {50, 250}});
    tests.push_back({"TREND_L250", SingleIndicatorId::LinearTrend, {120, 250}});

    // CUBIC_TREND (2)
    tests.push_back({"CUBIC_TREND_S", SingleIndicatorId::CubicTrend, {10, 100}});
    tests.push_back({"CUBIC_TREND_L", SingleIndicatorId::CubicTrend, {60, 250}});

    // CLOSE_MINUS_MA (3)
    tests.push_back({"CMMA_S", SingleIndicatorId::CloseMinusMovingAverage, {10, 250}});
    tests.push_back({"CMMA_M", SingleIndicatorId::CloseMinusMovingAverage, {50, 250}});
    tests.push_back({"CMMA_L", SingleIndicatorId::CloseMinusMovingAverage, {120, 250}});

    // PRICE_CHANGE_OSCILLATOR (1)
    tests.push_back({"PCO_10_20", SingleIndicatorId::PriceChangeOscillator, {10, 20}});

    // ADX (3)
    tests.push_back({"ADX_S", SingleIndicatorId::Adx, {14}});
    tests.push_back({"ADX_M", SingleIndicatorId::Adx, {50}});
    tests.push_back({"ADX_L", SingleIndicatorId::Adx, {120}});

    // PRICE_VARIANCE_RATIO (3)
    tests.push_back({"PVR_10_20", SingleIndicatorId::PriceVarianceRatio, {10, 20}});
    tests.push_back({"PVR_10_3", SingleIndicatorId::PriceVarianceRatio, {10, 3}});
    tests.push_back({"PVR_20_4", SingleIndicatorId::PriceVarianceRatio, {20, 4}});

    // CHANGE_VARIANCE_RATIO (2)
    tests.push_back({"CVR_S_2", SingleIndicatorId::ChangeVarianceRatio, {10, 2}});
    tests.push_back({"CVR_10_3", SingleIndicatorId::ChangeVarianceRatio, {10, 3}});

    std::cout << "\n=========================================================================\n";
    std::cout << "TESTING " << tests.size() << " INDICATORS\n";
    std::cout << "=========================================================================\n";

    int perfect_count = 0;
    int excellent_count = 0;
    int good_count = 0;
    int needs_review_count = 0;

    for (const auto& test : tests) {
        test_indicator(series, ohlcv_bars, tssb_bars, test);

        // Count based on status (re-compute MAE to categorize)
        SingleIndicatorRequest req;
        req.id = test.id;
        req.name = test.csv_name;
        for (size_t i = 0; i < test.params.size(); ++i) {
            req.params[i] = test.params[i];
        }
        auto result = tssb::compute_single_indicator(series, req);
        if (result.success) {
            auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_name);
            double sum_abs_error = 0;
            int count = 0;
            for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
                size_t ohlcv_idx = 1078 + csv_idx;
                if (std::isfinite(result.values[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
                    sum_abs_error += std::abs(result.values[ohlcv_idx] - expected[ohlcv_idx]);
                    ++count;
                }
            }
            double mae = count > 0 ? sum_abs_error / count : 0;
            if (mae < 0.1) perfect_count++;
            else if (mae < 1.0) excellent_count++;
            else if (mae < 5.0) good_count++;
            else needs_review_count++;
        }
    }

    std::cout << "\n=========================================================================\n";
    std::cout << "SUMMARY\n";
    std::cout << "=========================================================================\n";
    std::cout << "Total tested: " << tests.size() << "\n";
    std::cout << "Perfect (MAE < 0.1): " << perfect_count << "\n";
    std::cout << "Excellent (MAE < 1.0): " << excellent_count << "\n";
    std::cout << "Good (MAE < 5.0): " << good_count << "\n";
    std::cout << "Needs review (MAE >= 5.0): " << needs_review_count << "\n";
    std::cout << "=========================================================================\n";

    return 0;
}

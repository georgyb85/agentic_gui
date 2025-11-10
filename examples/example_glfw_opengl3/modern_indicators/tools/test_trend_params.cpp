#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

void test_params(const SingleMarketSeries& series, const std::vector<OHLCVBar>& ohlcv_bars,
                 const std::vector<TSBBIndicatorBar>& tssb_bars, const std::string& name,
                 int p1, int p2) {
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::LinearTrend;
    req.name = name;
    req.params[0] = p1;
    req.params[1] = p2;

    auto result = tssb::compute_single_indicator(series, req);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);

    size_t csv_start = 1078;
    double sum_abs_error = 0;
    int count = 0;
    for (size_t i = 0; i < 10; ++i) {
        size_t idx = csv_start + i;
        if (std::isfinite(result.values[idx]) && std::isfinite(expected[idx])) {
            sum_abs_error += std::abs(result.values[idx] - expected[idx]);
            count++;
        }
    }
    double mae = count > 0 ? sum_abs_error / count : 999;

    std::cout << name << " (p1=" << p1 << ", p2=" << p2 << "): MAE=" << std::setprecision(3) << mae << "\n";
}

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

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

    std::cout << "Testing different parameter combinations for TREND_L100:\n";
    std::cout << "Expected: lookback=120, atr_length=100\n\n";

    // Test if parameters are swapped
    test_params(series, ohlcv_bars, tssb_bars, "TREND_L100", 120, 100);  // Correct order
    test_params(series, ohlcv_bars, tssb_bars, "TREND_L100", 100, 120);  // Swapped
    test_params(series, ohlcv_bars, tssb_bars, "TREND_L100", 120, 120);  // Both 120
    test_params(series, ohlcv_bars, tssb_bars, "TREND_L100", 100, 100);  // Both 100

    return 0;
}

#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

void test_trend(const SingleMarketSeries& series, const std::vector<OHLCVBar>& ohlcv_bars,
                const std::vector<TSBBIndicatorBar>& tssb_bars, const std::string& name,
                int lookback, int atr_length) {
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::LinearTrend;
    req.name = name;
    req.params[0] = lookback;
    req.params[1] = atr_length;

    auto result = tssb::compute_single_indicator(series, req);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);

    size_t csv_start = 1078;
    double sum_abs_error = 0;
    int count = 0;
    for (size_t i = 0; i < 100; ++i) {
        size_t idx = csv_start + i;
        if (std::isfinite(result.values[idx]) && std::isfinite(expected[idx])) {
            sum_abs_error += std::abs(result.values[idx] - expected[idx]);
            count++;
        }
    }
    double mae = count > 0 ? sum_abs_error / count : 999;

    std::cout << name << " (lookback=" << lookback << ", atr=" << atr_length << "): MAE="
              << std::setprecision(3) << mae << "\n";
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

    std::cout << "Testing TREND indicators with corrected parameters:\n\n";

    // Original parameters from var.txt
    std::cout << "ORIGINAL PARAMETERS (from var.txt):\n";
    test_trend(series, ohlcv_bars, tssb_bars, "TREND_S100", 10, 100);
    test_trend(series, ohlcv_bars, tssb_bars, "TREND_M100", 50, 100);
    test_trend(series, ohlcv_bars, tssb_bars, "TREND_L100", 120, 100);

    std::cout << "\nCORRECTED PARAMETERS (TREND_L100 fixed to 100, 100):\n";
    test_trend(series, ohlcv_bars, tssb_bars, "TREND_S100", 10, 100);
    test_trend(series, ohlcv_bars, tssb_bars, "TREND_M100", 50, 100);
    test_trend(series, ohlcv_bars, tssb_bars, "TREND_L100", 100, 100);  // CORRECTED!

    return 0;
}

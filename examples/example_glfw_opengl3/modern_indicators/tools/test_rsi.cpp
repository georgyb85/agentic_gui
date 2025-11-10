#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

// Test if current RSI implementation matches CSV
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

    std::cout << "Testing RSI variants\n";
    std::cout << "====================\n\n";

    // Test RSI_S (length=10), RSI_M (length=50), RSI_L (length=120)
    std::vector<std::tuple<std::string, int>> tests = {
        {"RSI_S", 10},
        {"RSI_M", 50},
        {"RSI_L", 120}
    };

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

    size_t csv_start = 1078;

    for (const auto& [name, length] : tests) {
        std::cout << "\n" << name << " (length=" << length << ")\n";
        std::cout << std::string(50, '-') << "\n";

        SingleIndicatorRequest req;
        req.id = SingleIndicatorId::RSI;
        req.name = name;
        req.params[0] = length;

        auto result = tssb::compute_single_indicator(series, req);
        if (!result.success) {
            std::cerr << "ERROR: " << result.error_message << "\n";
            continue;
        }

        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);

        // Show first 10 values
        std::cout << "First 10 CSV values:\n";
        for (int i = 0; i < 10; ++i) {
            size_t idx = csv_start + i;
            double error = result.values[idx] - expected[idx];
            std::cout << "  Bar " << std::setw(4) << idx
                      << ": TSSB=" << std::fixed << std::setprecision(4) << std::setw(10) << expected[idx]
                      << ", Ours=" << std::setw(10) << result.values[idx]
                      << ", Err=" << std::setw(8) << error << "\n";
        }

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
        std::cout << "\nMAE: " << std::setprecision(6) << mae << "\n";
        std::cout << "Max Error: " << std::setprecision(6) << max_error << "\n";

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

        std::cout << "\nError distribution:\n";
        std::cout << "  |error| < 0.1: " << under_0_1 << " (" << (100.0*under_0_1/count) << "%)\n";
        std::cout << "  |error| < 1.0: " << under_1_0 << " (" << (100.0*under_1_0/count) << "%)\n";
        std::cout << "  |error| < 5.0: " << under_5_0 << " (" << (100.0*under_5_0/count) << "%)\n";
    }

    return 0;
}

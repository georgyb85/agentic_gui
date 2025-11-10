#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    if (ohlcv_bars.empty()) {
        std::cerr << "ERROR: " << OHLCVParser::get_last_error() << "\n";
        return 1;
    }

    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Compute indicators
    std::vector<std::pair<std::string, SingleIndicatorRequest>> indicators;

    // ADX indicators
    SingleIndicatorRequest adx_s;
    adx_s.id = SingleIndicatorId::Adx;
    adx_s.name = "ADX_S";
    adx_s.params[0] = 14;
    indicators.push_back({"ADX_S", adx_s});

    SingleIndicatorRequest adx_m;
    adx_m.id = SingleIndicatorId::Adx;
    adx_m.name = "ADX_M";
    adx_m.params[0] = 50;
    indicators.push_back({"ADX_M", adx_m});

    SingleIndicatorRequest adx_l;
    adx_l.id = SingleIndicatorId::Adx;
    adx_l.name = "ADX_L";
    adx_l.params[0] = 120;
    indicators.push_back({"ADX_L", adx_l});

    // TREND indicators
    SingleIndicatorRequest trend_s;
    trend_s.id = SingleIndicatorId::LinearTrend;
    trend_s.name = "TREND_S100";
    trend_s.params[0] = 10;
    trend_s.params[1] = 100;
    indicators.push_back({"TREND_S100", trend_s});

    SingleIndicatorRequest trend_m;
    trend_m.id = SingleIndicatorId::LinearTrend;
    trend_m.name = "TREND_M100";
    trend_m.params[0] = 50;
    trend_m.params[1] = 100;
    indicators.push_back({"TREND_M100", trend_m});

    SingleIndicatorRequest trend_l;
    trend_l.id = SingleIndicatorId::LinearTrend;
    trend_l.name = "TREND_L100";
    trend_l.params[0] = 120;
    trend_l.params[1] = 100;
    indicators.push_back({"TREND_L100", trend_l});

    // Compute all indicators
    std::vector<std::vector<double>> results;
    for (const auto& [name, req] : indicators) {
        auto result = compute_single_indicator(series, req);
        if (result.success) {
            results.push_back(result.values);
            std::cout << "Computed " << name << "\n";
        } else {
            std::cerr << "ERROR computing " << name << ": " << result.error_message << "\n";
            results.push_back(std::vector<double>(ohlcv_bars.size(), 0.0));
        }
    }

    // Write to CSV
    std::ofstream out("computed_indicators.csv");
    if (!out) {
        std::cerr << "Failed to create output file\n";
        return 1;
    }

    // Write header
    out << "Date,Time,Open,High,Low,Close,Volume";
    for (const auto& [name, req] : indicators) {
        out << "," << name;
    }
    out << "\n";

    // Write data - only from bar 1078 onward to match CSV
    for (size_t i = 1078; i < ohlcv_bars.size(); ++i) {
        const auto& bar = ohlcv_bars[i];
        out << bar.date << "," << bar.time << ","
            << std::fixed << std::setprecision(8)
            << bar.open << "," << bar.high << "," << bar.low << ","
            << bar.close << "," << bar.volume;

        for (size_t j = 0; j < results.size(); ++j) {
            out << "," << results[j][i];
        }
        out << "\n";
    }

    out.close();
    std::cout << "Wrote computed_indicators.csv with " << (ohlcv_bars.size() - 1078) << " rows\n";

    return 0;
}
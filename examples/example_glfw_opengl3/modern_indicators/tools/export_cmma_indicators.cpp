#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV> <output.csv>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    // Convert to series
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    struct CMMATest {
        std::string name;
        std::string csv_col;
        int lookback;
        int atr_length;
    };

    std::vector<CMMATest> tests = {
        {"CMMA_S", "CMMA_S", 10, 250},
        {"CMMA_M", "CMMA_M", 50, 250},
        {"CMMA_L", "CMMA_L", 120, 250}
    };

    // Compute all indicators
    std::vector<std::vector<double>> csv_values;
    std::vector<std::vector<double>> computed_values;
    std::vector<std::string> names;

    for (const auto& test : tests) {
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_col);
        csv_values.push_back(expected);
        names.push_back(test.name);

        SingleIndicatorRequest req;
        req.id = SingleIndicatorId::CloseMinusMovingAverage;
        req.name = test.name;
        req.params[0] = test.lookback;
        req.params[1] = test.atr_length;

        auto result = compute_single_indicator(series, req);
        if (!result.success) {
            std::cerr << "ERROR computing " << test.name << ": " << result.error_message << "\n";
            return 1;
        }
        computed_values.push_back(result.values);
    }

    // Write CSV file
    std::ofstream out(argv[3]);
    if (!out) {
        std::cerr << "ERROR: Cannot open output file: " << argv[3] << "\n";
        return 1;
    }

    // Header
    out << "bar,date,time";
    for (const auto& name : names) {
        out << ",csv_" << name << ",computed_" << name;
    }
    out << "\n";

    // Data
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        out << i << "," << ohlcv_bars[i].date << "," << ohlcv_bars[i].time;

        for (size_t j = 0; j < tests.size(); ++j) {
            out << "," << std::setprecision(10) << csv_values[j][i]
                << "," << std::setprecision(10) << computed_values[j][i];
        }
        out << "\n";
    }

    out.close();
    std::cout << "Exported CMMA data to " << argv[3] << "\n";
    std::cout << "Rows: " << ohlcv_bars.size() << "\n";
    std::cout << "Indicators: " << tests.size() << "\n";

    return 0;
}

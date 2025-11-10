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

    // Convert to series
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    std::cout << "TESTING BOTH CMMA VERSIONS\n";
    std::cout << "===========================\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    std::cout << std::setw(12) << "Indicator"
              << std::setw(18) << "Book Formula MAE"
              << std::setw(18) << "TSSB CSV MAE"
              << std::setw(18) << "Improvement" << "\n";
    std::cout << std::string(66, '-') << "\n";

    for (const auto& test : tests) {
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        // Test book formula (default, param[2]=0)
        SingleIndicatorRequest req_book;
        req_book.id = SingleIndicatorId::CloseMinusMovingAverage;
        req_book.name = test.name;
        req_book.params[0] = test.lookback;
        req_book.params[1] = test.atr;
        req_book.params[2] = 0;  // Use book formula

        auto result_book = compute_single_indicator(series, req_book);

        double mae_book = 0.0;
        int count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(result_book.values[i])) {
                mae_book += std::abs(result_book.values[i] - expected[i]);
                count++;
            }
        }
        mae_book /= count;

        // Test TSSB CSV formula (param[2]=1)
        SingleIndicatorRequest req_csv;
        req_csv.id = SingleIndicatorId::CloseMinusMovingAverage;
        req_csv.name = test.name;
        req_csv.params[0] = test.lookback;
        req_csv.params[1] = test.atr;
        req_csv.params[2] = 1;  // Use TSSB CSV formula

        auto result_csv = compute_single_indicator(series, req_csv);

        double mae_csv = 0.0;
        count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i]) && std::isfinite(result_csv.values[i])) {
                mae_csv += std::abs(result_csv.values[i] - expected[i]);
                count++;
            }
        }
        mae_csv /= count;

        std::cout << std::setw(12) << test.name
                  << std::fixed << std::setprecision(6)
                  << std::setw(18) << mae_book
                  << std::setw(18) << mae_csv
                  << std::setprecision(1)
                  << std::setw(17) << ((mae_book - mae_csv) / mae_book * 100.0) << "%";

        if (mae_csv < 0.1) std::cout << "  ✓✓";
        else if (mae_csv < 1.0) std::cout << "  ✓";

        std::cout << "\n";
    }

    std::cout << "\n" << std::string(66, '=') << "\n";
    std::cout << "SUMMARY\n";
    std::cout << std::string(66, '=') << "\n\n";

    std::cout << "Book Formula (default):\n";
    std::cout << "  Formula: 100 * Φ(Δ / (ATR * sqrt(k+1))) - 50\n";
    std::cout << "  Matches: Timothy Masters' book code (cmma.txt)\n";
    std::cout << "  Use: params = {lookback, atr_length, 0}\n\n";

    std::cout << "TSSB CSV Formula:\n";
    std::cout << "  Formula: 100 * Φ(0.095 * Δ / ATR) - 50\n";
    std::cout << "  Matches: TSSB CSV output (likely bug in executable)\n";
    std::cout << "  Use: params = {lookback, atr_length, 1}\n\n";

    std::cout << "Recommended: Use TSSB CSV version (param[2]=1) to match\n";
    std::cout << "your trading system's historical CSV data.\n";

    return 0;
}

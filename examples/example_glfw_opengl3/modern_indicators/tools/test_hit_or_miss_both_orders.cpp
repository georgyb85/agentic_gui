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
    std::cout << "TESTING THRESHOLD CHECKING ORDER\n";
    std::cout << "====================================================================\n\n";

    struct OrderTest {
        std::string name;
        double order_param;  // 0 = down first, 1 = up first
    };

    std::vector<OrderTest> orders = {
        {"DOWN FIRST (default)", 0.0},
        {"UP FIRST", 1.0}
    };

    for (const auto& order : orders) {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "Testing with: " << order.name << "\n";
        std::cout << std::string(70, '=') << "\n\n";

        // Test all three indicators with this order
        struct IndicatorTest {
            std::string name;
            std::string csv_col;
            std::vector<double> params;
        };

        std::vector<IndicatorTest> tests = {
            {"TGT_115", "TGT_115", {1, 1, 5, 0, order.order_param}},
            {"TGT_315", "TGT_315", {3, 1, 5, 0, order.order_param}},
            {"TGT_555", "TGT_555", {5, 5, 5, 0, order.order_param}},
        };

        for (const auto& test : tests) {
            auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_col);

            SingleIndicatorRequest req;
            req.id = SingleIndicatorId::HitOrMiss;
            req.name = test.name;
            for (size_t i = 0; i < test.params.size(); ++i) {
                req.params[i] = test.params[i];
            }

            auto result = compute_single_indicator(series, req);

            if (!result.success) {
                std::cerr << "ERROR computing " << test.name << ": " << result.error_message << "\n";
                continue;
            }

            // Compute statistics
            double sum_abs_error = 0.0;
            double max_abs_error = 0.0;
            int valid_count = 0;
            int outliers = 0;  // errors > 10

            for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i]) && std::isfinite(result.values[i]) &&
                    std::fabs(expected[i]) > 1e-6) {
                    double abs_error = std::fabs(result.values[i] - expected[i]);
                    sum_abs_error += abs_error;
                    if (abs_error > max_abs_error) max_abs_error = abs_error;
                    if (abs_error > 10.0) outliers++;
                    valid_count++;
                }
            }

            double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;

            std::cout << "  " << test.name << ": "
                      << "MAE=" << std::fixed << std::setprecision(4) << mae
                      << ", Max=" << std::setprecision(2) << max_abs_error
                      << ", Outliers(>10)=" << outliers
                      << " (" << std::setprecision(2) << (100.0*outliers/valid_count) << "%)\n";
        }
    }

    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "CONCLUSION: Use the order with fewer outliers\n";
    std::cout << std::string(70, '=') << "\n";

    return 0;
}

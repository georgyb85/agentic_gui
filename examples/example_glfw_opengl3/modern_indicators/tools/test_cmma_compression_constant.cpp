#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

using namespace tssb;
using namespace tssb::validation;

void compute_cmma_with_compression(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                                   std::vector<double>& output, double post_scale, double compression_c) {
    int n = bars.size();
    output.assign(n, 0.0);
    int front_bad = std::max(lookback, atr_length);

    for (int icase = front_bad; icase < n; ++icase) {
        double sum = 0.0;
        for (int k = icase - lookback; k < icase; ++k) {
            sum += std::log(bars[k].close);
        }
        sum /= lookback;

        double sum_tr = 0.0;
        for (int i = icase - atr_length + 1; i <= icase; ++i) {
            double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                               bars[i].high / bars[i-1].close,
                                               bars[i-1].close / bars[i].low}));
            sum_tr += tr_log;
        }
        double atr_val = sum_tr / atr_length;

        if (atr_val > 0.0) {
            double denom = atr_val * std::sqrt(lookback + 1.0);
            double raw_val = (std::log(bars[icase].close) - sum) / denom;

            // Apply compression constant
            double compressed = tssb::normal_cdf(compression_c * raw_val);
            output[icase] = post_scale * compressed - (post_scale / 2.0);
        } else {
            output[icase] = 0.0;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "TESTING COMPRESSION CONSTANT (c) in: normal_cdf(c * raw_val)\n";
    std::cout << "=============================================================\n\n";

    struct Test { std::string name; int lookback; int atr; double post_scale; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250, 10.27 * std::sqrt(10)},
        {"CMMA_M", 50, 250, 9.76 * std::sqrt(50)},
        {"CMMA_L", 120, 250, 9.47 * std::sqrt(120)}
    };

    for (const auto& test : tests) {
        std::cout << "\n" << test.name << " (lookback=" << test.lookback << ")\n";
        std::cout << std::string(70, '=') << "\n";

        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::cout << std::setw(8) << "c"
                  << std::setw(15) << "MAE"
                  << std::setw(15) << "Max Error"
                  << std::setw(15) << "Mean Error" << "\n";
        std::cout << std::string(53, '-') << "\n";

        double best_mae = 1e10;
        double best_c = 0;

        // Test compression constants from 0.2 to 2.0
        for (double c = 0.2; c <= 2.0; c += 0.05) {
            std::vector<double> output;
            compute_cmma_with_compression(ohlcv_bars, test.lookback, test.atr, output, test.post_scale, c);

            double mae = 0.0;
            double max_error = 0.0;
            double sum_error = 0.0;
            int count = 0;

            for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    double error = std::abs(output[i] - expected[i]);
                    double signed_error = output[i] - expected[i];
                    mae += error;
                    sum_error += signed_error;
                    max_error = std::max(max_error, error);
                    count++;
                }
            }
            mae /= count;
            double mean_error = sum_error / count;

            if (mae < best_mae) {
                best_mae = mae;
                best_c = c;
            }

            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(8) << c
                      << std::setprecision(6)
                      << std::setw(15) << mae
                      << std::setw(15) << max_error
                      << std::setw(15) << mean_error;

            if (mae < 0.01) std::cout << "  ✓✓✓";
            else if (mae < 0.1) std::cout << "  ✓✓";
            else if (mae < 0.3) std::cout << "  ✓";

            std::cout << "\n";
        }

        std::cout << "\nBest c: " << std::setprecision(2) << best_c
                  << " (MAE: " << std::setprecision(6) << best_mae << ")\n";
    }

    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "TESTING OPTIMAL c VALUES TOGETHER\n";
    std::cout << std::string(70, '=') << "\n\n";

    // Find optimal c for each
    std::vector<double> optimal_c_values;

    for (const auto& test : tests) {
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        double best_mae = 1e10;
        double best_c = 1.0;

        for (double c = 0.1; c <= 3.0; c += 0.01) {
            std::vector<double> output;
            compute_cmma_with_compression(ohlcv_bars, test.lookback, test.atr, output, test.post_scale, c);

            double mae = 0.0;
            int count = 0;

            for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    mae += std::abs(output[i] - expected[i]);
                    count++;
                }
            }
            mae /= count;

            if (mae < best_mae) {
                best_mae = mae;
                best_c = c;
            }
        }

        optimal_c_values.push_back(best_c);
    }

    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Lookback"
              << std::setw(12) << "Optimal c" << "\n";
    std::cout << std::string(36, '-') << "\n";

    for (size_t i = 0; i < tests.size(); ++i) {
        std::cout << std::setw(12) << tests[i].name
                  << std::setw(12) << tests[i].lookback
                  << std::fixed << std::setprecision(2)
                  << std::setw(12) << optimal_c_values[i] << "\n";
    }

    // Check for pattern
    std::cout << "\nPattern analysis:\n";
    std::cout << "  CMMA_S (lookback=10):  c = " << optimal_c_values[0] << "\n";
    std::cout << "  CMMA_M (lookback=50):  c = " << optimal_c_values[1] << "\n";
    std::cout << "  CMMA_L (lookback=120): c = " << optimal_c_values[2] << "\n";

    return 0;
}

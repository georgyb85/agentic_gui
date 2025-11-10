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

void compute_cmma_formula(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                         std::vector<double>& output, double constant) {
    int n = bars.size();
    output.assign(n, 0.0);
    int front_bad = std::max(lookback, atr_length);

    // Calculate post_scale from formula: post_scale = constant * sqrt(lookback)
    double post_scale = constant * std::sqrt(lookback);

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
            double compressed = tssb::normal_cdf(raw_val);
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

    std::cout << "VERIFYING CMMA FORMULA: post_scale = C * sqrt(lookback)\n";
    std::cout << "========================================================\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    std::cout << "Testing different values of constant C:\n\n";
    std::cout << std::setw(10) << "Constant"
              << std::setw(15) << "CMMA_S MAE"
              << std::setw(15) << "CMMA_M MAE"
              << std::setw(15) << "CMMA_L MAE"
              << std::setw(15) << "Avg MAE" << "\n";
    std::cout << std::string(70, '-') << "\n";

    double best_avg_mae = 1e10;
    double best_constant = 0;

    // Test constants from 8 to 12 in steps of 0.1
    for (double C = 8.0; C <= 12.0; C += 0.1) {
        std::vector<double> maes;

        for (const auto& test : tests) {
            auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

            size_t first_valid = 0;
            for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    first_valid = i;
                    break;
                }
            }

            std::vector<double> output;
            compute_cmma_formula(ohlcv_bars, test.lookback, test.atr, output, C);

            double mae = 0.0;
            int count = 0;
            for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    mae += std::abs(output[i] - expected[i]);
                    count++;
                }
            }
            mae /= count;
            maes.push_back(mae);
        }

        double avg_mae = (maes[0] + maes[1] + maes[2]) / 3.0;

        if (avg_mae < best_avg_mae) {
            best_avg_mae = avg_mae;
            best_constant = C;
        }

        // Print results
        std::cout << std::fixed << std::setprecision(2)
                  << std::setw(10) << C
                  << std::setprecision(6)
                  << std::setw(15) << maes[0]
                  << std::setw(15) << maes[1]
                  << std::setw(15) << maes[2]
                  << std::setw(15) << avg_mae;

        if (avg_mae < 0.2) {
            std::cout << "  ✓✓";
        } else if (avg_mae < 0.5) {
            std::cout << "  ✓";
        }

        std::cout << "\n";
    }

    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "BEST CONSTANT: " << std::fixed << std::setprecision(2) << best_constant << "\n";
    std::cout << "Average MAE: " << std::setprecision(6) << best_avg_mae << "\n";
    std::cout << std::string(70, '=') << "\n\n";

    std::cout << "FINAL FORMULA FOR CMMA:\n";
    std::cout << "  post_scale = " << best_constant << " * sqrt(lookback)\n";
    std::cout << "  output = post_scale * normal_cdf(raw_val) - post_scale/2\n\n";

    std::cout << "For each CMMA indicator:\n";
    for (const auto& test : tests) {
        double post_scale = best_constant * std::sqrt(test.lookback);
        std::cout << "  " << test.name << " (lookback=" << test.lookback << "): "
                  << "post_scale = " << std::setprecision(1) << post_scale
                  << ", range = [" << -post_scale/2 << ", " << post_scale/2 << "]\n";
    }

    return 0;
}

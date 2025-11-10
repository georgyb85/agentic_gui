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

void compute_cmma_with_c(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                         std::vector<double>& output, double C) {
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

            // Fixed post_scale and shift, vary compression C
            output[icase] = 100.0 * tssb::normal_cdf(C * raw_val) - 50.0;
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

    std::cout << "FINDING OPTIMAL COMPRESSION CONSTANT C\n";
    std::cout << "Formula: 100.0 * normal_cdf(C * raw_val) - 50.0\n";
    std::cout << "=========================================\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    std::vector<double> optimal_c_values;

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

        // Coarse search first
        double best_mae = 1e10;
        double best_c = 1.0;

        std::cout << "Coarse search (C from 0.1 to 3.0, step 0.1):\n";
        std::cout << std::setw(8) << "C"
                  << std::setw(15) << "MAE" << "\n";
        std::cout << std::string(23, '-') << "\n";

        for (double C = 0.1; C <= 3.0; C += 0.1) {
            std::vector<double> output;
            compute_cmma_with_c(ohlcv_bars, test.lookback, test.atr, output, C);

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
                best_c = C;
            }

            if (C >= best_c - 0.3 && C <= best_c + 0.3) {
                std::cout << std::fixed << std::setprecision(2)
                          << std::setw(8) << C
                          << std::setprecision(6)
                          << std::setw(15) << mae;
                if (mae == best_mae) std::cout << "  ← best";
                std::cout << "\n";
            }
        }

        std::cout << "\nCoarse best: C = " << std::fixed << std::setprecision(2) << best_c
                  << ", MAE = " << std::setprecision(6) << best_mae << "\n";

        // Fine search around best
        std::cout << "\nFine search (C from " << (best_c - 0.2) << " to " << (best_c + 0.2) << ", step 0.01):\n";

        double fine_best_mae = 1e10;
        double fine_best_c = best_c;

        for (double C = best_c - 0.2; C <= best_c + 0.2; C += 0.01) {
            if (C <= 0) continue;

            std::vector<double> output;
            compute_cmma_with_c(ohlcv_bars, test.lookback, test.atr, output, C);

            double mae = 0.0;
            int count = 0;
            for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    mae += std::abs(output[i] - expected[i]);
                    count++;
                }
            }
            mae /= count;

            if (mae < fine_best_mae) {
                fine_best_mae = mae;
                fine_best_c = C;
            }
        }

        std::cout << "Fine best: C = " << std::fixed << std::setprecision(2) << fine_best_c
                  << ", MAE = " << std::setprecision(6) << fine_best_mae;

        if (fine_best_mae < 0.01) std::cout << "  ✓✓✓ PERFECT!";
        else if (fine_best_mae < 0.1) std::cout << "  ✓✓ EXCELLENT";
        else if (fine_best_mae < 1.0) std::cout << "  ✓ GOOD";

        std::cout << "\n";

        optimal_c_values.push_back(fine_best_c);
    }

    // Summary
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "SUMMARY: OPTIMAL C VALUES\n";
    std::cout << std::string(70, '=') << "\n\n";

    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Lookback"
              << std::setw(15) << "Optimal C"
              << std::setw(15) << "MAE" << "\n";
    std::cout << std::string(54, '-') << "\n";

    for (size_t i = 0; i < tests.size(); ++i) {
        const auto& test = tests[i];
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t j = 0; j < ohlcv_bars.size(); ++j) {
            if (std::isfinite(expected[j])) {
                first_valid = j;
                break;
            }
        }

        std::vector<double> output;
        compute_cmma_with_c(ohlcv_bars, test.lookback, test.atr, output, optimal_c_values[i]);

        double mae = 0.0;
        int count = 0;
        for (size_t j = first_valid; j < ohlcv_bars.size(); ++j) {
            if (std::isfinite(expected[j])) {
                mae += std::abs(output[j] - expected[j]);
                count++;
            }
        }
        mae /= count;

        std::cout << std::setw(12) << test.name
                  << std::setw(12) << test.lookback
                  << std::fixed << std::setprecision(2)
                  << std::setw(15) << optimal_c_values[i]
                  << std::setprecision(6)
                  << std::setw(15) << mae << "\n";
    }

    // Check for pattern in C values
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "PATTERN ANALYSIS\n";
    std::cout << std::string(70, '=') << "\n\n";

    std::cout << "Optimal C values:\n";
    std::cout << "  CMMA_S (lookback=10):  C = " << std::setprecision(2) << optimal_c_values[0] << "\n";
    std::cout << "  CMMA_M (lookback=50):  C = " << optimal_c_values[1] << "\n";
    std::cout << "  CMMA_L (lookback=120): C = " << optimal_c_values[2] << "\n\n";

    // Test if C varies with lookback
    if (std::abs(optimal_c_values[0] - optimal_c_values[1]) < 0.05 &&
        std::abs(optimal_c_values[1] - optimal_c_values[2]) < 0.05) {
        double avg_c = (optimal_c_values[0] + optimal_c_values[1] + optimal_c_values[2]) / 3.0;
        std::cout << "✓ C values are similar! Average C = " << std::setprecision(2) << avg_c << "\n";
        std::cout << "  → Can use single compression constant for all lookbacks\n";
        std::cout << "  → Recommended: C = " << avg_c << "\n";
    } else {
        std::cout << "✗ C values vary significantly with lookback\n";
        std::cout << "  → Need to use lookup table or formula for C\n";

        // Try to fit C = A - B * log(lookback)
        double log10 = std::log(tests[0].lookback);
        double log50 = std::log(tests[1].lookback);
        double log120 = std::log(tests[2].lookback);

        double sum_x = log10 + log50 + log120;
        double sum_y = optimal_c_values[0] + optimal_c_values[1] + optimal_c_values[2];
        double sum_xy = log10 * optimal_c_values[0] + log50 * optimal_c_values[1] + log120 * optimal_c_values[2];
        double sum_x2 = log10*log10 + log50*log50 + log120*log120;

        double B = (3 * sum_xy - sum_x * sum_y) / (3 * sum_x2 - sum_x * sum_x);
        double A = (sum_y - B * sum_x) / 3;

        std::cout << "\n  Trying: C = A + B * log(lookback)\n";
        std::cout << "    A = " << std::setprecision(4) << A << "\n";
        std::cout << "    B = " << B << "\n";

        for (size_t i = 0; i < tests.size(); ++i) {
            double predicted = A + B * std::log(tests[i].lookback);
            std::cout << "    " << tests[i].name << ": actual=" << std::setprecision(2) << optimal_c_values[i]
                      << ", predicted=" << predicted
                      << ", error=" << std::setprecision(4) << (predicted - optimal_c_values[i]) << "\n";
        }
    }

    return 0;
}

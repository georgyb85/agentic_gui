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

double find_best_c(const std::vector<OHLCVBar>& ohlcv_bars,
                   const std::vector<TSBBIndicatorBar>& tssb_bars,
                   const std::string& name, int lookback, int atr_length) {

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);

    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            first_valid = i;
            break;
        }
    }

    double best_mae = 1e10;
    double best_c = 0;

    // Fine-grained search from 8 to 12
    for (double C = 8.0; C <= 12.0; C += 0.01) {
        std::vector<double> output;
        compute_cmma_formula(ohlcv_bars, lookback, atr_length, output, C);

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
    }

    return best_c;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "FINDING PATTERN IN OPTIMAL C VALUES\n";
    std::cout << "====================================\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Lookback"
              << std::setw(15) << "Optimal C"
              << std::setw(18) << "post_scale"
              << std::setw(15) << "MAE" << "\n";
    std::cout << std::string(72, '-') << "\n";

    std::vector<double> optimal_cs;
    std::vector<int> lookbacks;

    for (const auto& test : tests) {
        double best_c = find_best_c(ohlcv_bars, tssb_bars, test.name, test.lookback, test.atr);
        double post_scale = best_c * std::sqrt(test.lookback);

        // Calculate MAE with optimal C
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::vector<double> output;
        compute_cmma_formula(ohlcv_bars, test.lookback, test.atr, output, best_c);

        double mae = 0.0;
        int count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                mae += std::abs(output[i] - expected[i]);
                count++;
            }
        }
        mae /= count;

        std::cout << std::setw(12) << test.name
                  << std::setw(12) << test.lookback
                  << std::fixed << std::setprecision(2)
                  << std::setw(15) << best_c
                  << std::setw(18) << post_scale
                  << std::setprecision(6)
                  << std::setw(15) << mae;

        if (mae < 0.01) std::cout << "  ✓✓✓";
        else if (mae < 0.1) std::cout << "  ✓✓";
        else if (mae < 0.5) std::cout << "  ✓";

        std::cout << "\n";

        optimal_cs.push_back(best_c);
        lookbacks.push_back(test.lookback);
    }

    std::cout << "\n" << std::string(72, '=') << "\n";
    std::cout << "PATTERN ANALYSIS\n";
    std::cout << std::string(72, '=') << "\n\n";

    // Try to fit: C = A - B * log(lookback)
    // Using two points to solve for A and B
    double log_lookback_0 = std::log(lookbacks[0]);
    double log_lookback_1 = std::log(lookbacks[1]);
    double C_0 = optimal_cs[0];
    double C_1 = optimal_cs[1];

    double B_log = (C_0 - C_1) / (log_lookback_0 - log_lookback_1);
    double A_log = C_0 + B_log * log_lookback_0;

    std::cout << "Tested formula: C = A - B * log(lookback)\n";
    std::cout << "  A = " << std::fixed << std::setprecision(4) << A_log << "\n";
    std::cout << "  B = " << B_log << "\n";
    std::cout << "  Formula: C = " << A_log << " - " << B_log << " * log(lookback)\n\n";

    // Verify the formula
    std::cout << "Verification:\n";
    for (size_t i = 0; i < tests.size(); ++i) {
        double predicted_c = A_log - B_log * std::log(lookbacks[i]);
        std::cout << "  " << tests[i].name << ": Actual C = " << optimal_cs[i]
                  << ", Predicted C = " << predicted_c
                  << ", Error = " << std::abs(predicted_c - optimal_cs[i]) << "\n";
    }

    // Try to fit: C = A - B * sqrt(lookback)
    double sqrt_lookback_0 = std::sqrt(lookbacks[0]);
    double sqrt_lookback_1 = std::sqrt(lookbacks[1]);

    double B_sqrt = (C_0 - C_1) / (sqrt_lookback_0 - sqrt_lookback_1);
    double A_sqrt = C_0 + B_sqrt * sqrt_lookback_0;

    std::cout << "\n" << std::string(72, '-') << "\n";
    std::cout << "Tested formula: C = A - B * sqrt(lookback)\n";
    std::cout << "  A = " << std::fixed << std::setprecision(4) << A_sqrt << "\n";
    std::cout << "  B = " << B_sqrt << "\n";
    std::cout << "  Formula: C = " << A_sqrt << " - " << B_sqrt << " * sqrt(lookback)\n\n";

    std::cout << "Verification:\n";
    for (size_t i = 0; i < tests.size(); ++i) {
        double predicted_c = A_sqrt - B_sqrt * std::sqrt(lookbacks[i]);
        std::cout << "  " << tests[i].name << ": Actual C = " << optimal_cs[i]
                  << ", Predicted C = " << predicted_c
                  << ", Error = " << std::abs(predicted_c - optimal_cs[i]) << "\n";
    }

    return 0;
}

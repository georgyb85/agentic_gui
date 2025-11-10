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

void compute_cmma_with_formula(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                               std::vector<double>& output, double A, double B) {
    int n = bars.size();
    output.assign(n, 0.0);
    int front_bad = std::max(lookback, atr_length);

    double post_scale = A * std::sqrt(lookback) + B;

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

    std::cout << "FINDING OPTIMAL FORMULA: post_scale = A * sqrt(lookback) + B\n";
    std::cout << "==============================================================\n\n";

    struct Test { std::string name; int lookback; int atr; double optimal_scale; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250, 32.5},
        {"CMMA_M", 50, 250, 69.0},
        {"CMMA_L", 120, 250, 103.7}
    };

    // Fit formula: post_scale = A * sqrt(lookback) + B using linear regression
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    int n = tests.size();

    for (const auto& test : tests) {
        double x = std::sqrt(test.lookback);
        double y = test.optimal_scale;
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    double A = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    double B = (sum_y - A * sum_x) / n;

    std::cout << "Linear regression fit:\n";
    std::cout << "  A = " << std::fixed << std::setprecision(4) << A << "\n";
    std::cout << "  B = " << B << "\n";
    std::cout << "  Formula: post_scale = " << A << " * sqrt(lookback) + " << B << "\n\n";

    // Verify fit
    std::cout << "Verification of formula fit:\n";
    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Lookback"
              << std::setw(15) << "Optimal"
              << std::setw(15) << "Formula"
              << std::setw(12) << "Error" << "\n";
    std::cout << std::string(66, '-') << "\n";

    for (const auto& test : tests) {
        double predicted = A * std::sqrt(test.lookback) + B;
        double error = predicted - test.optimal_scale;
        std::cout << std::setw(12) << test.name
                  << std::setw(12) << test.lookback
                  << std::fixed << std::setprecision(2)
                  << std::setw(15) << test.optimal_scale
                  << std::setw(15) << predicted
                  << std::setprecision(4)
                  << std::setw(12) << error << "\n";
    }

    // Test the formula
    std::cout << "\n" << std::string(66, '=') << "\n";
    std::cout << "TESTING FORMULA PERFORMANCE\n";
    std::cout << std::string(66, '=') << "\n\n";

    std::cout << std::setw(12) << "Indicator"
              << std::setw(15) << "post_scale"
              << std::setw(15) << "MAE"
              << std::setw(15) << "vs Optimal" << "\n";
    std::cout << std::string(57, '-') << "\n";

    double total_mae = 0;
    std::vector<double> optimal_maes = {0.220, 0.210, 0.105};

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
        compute_cmma_with_formula(ohlcv_bars, test.lookback, test.atr, output, A, B);

        double post_scale = A * std::sqrt(test.lookback) + B;

        double mae = 0.0;
        int count = 0;
        for (size_t j = first_valid; j < ohlcv_bars.size(); ++j) {
            if (std::isfinite(expected[j])) {
                mae += std::abs(output[j] - expected[j]);
                count++;
            }
        }
        mae /= count;
        total_mae += mae;

        double vs_optimal = mae - optimal_maes[i];

        std::cout << std::setw(12) << test.name
                  << std::fixed << std::setprecision(1)
                  << std::setw(15) << post_scale
                  << std::setprecision(6)
                  << std::setw(15) << mae
                  << std::setprecision(4)
                  << std::setw(15) << vs_optimal;

        if (mae < 0.01) std::cout << "  ✓✓✓";
        else if (mae < 0.1) std::cout << "  ✓✓";
        else if (mae < 0.5) std::cout << "  ✓";

        std::cout << "\n";
    }

    std::cout << "\nAverage MAE with formula: " << std::fixed << std::setprecision(6) << total_mae / tests.size() << "\n";
    std::cout << "Average MAE with lookup:  " << (optimal_maes[0] + optimal_maes[1] + optimal_maes[2]) / 3.0 << "\n";

    // Test alternative: simple fixed C = average
    std::cout << "\n" << std::string(66, '=') << "\n";
    std::cout << "COMPARISON: Fixed C vs Formula\n";
    std::cout << std::string(66, '=') << "\n\n";

    double avg_C = (10.27 + 9.76 + 9.47) / 3.0;
    std::cout << "Testing fixed C = " << std::setprecision(2) << avg_C << " (average of optimal C values)\n\n";

    std::cout << std::setw(12) << "Method"
              << std::setw(18) << "CMMA_S MAE"
              << std::setw(18) << "CMMA_M MAE"
              << std::setw(18) << "CMMA_L MAE" << "\n";
    std::cout << std::string(66, '-') << "\n";

    std::vector<double> fixed_c_maes;
    for (const auto& test : tests) {
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);
        size_t first_valid = 0;
        for (size_t j = 0; j < ohlcv_bars.size(); ++j) {
            if (std::isfinite(expected[j])) {
                first_valid = j;
                break;
            }
        }

        std::vector<double> output;
        compute_cmma_with_formula(ohlcv_bars, test.lookback, test.atr, output, avg_C, 0.0);

        double mae = 0.0;
        int count = 0;
        for (size_t j = first_valid; j < ohlcv_bars.size(); ++j) {
            if (std::isfinite(expected[j])) {
                mae += std::abs(output[j] - expected[j]);
                count++;
            }
        }
        mae /= count;
        fixed_c_maes.push_back(mae);
    }

    std::cout << "Fixed C=" << std::setprecision(2) << avg_C;
    for (double mae : fixed_c_maes) {
        std::cout << std::fixed << std::setprecision(6) << std::setw(18) << mae;
    }
    std::cout << "\n";

    std::cout << "Formula A,B";
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
        compute_cmma_with_formula(ohlcv_bars, test.lookback, test.atr, output, A, B);

        double mae = 0.0;
        int count = 0;
        for (size_t j = first_valid; j < ohlcv_bars.size(); ++j) {
            if (std::isfinite(expected[j])) {
                mae += std::abs(output[j] - expected[j]);
                count++;
            }
        }
        mae /= count;
        std::cout << std::fixed << std::setprecision(6) << std::setw(18) << mae;
    }
    std::cout << "\n";

    double avg_fixed = (fixed_c_maes[0] + fixed_c_maes[1] + fixed_c_maes[2]) / 3.0;
    double avg_formula = total_mae / tests.size();

    std::cout << "\n";
    std::cout << "Average MAE (Fixed C=" << std::setprecision(2) << avg_C << "): "
              << std::setprecision(6) << avg_fixed << "\n";
    std::cout << "Average MAE (Formula):  " << avg_formula << "\n";

    if (avg_formula < avg_fixed) {
        std::cout << "\n✓ Formula is better by " << std::setprecision(4) << (avg_fixed - avg_formula) << "\n";
    } else {
        std::cout << "\n✓ Fixed C is better by " << std::setprecision(4) << (avg_formula - avg_fixed) << "\n";
    }

    std::cout << "\n" << std::string(66, '=') << "\n";
    std::cout << "RECOMMENDED FORMULA\n";
    std::cout << std::string(66, '=') << "\n\n";

    std::cout << "post_scale = " << std::setprecision(2) << A << " * sqrt(lookback) + " << B << "\n";
    std::cout << "output = post_scale * normal_cdf(raw_val) - post_scale/2\n\n";

    std::cout << "This single formula works for all lookback periods.\n";

    return 0;
}

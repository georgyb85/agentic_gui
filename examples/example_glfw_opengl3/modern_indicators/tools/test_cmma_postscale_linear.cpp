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

void compute_cmma_with_postscale(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                                 std::vector<double>& output, double post_scale) {
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
            double compressed = tssb::normal_cdf(raw_val);
            output[icase] = post_scale * compressed - (post_scale / 2.0);
        } else {
            output[icase] = 0.0;
        }
    }
}

double find_best_postscale(const std::vector<OHLCVBar>& ohlcv_bars,
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
    double best_scale = 0;

    // Fine-grained search
    for (double scale = 20.0; scale <= 120.0; scale += 0.1) {
        std::vector<double> output;
        compute_cmma_with_postscale(ohlcv_bars, lookback, atr_length, output, scale);

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
            best_scale = scale;
        }
    }

    return best_scale;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "TESTING: post_scale as linear function of lookback\n";
    std::cout << "====================================================\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Lookback"
              << std::setw(18) << "Optimal post_scale"
              << std::setw(15) << "MAE" << "\n";
    std::cout << std::string(57, '-') << "\n";

    std::vector<double> optimal_scales;
    std::vector<int> lookbacks;

    for (const auto& test : tests) {
        double best_scale = find_best_postscale(ohlcv_bars, tssb_bars, test.name, test.lookback, test.atr);

        // Calculate MAE with optimal post_scale
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::vector<double> output;
        compute_cmma_with_postscale(ohlcv_bars, test.lookback, test.atr, output, best_scale);

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
                  << std::fixed << std::setprecision(1)
                  << std::setw(18) << best_scale
                  << std::setprecision(6)
                  << std::setw(15) << mae;

        if (mae < 0.01) std::cout << "  ✓✓✓";
        else if (mae < 0.1) std::cout << "  ✓✓";
        else if (mae < 0.5) std::cout << "  ✓";

        std::cout << "\n";

        optimal_scales.push_back(best_scale);
        lookbacks.push_back(test.lookback);
    }

    std::cout << "\n" << std::string(57, '=') << "\n";
    std::cout << "PATTERN ANALYSIS\n";
    std::cout << std::string(57, '=') << "\n\n";

    // Fit: post_scale = A + B * lookback (linear regression)
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    int n = lookbacks.size();

    for (int i = 0; i < n; ++i) {
        sum_x += lookbacks[i];
        sum_y += optimal_scales[i];
        sum_xy += lookbacks[i] * optimal_scales[i];
        sum_x2 += lookbacks[i] * lookbacks[i];
    }

    double B = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    double A = (sum_y - B * sum_x) / n;

    std::cout << "Linear fit: post_scale = A + B * lookback\n";
    std::cout << "  A = " << std::fixed << std::setprecision(4) << A << "\n";
    std::cout << "  B = " << B << "\n";
    std::cout << "  Formula: post_scale = " << A << " + " << B << " * lookback\n\n";

    std::cout << "Verification:\n";
    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Actual"
              << std::setw(12) << "Predicted"
              << std::setw(12) << "Error" << "\n";
    std::cout << std::string(48, '-') << "\n";

    double max_error = 0;
    for (size_t i = 0; i < tests.size(); ++i) {
        double predicted = A + B * lookbacks[i];
        double error = std::abs(predicted - optimal_scales[i]);
        max_error = std::max(max_error, error);

        std::cout << std::setw(12) << tests[i].name
                  << std::setw(12) << std::setprecision(1) << optimal_scales[i]
                  << std::setw(12) << predicted
                  << std::setw(12) << std::setprecision(4) << error << "\n";
    }

    std::cout << "\nMax fitting error: " << std::setprecision(4) << max_error << "\n";

    // Test the formula
    std::cout << "\n" << std::string(57, '=') << "\n";
    std::cout << "TESTING LINEAR FORMULA\n";
    std::cout << std::string(57, '=') << "\n\n";

    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Lookback"
              << std::setw(15) << "post_scale"
              << std::setw(15) << "MAE" << "\n";
    std::cout << std::string(54, '-') << "\n";

    double total_mae = 0;
    for (const auto& test : tests) {
        double post_scale = A + B * test.lookback;

        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::vector<double> output;
        compute_cmma_with_postscale(ohlcv_bars, test.lookback, test.atr, output, post_scale);

        double mae = 0.0;
        int count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                mae += std::abs(output[i] - expected[i]);
                count++;
            }
        }
        mae /= count;
        total_mae += mae;

        std::cout << std::setw(12) << test.name
                  << std::setw(12) << test.lookback
                  << std::fixed << std::setprecision(1)
                  << std::setw(15) << post_scale
                  << std::setprecision(6)
                  << std::setw(15) << mae;

        if (mae < 0.01) std::cout << "  ✓✓✓";
        else if (mae < 0.1) std::cout << "  ✓✓";
        else if (mae < 0.5) std::cout << "  ✓";

        std::cout << "\n";
    }

    std::cout << "\nAverage MAE: " << std::setprecision(6) << total_mae / tests.size() << "\n";

    return 0;
}

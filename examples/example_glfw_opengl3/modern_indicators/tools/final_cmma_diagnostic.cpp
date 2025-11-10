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

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "FINAL CMMA DIAGNOSTIC\n";
    std::cout << "=====================\n\n";

    struct Test { std::string name; int lookback; int atr; double optimal_C; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250, 10.27},
        {"CMMA_M", 50, 250, 9.76},
        {"CMMA_L", 120, 250, 9.47}
    };

    for (const auto& test : tests) {
        std::cout << "\n" << test.name << " (lookback=" << test.lookback << ", C=" << test.optimal_C << ")\n";
        std::cout << std::string(60, '=') << "\n";

        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        // Find first valid bar
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        // Find bars with largest errors
        struct ErrorInfo { size_t bar; double expected; double computed; double error; };
        std::vector<ErrorInfo> errors;

        double post_scale = test.optimal_C * std::sqrt(test.lookback);
        int front_bad = std::max(test.lookback, test.atr);

        for (size_t icase = front_bad; icase < ohlcv_bars.size(); ++icase) {
            if (!std::isfinite(expected[icase])) continue;

            // Compute our value
            double sum = 0.0;
            for (int k = icase - test.lookback; k < (int)icase; ++k) {
                sum += std::log(ohlcv_bars[k].close);
            }
            sum /= test.lookback;

            double sum_tr = 0.0;
            for (int i = icase - test.atr + 1; i <= (int)icase; ++i) {
                double tr_log = std::log(std::max({ohlcv_bars[i].high / ohlcv_bars[i].low,
                                                   ohlcv_bars[i].high / ohlcv_bars[i-1].close,
                                                   ohlcv_bars[i-1].close / ohlcv_bars[i].low}));
                sum_tr += tr_log;
            }
            double atr_val = sum_tr / test.atr;

            double computed = 0.0;
            if (atr_val > 0.0) {
                double denom = atr_val * std::sqrt(test.lookback + 1.0);
                double raw_val = (std::log(ohlcv_bars[icase].close) - sum) / denom;
                double compressed = tssb::normal_cdf(raw_val);
                computed = post_scale * compressed - (post_scale / 2.0);
            }

            double error = computed - expected[icase];
            errors.push_back({icase, expected[icase], computed, error});
        }

        // Sort by absolute error
        std::sort(errors.begin(), errors.end(),
                  [](const ErrorInfo& a, const ErrorInfo& b) {
                      return std::abs(a.error) > std::abs(b.error);
                  });

        std::cout << "\nTop 10 bars with largest errors:\n";
        std::cout << std::setw(8) << "Bar"
                  << std::setw(15) << "Expected"
                  << std::setw(15) << "Computed"
                  << std::setw(12) << "Error"
                  << std::setw(12) << "Ratio" << "\n";
        std::cout << std::string(62, '-') << "\n";

        for (int i = 0; i < std::min(10, (int)errors.size()); ++i) {
            const auto& e = errors[i];
            double ratio = (std::abs(e.expected) > 0.01) ? e.computed / e.expected : 0.0;
            std::cout << std::setw(8) << e.bar
                      << std::fixed << std::setprecision(6)
                      << std::setw(15) << e.expected
                      << std::setw(15) << e.computed
                      << std::setw(12) << e.error
                      << std::setw(12) << std::setprecision(4) << ratio << "\n";
        }

        // Statistics
        double mae = 0.0, max_error = 0.0;
        std::vector<double> all_errors;
        for (const auto& e : errors) {
            double abs_err = std::abs(e.error);
            mae += abs_err;
            max_error = std::max(max_error, abs_err);
            all_errors.push_back(e.error);
        }
        mae /= errors.size();

        // Median error
        std::sort(all_errors.begin(), all_errors.end());
        double median_error = all_errors[all_errors.size() / 2];

        // Check if errors are systematically biased
        double sum_errors = 0.0;
        for (double err : all_errors) {
            sum_errors += err;
        }
        double mean_error = sum_errors / all_errors.size();

        std::cout << "\nStatistics:\n";
        std::cout << "  MAE: " << std::setprecision(6) << mae << "\n";
        std::cout << "  Max Error: " << max_error << "\n";
        std::cout << "  Mean Error: " << mean_error
                  << (std::abs(mean_error) > 0.05 ? "  ⚠ BIASED!" : "") << "\n";
        std::cout << "  Median Error: " << median_error << "\n";

        // Check systematic bias
        int positive = 0, negative = 0;
        for (double err : all_errors) {
            if (err > 0) positive++;
            else if (err < 0) negative++;
        }
        std::cout << "  Positive errors: " << positive << " (" << 100.0 * positive / all_errors.size() << "%)\n";
        std::cout << "  Negative errors: " << negative << " (" << 100.0 * negative / all_errors.size() << "%)\n";

        if (std::abs(mean_error) > 0.05) {
            std::cout << "\n  ⚠ SYSTEMATIC BIAS DETECTED: Mean error is " << mean_error << "\n";
            std::cout << "  → Suggests post_scale might need adjustment of " << -mean_error << "\n";
            double adjusted_scale = post_scale - mean_error * 2.0;  // *2 because we subtract post_scale/2
            std::cout << "  → Try post_scale = " << adjusted_scale << " instead of " << post_scale << "\n";
        }
    }

    return 0;
}

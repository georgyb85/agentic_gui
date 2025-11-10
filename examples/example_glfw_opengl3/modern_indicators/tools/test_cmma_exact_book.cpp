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

void compute_cmma_book_formula(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                               std::vector<double>& output) {
    int n = bars.size();
    output.assign(n, 0.0);
    int front_bad = std::max(lookback, atr_length);

    for (int icase = front_bad; icase < n; ++icase) {
        // Compute MA of log prices EXCLUDING current bar
        double sum = 0.0;
        for (int k = icase - lookback; k < icase; ++k) {
            sum += std::log(bars[k].close);
        }
        sum /= lookback;

        // Compute ATR
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

            // EXACT book formula: fixed 100.0 and 1.0
            output[icase] = 100.0 * tssb::normal_cdf(1.0 * raw_val) - 50.0;
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

    std::cout << "TESTING EXACT BOOK FORMULA: 100.0 * normal_cdf(1.0 * x) - 50.0\n";
    std::cout << "===============================================================\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    for (const auto& test : tests) {
        std::cout << "\n" << test.name << " (lookback=" << test.lookback << ")\n";
        std::cout << std::string(60, '=') << "\n";

        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::vector<double> output;
        compute_cmma_book_formula(ohlcv_bars, test.lookback, test.atr, output);

        // Calculate statistics
        double mae = 0.0;
        double max_error = 0.0;
        double sum_error = 0.0;
        int count = 0;

        std::vector<double> all_errors;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                double error = output[i] - expected[i];
                double abs_error = std::abs(error);
                mae += abs_error;
                sum_error += error;
                max_error = std::max(max_error, abs_error);
                all_errors.push_back(error);
                count++;
            }
        }
        mae /= count;
        double mean_error = sum_error / count;

        std::cout << "MAE: " << std::fixed << std::setprecision(6) << mae;
        if (mae < 0.01) std::cout << "  ✓✓✓ PERFECT!";
        else if (mae < 0.1) std::cout << "  ✓✓ EXCELLENT";
        else if (mae < 1.0) std::cout << "  ✓ GOOD";
        std::cout << "\n";

        std::cout << "Max Error: " << max_error << "\n";
        std::cout << "Mean Error: " << mean_error;
        if (std::abs(mean_error) > 0.05) std::cout << "  ⚠ BIASED!";
        std::cout << "\n";

        // Show first 10 values
        std::cout << "\nFirst 10 values:\n";
        std::cout << std::setw(8) << "Bar"
                  << std::setw(15) << "CSV"
                  << std::setw(15) << "Book Formula"
                  << std::setw(12) << "Error" << "\n";
        std::cout << std::string(50, '-') << "\n";

        for (int i = 0; i < 10 && (first_valid + i) < ohlcv_bars.size(); ++i) {
            size_t idx = first_valid + i;
            if (std::isfinite(expected[idx])) {
                double error = output[idx] - expected[idx];
                std::cout << std::setw(8) << idx
                          << std::fixed << std::setprecision(6)
                          << std::setw(15) << expected[idx]
                          << std::setw(15) << output[idx]
                          << std::setw(12) << error << "\n";
            }
        }

        // Show range of values
        double min_csv = 1e10, max_csv = -1e10;
        double min_computed = 1e10, max_computed = -1e10;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                min_csv = std::min(min_csv, expected[i]);
                max_csv = std::max(max_csv, expected[i]);
                min_computed = std::min(min_computed, output[i]);
                max_computed = std::max(max_computed, output[i]);
            }
        }

        std::cout << "\nValue ranges:\n";
        std::cout << "  CSV:          [" << std::setprecision(2) << min_csv << ", " << max_csv << "]\n";
        std::cout << "  Book Formula: [" << min_computed << ", " << max_computed << "]\n";
        std::cout << "  Book range should be: [-50.0, +50.0] theoretically\n";
    }

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "COMPARISON WITH OUR OPTIMIZED FORMULA\n";
    std::cout << std::string(60, '=') << "\n\n";

    std::cout << std::setw(12) << "Indicator"
              << std::setw(18) << "Book MAE"
              << std::setw(18) << "Optimized MAE"
              << std::setw(18) << "Improvement" << "\n";
    std::cout << std::string(66, '-') << "\n";

    double book_maes[] = {0, 0, 0};
    double opt_maes[] = {0.220, 0.210, 0.105};  // From our previous results

    for (size_t t = 0; t < tests.size(); ++t) {
        const auto& test = tests[t];
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::vector<double> output;
        compute_cmma_book_formula(ohlcv_bars, test.lookback, test.atr, output);

        double mae = 0.0;
        int count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                mae += std::abs(output[i] - expected[i]);
                count++;
            }
        }
        mae /= count;
        book_maes[t] = mae;

        double improvement = ((book_maes[t] - opt_maes[t]) / book_maes[t]) * 100.0;

        std::cout << std::setw(12) << test.name
                  << std::fixed << std::setprecision(6)
                  << std::setw(18) << book_maes[t]
                  << std::setw(18) << opt_maes[t]
                  << std::setprecision(1)
                  << std::setw(17) << improvement << "%" << "\n";
    }

    std::cout << "\n";
    double avg_book = (book_maes[0] + book_maes[1] + book_maes[2]) / 3.0;
    double avg_opt = (opt_maes[0] + opt_maes[1] + opt_maes[2]) / 3.0;
    std::cout << "Average Book MAE: " << std::fixed << std::setprecision(6) << avg_book << "\n";
    std::cout << "Average Optimized MAE: " << avg_opt << "\n";
    std::cout << "Overall Improvement: " << std::setprecision(1) << ((avg_book - avg_opt) / avg_book * 100.0) << "%\n";

    return 0;
}

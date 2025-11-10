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

void compute_cmma_global_constant(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                                   std::vector<double>& output, double global_const) {
    int n = bars.size();
    output.assign(n, 0.0);
    int front_bad = std::max(lookback, atr_length);

    for (int icase = front_bad; icase < n; ++icase) {
        // MA of log prices EXCLUDING current bar
        double sum = 0.0;
        for (int k = icase - lookback; k < icase; ++k) {
            sum += std::log(bars[k].close);
        }
        double log_ma = sum / lookback;

        // ATR (log-based)
        double sum_tr = 0.0;
        for (int i = icase - atr_length + 1; i <= icase; ++i) {
            double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                               bars[i].high / bars[i-1].close,
                                               bars[i-1].close / bars[i].low}));
            sum_tr += tr_log;
        }
        double atr_val = sum_tr / atr_length;

        if (atr_val > 0.0) {
            // NEW FORMULA: denominator is just ATR (no sqrt(k+1))
            double delta = std::log(bars[icase].close) - log_ma;
            double z = global_const * delta / atr_val;
            output[icase] = 100.0 * tssb::normal_cdf(z) - 50.0;
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

    std::cout << "TESTING GLOBAL CONSTANT FORMULA\n";
    std::cout << "================================\n\n";
    std::cout << "Hypothesis: C_fit = A * sqrt(k+1)\n";
    std::cout << "  → Cancels sqrt(k+1) in denominator\n";
    std::cout << "  → Formula: 100 * Φ(A * Δ / ATR) - 50\n\n";

    struct Test { std::string name; int lookback; int atr; double optimal_c; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250, 0.31},
        {"CMMA_M", 50, 250, 0.68},
        {"CMMA_L", 120, 250, 1.04}
    };

    // Verify pattern: C = A * sqrt(k+1)
    std::cout << "Checking C = A * sqrt(k+1) pattern:\n";
    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "k"
              << std::setw(15) << "sqrt(k+1)"
              << std::setw(12) << "Measured C"
              << std::setw(12) << "C/sqrt(k+1)" << "\n";
    std::cout << std::string(63, '-') << "\n";

    std::vector<double> A_values;
    for (const auto& test : tests) {
        double sqrt_kp1 = std::sqrt(test.lookback + 1.0);
        double A = test.optimal_c / sqrt_kp1;
        A_values.push_back(A);

        std::cout << std::setw(12) << test.name
                  << std::setw(12) << test.lookback
                  << std::fixed << std::setprecision(4)
                  << std::setw(15) << sqrt_kp1
                  << std::setw(12) << test.optimal_c
                  << std::setw(12) << A << "\n";
    }

    double avg_A = (A_values[0] + A_values[1] + A_values[2]) / 3.0;
    double std_A = 0.0;
    for (double A : A_values) std_A += (A - avg_A) * (A - avg_A);
    std_A = std::sqrt(std_A / 3.0);

    std::cout << "\nAverage A: " << std::setprecision(6) << avg_A << "\n";
    std::cout << "Std Dev A: " << std_A << "\n";

    if (std_A < avg_A * 0.05) {
        std::cout << "✓ A is constant! Pattern confirmed: C = " << std::setprecision(4) << avg_A << " * sqrt(k+1)\n";
    } else {
        std::cout << "✗ A varies significantly, pattern doesn't hold perfectly\n";
    }

    // Test the global constant formula
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "TESTING GLOBAL CONSTANT FORMULA\n";
    std::cout << "Formula: 100 * Φ(" << std::setprecision(4) << avg_A << " * Δ / ATR) - 50\n";
    std::cout << std::string(70, '=') << "\n\n";

    std::cout << std::setw(12) << "Indicator"
              << std::setw(15) << "MAE (lookup)"
              << std::setw(15) << "MAE (global)"
              << std::setw(15) << "Difference" << "\n";
    std::cout << std::string(57, '-') << "\n";

    for (const auto& test : tests) {
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        // MAE with lookup C
        std::vector<double> output_lookup;
        int n = ohlcv_bars.size();
        int front_bad = std::max(test.lookback, test.atr);
        output_lookup.assign(n, 0.0);

        for (int icase = front_bad; icase < n; ++icase) {
            double sum = 0.0;
            for (int k = icase - test.lookback; k < icase; ++k) {
                sum += std::log(ohlcv_bars[k].close);
            }
            sum /= test.lookback;

            double sum_tr = 0.0;
            for (int i = icase - test.atr + 1; i <= icase; ++i) {
                double tr = std::log(std::max({ohlcv_bars[i].high / ohlcv_bars[i].low,
                                               ohlcv_bars[i].high / ohlcv_bars[i-1].close,
                                               ohlcv_bars[i-1].close / ohlcv_bars[i].low}));
                sum_tr += tr;
            }
            double atr_val = sum_tr / test.atr;

            if (atr_val > 0.0) {
                double denom = atr_val * std::sqrt(test.lookback + 1.0);
                double raw_val = (std::log(ohlcv_bars[icase].close) - sum) / denom;
                output_lookup[icase] = 100.0 * tssb::normal_cdf(test.optimal_c * raw_val) - 50.0;
            }
        }

        double mae_lookup = 0.0;
        int count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                mae_lookup += std::abs(output_lookup[i] - expected[i]);
                count++;
            }
        }
        mae_lookup /= count;

        // MAE with global constant
        std::vector<double> output_global;
        compute_cmma_global_constant(ohlcv_bars, test.lookback, test.atr, output_global, avg_A);

        double mae_global = 0.0;
        count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                mae_global += std::abs(output_global[i] - expected[i]);
                count++;
            }
        }
        mae_global /= count;

        std::cout << std::setw(12) << test.name
                  << std::fixed << std::setprecision(6)
                  << std::setw(15) << mae_lookup
                  << std::setw(15) << mae_global
                  << std::setprecision(4)
                  << std::setw(15) << (mae_global - mae_lookup);

        if (std::abs(mae_global - mae_lookup) < 0.001) {
            std::cout << "  ✓ Same!";
        } else if (mae_global < mae_lookup) {
            std::cout << "  ↓ Better!";
        } else {
            std::cout << "  ↑ Worse";
        }
        std::cout << "\n";
    }

    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "CONCLUSION\n";
    std::cout << std::string(70, '=') << "\n\n";

    std::cout << "The CSV formula is:\n\n";
    std::cout << "  output = 100 * Φ(" << std::setprecision(4) << avg_A << " * Δ / ATR) - 50\n\n";
    std::cout << "where:\n";
    std::cout << "  Δ = log(close) - MA(log(close), k)  [excluding current]\n";
    std::cout << "  ATR = MA(log(TR), " << tests[0].atr << ")              [log-based, including current]\n";
    std::cout << "  Φ = standard normal CDF\n\n";

    std::cout << "Key difference from book:\n";
    std::cout << "  Book:  denominator = ATR * sqrt(k+1)\n";
    std::cout << "  CSV:   denominator = ATR  [no sqrt term]\n\n";

    std::cout << "The global constant " << avg_A << " is independent of lookback period!\n";

    return 0;
}

#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);

    std::cout << "DIAGNOSING PRE-COMPRESSION Z-SCORES\n";
    std::cout << "====================================\n\n";
    std::cout << "Checking implementation details:\n";
    std::cout << "1. ATR computation (log-based)\n";
    std::cout << "2. MA window (excluding current bar)\n";
    std::cout << "3. ATR window alignment (including current bar)\n";
    std::cout << "4. Z-score variance by lookback\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    for (const auto& test : tests) {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << test.name << " (lookback=" << test.lookback << ")\n";
        std::cout << std::string(70, '=') << "\n\n";

        int n = ohlcv_bars.size();
        int front_bad = std::max(test.lookback, test.atr);

        std::vector<double> z_scores;
        std::vector<double> log_closes;
        std::vector<double> log_mas;
        std::vector<double> atr_vals;

        // Sample bar for detailed inspection
        int sample_bar = front_bad + 100;

        for (int icase = front_bad; icase < n; ++icase) {
            // 1. Compute MA of log prices EXCLUDING current bar
            double sum = 0.0;
            for (int k = icase - test.lookback; k < icase; ++k) {
                sum += std::log(ohlcv_bars[k].close);
            }
            double log_ma = sum / test.lookback;

            // 2. Compute ATR (log-based) INCLUDING current bar
            double sum_tr = 0.0;
            for (int i = icase - test.atr + 1; i <= icase; ++i) {
                double term1 = ohlcv_bars[i].high / ohlcv_bars[i].low;
                double term2 = ohlcv_bars[i].high / ohlcv_bars[i-1].close;
                double term3 = ohlcv_bars[i-1].close / ohlcv_bars[i].low;
                double max_term = std::max({term1, term2, term3});
                sum_tr += std::log(max_term);
            }
            double atr_val = sum_tr / test.atr;

            // 3. Compute z-score (pre-compression value)
            double log_close = std::log(ohlcv_bars[icase].close);
            double denom = atr_val * std::sqrt(test.lookback + 1.0);
            double z = (log_close - log_ma) / denom;

            z_scores.push_back(z);

            if (icase == sample_bar) {
                log_closes.push_back(log_close);
                log_mas.push_back(log_ma);
                atr_vals.push_back(atr_val);

                std::cout << "Detailed computation for bar " << sample_bar << ":\n";
                std::cout << "  Close: " << std::fixed << std::setprecision(2) << ohlcv_bars[icase].close << "\n";
                std::cout << "  log(Close): " << std::setprecision(8) << log_close << "\n";

                // Show MA window
                std::cout << "  MA window: [" << (icase - test.lookback) << ", " << (icase - 1) << "] (excludes current)\n";
                std::cout << "  MA of log prices: " << log_ma << "\n";

                // Show ATR window
                std::cout << "  ATR window: [" << (icase - test.atr + 1) << ", " << icase << "] (includes current)\n";
                std::cout << "  ATR (log-based): " << std::setprecision(8) << atr_val << "\n";

                std::cout << "  Denominator: " << atr_val << " * sqrt(" << test.lookback << " + 1) = "
                          << std::setprecision(8) << denom << "\n";
                std::cout << "  Numerator: " << log_close << " - " << log_ma << " = "
                          << (log_close - log_ma) << "\n";
                std::cout << "  Z-score: " << std::setprecision(6) << z << "\n\n";
            }
        }

        // Statistics on z-scores
        double mean_z = 0.0;
        for (double z : z_scores) mean_z += z;
        mean_z /= z_scores.size();

        double var_z = 0.0;
        for (double z : z_scores) var_z += (z - mean_z) * (z - mean_z);
        var_z /= z_scores.size();
        double std_z = std::sqrt(var_z);

        std::cout << "Z-score statistics (n=" << z_scores.size() << "):\n";
        std::cout << "  Mean: " << std::fixed << std::setprecision(6) << mean_z;
        if (std::abs(mean_z) > 0.01) std::cout << "  ⚠ Should be ≈0";
        std::cout << "\n";
        std::cout << "  Std Dev: " << std_z;
        if (std::abs(std_z - 1.0) > 0.2) std::cout << "  ⚠ Should be ≈1.0 for normalized";
        std::cout << "\n";
        std::cout << "  Variance: " << var_z << "\n";

        // Percentiles
        std::vector<double> sorted_z = z_scores;
        std::sort(sorted_z.begin(), sorted_z.end());
        double p01 = sorted_z[sorted_z.size() * 0.01];
        double p99 = sorted_z[sorted_z.size() * 0.99];
        double p25 = sorted_z[sorted_z.size() * 0.25];
        double p75 = sorted_z[sorted_z.size() * 0.75];

        std::cout << "  Percentiles:\n";
        std::cout << "    1%: " << std::setprecision(4) << p01 << "\n";
        std::cout << "    25%: " << p25 << "\n";
        std::cout << "    75%: " << p75 << "\n";
        std::cout << "    99%: " << p99 << "\n";
        std::cout << "    Range: [" << sorted_z.front() << ", " << sorted_z.back() << "]\n";
    }

    // Compare variances across lookbacks
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "VARIANCE COMPARISON ACROSS LOOKBACKS\n";
    std::cout << std::string(70, '=') << "\n\n";

    std::vector<double> variances;
    for (const auto& test : tests) {
        int n = ohlcv_bars.size();
        int front_bad = std::max(test.lookback, test.atr);

        std::vector<double> z_scores;
        for (int icase = front_bad; icase < n; ++icase) {
            double sum = 0.0;
            for (int k = icase - test.lookback; k < icase; ++k) {
                sum += std::log(ohlcv_bars[k].close);
            }
            double log_ma = sum / test.lookback;

            double sum_tr = 0.0;
            for (int i = icase - test.atr + 1; i <= icase; ++i) {
                double term = std::max({ohlcv_bars[i].high / ohlcv_bars[i].low,
                                       ohlcv_bars[i].high / ohlcv_bars[i-1].close,
                                       ohlcv_bars[i-1].close / ohlcv_bars[i].low});
                sum_tr += std::log(term);
            }
            double atr_val = sum_tr / test.atr;

            if (atr_val > 0.0) {
                double denom = atr_val * std::sqrt(test.lookback + 1.0);
                double z = (std::log(ohlcv_bars[icase].close) - log_ma) / denom;
                z_scores.push_back(z);
            }
        }

        double mean = 0.0;
        for (double z : z_scores) mean += z;
        mean /= z_scores.size();

        double var = 0.0;
        for (double z : z_scores) var += (z - mean) * (z - mean);
        var /= z_scores.size();

        variances.push_back(var);
    }

    std::cout << std::setw(12) << "Indicator"
              << std::setw(12) << "Lookback"
              << std::setw(15) << "Z Variance"
              << std::setw(20) << "Interpretation" << "\n";
    std::cout << std::string(59, '-') << "\n";

    for (size_t i = 0; i < tests.size(); ++i) {
        std::cout << std::setw(12) << tests[i].name
                  << std::setw(12) << tests[i].lookback
                  << std::fixed << std::setprecision(6)
                  << std::setw(15) << variances[i];

        if (i > 0 && variances[i] > variances[i-1] * 1.1) {
            std::cout << "  ⚠ Increasing!";
        } else if (i > 0 && variances[i] < variances[i-1] * 0.9) {
            std::cout << "  Decreasing";
        } else {
            std::cout << "  ✓ Similar";
        }
        std::cout << "\n";
    }

    std::cout << "\n";
    if (variances[0] > variances[2] * 1.2) {
        std::cout << "⚠ WARNING: Variance increases as lookback decreases!\n";
        std::cout << "  → Likely ATR mismatch (not in log-space or wrong alignment)\n";
        std::cout << "  → Short lookbacks get over-compressed\n";
    } else if (variances[0] < variances[2] * 0.8) {
        std::cout << "⚠ WARNING: Variance decreases as lookback decreases!\n";
        std::cout << "  → Short lookbacks get under-compressed\n";
    } else {
        std::cout << "✓ Variance is relatively stable across lookbacks\n";
        std::cout << "  → ATR normalization appears correct\n";
        std::cout << "  → Discrepancy is likely in compression constant C\n";
    }

    return 0;
}

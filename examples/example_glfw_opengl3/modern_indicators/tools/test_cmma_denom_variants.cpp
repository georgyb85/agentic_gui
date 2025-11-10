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

// Test different denominator formulas
void compute_cmma_variant(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                         std::vector<double>& output, int variant) {
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
            double denom;
            switch (variant) {
                case 0: // Original TSSB: atr * sqrt(lookback+1)
                    denom = atr_val * std::sqrt(lookback + 1.0);
                    break;
                case 1: // Variant 1: atr * sqrt(lookback)
                    denom = atr_val * std::sqrt(lookback);
                    break;
                case 2: // Variant 2: atr * lookback
                    denom = atr_val * lookback;
                    break;
                case 3: // Variant 3: just atr
                    denom = atr_val;
                    break;
                case 4: // Variant 4: atr * (lookback+1)
                    denom = atr_val * (lookback + 1.0);
                    break;
                default:
                    denom = atr_val * std::sqrt(lookback + 1.0);
            }

            double raw_val = (std::log(bars[icase].close) - sum) / denom;
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

    std::cout << "CMMA DENOMINATOR FORMULA VARIANTS TEST\n";
    std::cout << "=======================================\n\n";

    struct CMMATest {
        std::string name;
        std::string csv_col;
        int lookback;
        int atr_length;
    };

    std::vector<CMMATest> tests = {
        {"CMMA_S", "CMMA_S", 10, 250},
        {"CMMA_M", "CMMA_M", 50, 250},
        {"CMMA_L", "CMMA_L", 120, 250}
    };

    const char* variant_names[] = {
        "atr * sqrt(lookback+1)  [TSSB source]",
        "atr * sqrt(lookback)",
        "atr * lookback",
        "atr  [no scaling]",
        "atr * (lookback+1)"
    };

    for (const auto& test : tests) {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << test.name << " (lookback=" << test.lookback
                  << ", atr_length=" << test.atr_length << ")\n";
        std::cout << std::string(70, '=') << "\n\n";

        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_col);

        // Find first valid bar
        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        std::cout << std::setw(40) << "Denominator Formula"
                  << std::setw(12) << "MAE"
                  << std::setw(15) << "Median Ratio" << "\n";
        std::cout << std::string(67, '-') << "\n";

        double best_mae = 1e10;
        int best_variant = -1;

        for (int variant = 0; variant < 5; ++variant) {
            std::vector<double> output;
            compute_cmma_variant(ohlcv_bars, test.lookback, test.atr_length, output, variant);

            double mae = 0.0;
            std::vector<double> ratios;
            int count = 0;

            for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    double error = std::abs(output[i] - expected[i]);
                    mae += error;

                    // Calculate ratio for non-zero CSV values
                    if (std::abs(expected[i]) > 0.1) {
                        ratios.push_back(output[i] / expected[i]);
                    }
                    count++;
                }
            }
            mae /= count;

            // Calculate median ratio
            std::sort(ratios.begin(), ratios.end());
            double median_ratio = ratios[ratios.size() / 2];

            std::cout << std::setw(40) << variant_names[variant]
                      << std::fixed << std::setprecision(6)
                      << std::setw(12) << mae
                      << std::setw(15) << median_ratio;

            if (mae < best_mae) {
                best_mae = mae;
                best_variant = variant;
            }

            if (mae < 0.01) {
                std::cout << "  ✓✓✓ PERFECT!";
            } else if (mae < 0.1) {
                std::cout << "  ✓✓ EXCELLENT";
            } else if (mae < 1.0) {
                std::cout << "  ✓ GOOD";
            }

            std::cout << "\n";
        }

        std::cout << "\nBest: " << variant_names[best_variant]
                  << " (MAE: " << best_mae << ")\n";
    }

    return 0;
}

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

// Compute CMMA with custom compression constant
void compute_cmma_with_compression(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                                  std::vector<double>& output, double compression_const) {
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
            output[icase] = 100.0 * tssb::normal_cdf(compression_const * raw_val) - 50.0;
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

    std::cout << "CMMA COMPRESSION CONSTANT SEARCH\n";
    std::cout << "=================================\n\n";

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

    // Test compression constants from 0.25 to 5.0
    std::vector<double> compression_values;
    for (double c = 0.25; c <= 5.0; c += 0.05) {
        compression_values.push_back(c);
    }

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

        double best_mae = 1e10;
        double best_compression = 0;

        for (double comp : compression_values) {
            std::vector<double> output;
            compute_cmma_with_compression(ohlcv_bars, test.lookback, test.atr_length, output, comp);

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
                best_compression = comp;
            }
        }

        std::cout << "Best compression: " << std::fixed << std::setprecision(3) << best_compression
                  << "  MAE: " << std::setprecision(6) << best_mae;

        if (best_mae < 0.01) {
            std::cout << "  ✓✓✓ PERFECT MATCH!";
        } else if (best_mae < 0.1) {
            std::cout << "  ✓✓ EXCELLENT";
        } else if (best_mae < 1.0) {
            std::cout << "  ✓ GOOD";
        } else {
            std::cout << "  ✗ HIGH ERROR";
        }
        std::cout << "\n\n";

        // Show first 10 bars with best compression
        std::vector<double> best_output;
        compute_cmma_with_compression(ohlcv_bars, test.lookback, test.atr_length, best_output, best_compression);

        std::cout << "First 10 bars with best compression (" << best_compression << "):\n";
        std::cout << std::setw(6) << "Bar"
                  << std::setw(12) << "CSV"
                  << std::setw(12) << "Computed"
                  << std::setw(12) << "Error" << "\n";
        std::cout << std::string(42, '-') << "\n";

        for (int i = 0; i < 10; ++i) {
            size_t idx = first_valid + i;
            double error = best_output[idx] - expected[idx];
            std::cout << std::setw(6) << idx
                      << std::fixed << std::setprecision(4)
                      << std::setw(12) << expected[idx]
                      << std::setw(12) << best_output[idx]
                      << std::setw(12) << error << "\n";
        }
    }

    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "SUMMARY\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << "TSSB source uses compression = 1.0\n";
    std::cout << "CSV appears to use different compression for each CMMA variant.\n";
    std::cout << std::string(70, '=') << "\n";

    return 0;
}

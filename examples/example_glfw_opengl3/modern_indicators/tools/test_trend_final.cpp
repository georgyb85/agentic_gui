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

// Compute TREND with specified compression constant
double compute_trend_with_compression(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                                     std::vector<double>& output, double compression_const) {
    int n = bars.size();
    output.assign(n, 0.0);

    // Compute Legendre coefficients (same as TSSB)
    std::vector<double> c1(lookback);
    double sum = 0.0;
    for (int i = 0; i < lookback; ++i) {
        c1[i] = 2.0 * i / (lookback - 1.0) - 1.0;
        sum += c1[i] * c1[i];
    }
    sum = std::sqrt(sum);
    for (int i = 0; i < lookback; ++i) {
        c1[i] /= sum;
    }

    int front_bad = std::max(lookback - 1, atr_length);

    for (int icase = front_bad; icase < n; ++icase) {
        // Compute dot product and mean
        double dot_prod = 0.0;
        double mean = 0.0;
        for (int k = 0; k < lookback; ++k) {
            int idx = icase - lookback + 1 + k;
            double price = std::log(bars[idx].close);
            mean += price;
            dot_prod += price * c1[k];
        }
        mean /= lookback;

        // Compute ATR
        double sum_tr = 0.0;
        for (int i = icase - atr_length + 1; i <= icase; ++i) {
            double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                               bars[i].high / bars[i-1].close,
                                               bars[i-1].close / bars[i].low}));
            sum_tr += tr_log;
        }
        double atr_val = sum_tr / atr_length;

        int k_factor = (lookback == 2) ? 2 : (lookback - 1);
        double denom = atr_val * k_factor;

        double indicator = dot_prod * 2.0 / (denom + 1.e-60);

        // Compute R-squared
        double yss = 0.0;
        double rsq_sum = 0.0;
        for (int k = 0; k < lookback; ++k) {
            int idx = icase - lookback + 1 + k;
            double price = std::log(bars[idx].close);
            double diff = price - mean;
            yss += diff * diff;
            double pred = dot_prod * c1[k];
            double error = diff - pred;
            rsq_sum += error * error;
        }
        double rsq = 1.0 - rsq_sum / (yss + 1.e-60);
        if (rsq < 0.0) rsq = 0.0;

        indicator *= rsq;

        // Apply compression
        output[icase] = 100.0 * tssb::normal_cdf(compression_const * indicator) - 50.0;
    }

    return 0.0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "FINAL TREND COMPRESSION SEARCH\n";
    std::cout << "===============================\n\n";

    struct TrendTest {
        std::string name;
        std::string csv_col;
        int lookback;
        int atr_length;
    };

    std::vector<TrendTest> tests = {
        {"TREND_S100", "TREND_S100", 10, 100},
        {"TREND_M100", "TREND_M100", 50, 100},
        {"TREND_L100", "TREND_L100", 120, 100}
    };

    // Fine grid search around 1.5
    std::vector<double> compression_values;
    for (double c = 1.0; c <= 2.0; c += 0.05) {
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
            compute_trend_with_compression(ohlcv_bars, test.lookback, test.atr_length, output, comp);

            double mae = 0.0;
            double max_error = 0.0;
            int count = 0;

            for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    double error = std::abs(output[i] - expected[i]);
                    mae += error;
                    max_error = std::max(max_error, error);
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
        compute_trend_with_compression(ohlcv_bars, test.lookback, test.atr_length, best_output, best_compression);

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
    std::cout << "TREND indicators do NOT match CSV with any tested variations.\n";
    std::cout << "Best results with compression ~1.5, but MAE still ~4-7.\n";
    std::cout << "This suggests a fundamental algorithmic difference, not just\n";
    std::cout << "a parameter difference. The CSV may have been generated with:\n";
    std::cout << "  - Different polynomial (not Legendre)\n";
    std::cout << "  - Different R-squared formula\n";
    std::cout << "  - Different ATR calculation method\n";
    std::cout << "  - Or a completely different trend computation\n";
    std::cout << std::string(70, '=') << "\n";

    return 0;
}

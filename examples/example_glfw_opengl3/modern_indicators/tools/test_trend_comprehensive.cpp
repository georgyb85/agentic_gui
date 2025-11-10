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

// Compute TREND with different options
double compute_trend_option(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                           std::vector<double>& output, int atr_for_norm, double compression_const) {
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

    // Determine which ATR length to use for normalization
    int atr_len_to_use = (atr_for_norm == 0) ? atr_length : atr_for_norm;
    int front_bad = std::max(lookback - 1, atr_len_to_use);

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
        for (int i = icase - atr_len_to_use + 1; i <= icase; ++i) {
            double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                               bars[i].high / bars[i-1].close,
                                               bars[i-1].close / bars[i].low}));
            sum_tr += tr_log;
        }
        double atr_val = sum_tr / atr_len_to_use;

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

    std::cout << "COMPREHENSIVE TREND VARIANT TESTING\n";
    std::cout << "====================================\n\n";

    // Test TREND_S100 only to save time
    std::string csv_col = "TREND_S100";
    int lookback = 10;
    int atr_length = 100;

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, csv_col);

    // Find first valid bar
    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            first_valid = i;
            break;
        }
    }

    std::cout << "Testing " << csv_col << " (lookback=" << lookback << ", atr_length=" << atr_length << ")\n\n";

    // Test different compression constants
    std::cout << "=== COMPRESSION CONSTANT VARIATIONS ===\n\n";
    std::vector<double> compression_constants = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0};

    double best_mae = 1e10;
    double best_compression = 0;

    for (double comp : compression_constants) {
        std::vector<double> output;
        compute_trend_option(ohlcv_bars, lookback, atr_length, output, 0, comp);

        double mae = 0.0;
        int count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                mae += std::abs(output[i] - expected[i]);
                count++;
            }
        }
        mae /= count;

        std::cout << "  Compression = " << std::fixed << std::setprecision(2) << std::setw(6) << comp
                  << "  MAE: " << std::setprecision(6) << std::setw(10) << mae;

        if (mae < 0.01) {
            std::cout << "  ✓✓✓ PERFECT MATCH!";
        } else if (mae < 0.1) {
            std::cout << "  ✓✓ EXCELLENT";
        } else if (mae < 1.0) {
            std::cout << "  ✓ GOOD";
        }
        std::cout << "\n";

        if (mae < best_mae) {
            best_mae = mae;
            best_compression = comp;
        }
    }

    std::cout << "\nBest compression: " << best_compression << " (MAE: " << best_mae << ")\n\n";

    // Test different ATR normalization lengths
    std::cout << "=== ATR NORMALIZATION LENGTH VARIATIONS ===\n";
    std::cout << "(Using best compression constant: " << best_compression << ")\n\n";

    std::vector<int> atr_norm_lengths = {lookback, 20, 50, 75, 100, 150, 200};

    double best_mae2 = 1e10;
    int best_atr_norm = 0;

    for (int atr_norm : atr_norm_lengths) {
        std::vector<double> output;
        compute_trend_option(ohlcv_bars, lookback, atr_length, output, atr_norm, best_compression);

        double mae = 0.0;
        int count = 0;
        for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                mae += std::abs(output[i] - expected[i]);
                count++;
            }
        }
        mae /= count;

        std::cout << "  ATR norm length = " << std::setw(4) << atr_norm
                  << "  MAE: " << std::fixed << std::setprecision(6) << std::setw(10) << mae;

        if (mae < 0.01) {
            std::cout << "  ✓✓✓ PERFECT MATCH!";
        } else if (mae < 0.1) {
            std::cout << "  ✓✓ EXCELLENT";
        } else if (mae < 1.0) {
            std::cout << "  ✓ GOOD";
        }
        std::cout << "\n";

        if (mae < best_mae2) {
            best_mae2 = mae;
            best_atr_norm = atr_norm;
        }
    }

    std::cout << "\nBest ATR norm length: " << best_atr_norm << " (MAE: " << best_mae2 << ")\n\n";

    // Show first 10 bars with best settings
    std::vector<double> best_output;
    compute_trend_option(ohlcv_bars, lookback, atr_length, best_output, best_atr_norm, best_compression);

    std::cout << "First 10 bars with best settings:\n";
    std::cout << "(compression=" << best_compression << ", atr_norm=" << best_atr_norm << ")\n\n";
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

    return 0;
}

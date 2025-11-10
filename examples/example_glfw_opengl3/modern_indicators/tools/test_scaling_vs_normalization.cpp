#include "helpers/WaveletHelpers.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

using namespace tssb;
using namespace tssb::validation;
using namespace tssb::helpers;

int main() {
    // Load data
    auto ohlcv_bars = OHLCVParser::parse_file("/mnt/c/masters/timothy masters/btc25_3.txt");
    auto tssb_bars = TSBBOutputParser::parse_file("/mnt/c/masters/timothy masters/BTC25_3 HM.CSV");

    // Extract expected REAL_MORLET_10 values
    std::vector<double> expected = TSBBOutputParser::extract_indicator_aligned(
        tssb_bars, ohlcv_bars, "REAL_MORLET_10");

    // Prepare log close data
    std::vector<double> log_close(ohlcv_bars.size());
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        log_close[i] = std::log(ohlcv_bars[i].close + 1e-10);
    }

    // Compute raw Morlet wavelet values
    const int period = 10;
    const int width = 2 * period;
    const int lag = width;
    const int npts = 2 * width + 1;

    MorletTransform morlet(period, width, lag, true); // real component

    std::vector<double> raw_values(ohlcv_bars.size(), std::numeric_limits<double>::quiet_NaN());

    for (size_t i = npts - 1; i < ohlcv_bars.size(); ++i) {
        std::vector<double> data_window(npts);
        for (size_t j = 0; j < npts; ++j) {
            data_window[j] = log_close[i - j];
        }
        raw_values[i] = morlet.transform(data_window.data(), npts);
    }

    std::cout << "Testing SCALING vs NORMALIZATION for REAL_MORLET_10\n";
    std::cout << std::string(100, '=') << "\n\n";

    const int window = 250;

    std::cout << "Using window = " << window << "\n\n";

    std::cout << std::setw(10) << "C_value"
              << std::setw(15) << "SCALING MAE"
              << std::setw(18) << "NORMALIZE MAE"
              << std::setw(15) << "Difference"
              << std::setw(12) << "Better\n";
    std::cout << std::string(70, '-') << "\n";

    std::vector<double> c_values = {0.10, 0.20, 0.25, 0.30, 0.40, 0.50, 0.75, 1.00, 1.50, 2.00, 3.00, 4.00};

    double best_scaling_mae = 1e10;
    double best_scaling_c = 0.25;
    double best_norm_mae = 1e10;
    double best_norm_c = 0.25;

    for (double c : c_values) {
        // Apply SCALING compression
        std::vector<double> scaling_compressed(ohlcv_bars.size(), 0.0);
        std::vector<double> norm_compressed(ohlcv_bars.size(), 0.0);

        for (size_t i = window; i < ohlcv_bars.size(); ++i) {
            if (!std::isfinite(raw_values[i])) continue;

            // Build historical window
            std::vector<double> history;
            for (int j = 1; j <= window; ++j) {
                size_t idx = i - j;
                if (std::isfinite(raw_values[idx])) {
                    history.push_back(raw_values[idx]);
                }
            }

            if (history.empty()) continue;

            double median = compute_median(history);
            double iqr = compute_iqr(history);

            // SCALING (no median)
            scaling_compressed[i] = compress_scaling(raw_values[i], iqr, c);

            // NORMALIZATION (with median) - this is compress_to_range
            norm_compressed[i] = compress_to_range(raw_values[i], median, iqr, c);
        }

        // Compute MAE for both
        auto compute_mae = [&](const std::vector<double>& compressed) {
            double sum_abs_error = 0.0;
            int valid_count = 0;
            for (size_t i = 0; i < std::min(expected.size(), compressed.size()); ++i) {
                if (std::isfinite(expected[i]) && std::isfinite(compressed[i])) {
                    sum_abs_error += std::fabs(compressed[i] - expected[i]);
                    valid_count++;
                }
            }
            return valid_count > 0 ? sum_abs_error / valid_count : 0.0;
        };

        double scaling_mae = compute_mae(scaling_compressed);
        double norm_mae = compute_mae(norm_compressed);

        if (scaling_mae < best_scaling_mae) {
            best_scaling_mae = scaling_mae;
            best_scaling_c = c;
        }
        if (norm_mae < best_norm_mae) {
            best_norm_mae = norm_mae;
            best_norm_c = c;
        }

        std::cout << std::setw(10) << std::fixed << std::setprecision(2) << c
                  << std::setw(15) << std::setprecision(4) << scaling_mae
                  << std::setw(18) << norm_mae
                  << std::setw(15) << (scaling_mae - norm_mae)
                  << std::setw(12) << (scaling_mae < norm_mae ? "SCALING" : "NORMALIZE") << "\n";
    }

    std::cout << "\n" << std::string(100, '=') << "\n";
    std::cout << "BEST SCALING:\n";
    std::cout << "  C value: " << best_scaling_c << "\n";
    std::cout << "  MAE: " << best_scaling_mae << "\n\n";

    std::cout << "BEST NORMALIZATION:\n";
    std::cout << "  C value: " << best_norm_c << "\n";
    std::cout << "  MAE: " << best_norm_mae << "\n\n";

    std::cout << "WINNER: " << (best_scaling_mae < best_norm_mae ? "SCALING" : "NORMALIZATION") << "\n";
    std::cout << "Improvement: " << std::fabs(best_scaling_mae - best_norm_mae) << "\n";

    return 0;
}

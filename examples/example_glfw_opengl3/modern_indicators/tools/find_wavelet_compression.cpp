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

    std::cout << "Testing different compression parameters for REAL_MORLET_10\n";
    std::cout << std::string(80, '=') << "\n\n";

    // Test different compression constants
    std::vector<double> c_values = {0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.50, 0.75, 1.00, 1.50, 2.00};
    std::vector<int> window_sizes = {250, 500, 750, 1000, 1250, 1500};

    std::cout << std::setw(10) << "C_value"
              << std::setw(12) << "Window"
              << std::setw(12) << "MAE"
              << std::setw(12) << "Mean Err"
              << std::setw(12) << "Max Err"
              << std::setw(12) << "RMSE\n";
    std::cout << std::string(70, '-') << "\n";

    double best_mae = 1e10;
    double best_c = 0.25;
    int best_window = 1000;

    for (int window : window_sizes) {
        for (double c : c_values) {
            // Apply compression
            std::vector<double> compressed(ohlcv_bars.size(), 0.0);

            for (size_t i = window; i < ohlcv_bars.size(); ++i) {
                if (!std::isfinite(raw_values[i])) continue;

                // Build historical window (excluding current bar)
                std::vector<double> history;
                history.reserve(window);
                for (int j = 1; j <= window; ++j) {
                    size_t idx = i - j;
                    if (std::isfinite(raw_values[idx])) {
                        history.push_back(raw_values[idx]);
                    }
                }

                if (history.empty()) continue;

                // Compute median and IQR
                double median = compute_median(history);
                double iqr = compute_iqr(history);

                // Apply compression: V = 100*Φ(c*(X-median)/IQR) - 50
                compressed[i] = compress_to_range(raw_values[i], median, iqr, c);
            }

            // Compute MAE
            double sum_error = 0.0;
            double sum_abs_error = 0.0;
            double sum_squared_error = 0.0;
            double max_error = 0.0;
            int valid_count = 0;

            for (size_t i = 0; i < std::min(expected.size(), compressed.size()); ++i) {
                if (std::isfinite(expected[i]) && std::isfinite(compressed[i])) {
                    double error = compressed[i] - expected[i];
                    double abs_error = std::fabs(error);

                    sum_error += error;
                    sum_abs_error += abs_error;
                    sum_squared_error += error * error;
                    max_error = std::max(max_error, abs_error);
                    valid_count++;
                }
            }

            double mean_error = valid_count > 0 ? sum_error / valid_count : 0.0;
            double mae = valid_count > 0 ? sum_abs_error / valid_count : 0.0;
            double rmse = valid_count > 0 ? std::sqrt(sum_squared_error / valid_count) : 0.0;

            if (mae < best_mae) {
                best_mae = mae;
                best_c = c;
                best_window = window;
            }

            std::cout << std::setw(10) << std::fixed << std::setprecision(2) << c
                      << std::setw(12) << window
                      << std::setw(12) << std::setprecision(4) << mae
                      << std::setw(12) << mean_error
                      << std::setw(12) << max_error
                      << std::setw(12) << rmse << "\n";
        }
    }

    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "BEST PARAMETERS:\n";
    std::cout << "  C value: " << best_c << "\n";
    std::cout << "  Window: " << best_window << "\n";
    std::cout << "  MAE: " << best_mae << "\n";

    return 0;
}

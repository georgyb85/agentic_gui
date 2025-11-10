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

    // Extract expected REAL_MORLET_10 values (these are compressed)
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

    // Apply compression with best parameters (c=0.75, window=250, SCALING)
    const double c = 0.75;
    const int window = 250;
    std::vector<double> compressed(ohlcv_bars.size(), 0.0);

    for (size_t i = window; i < ohlcv_bars.size(); ++i) {
        if (!std::isfinite(raw_values[i])) continue;

        std::vector<double> history;
        for (int j = 1; j <= window; ++j) {
            size_t idx = i - j;
            if (std::isfinite(raw_values[idx])) {
                history.push_back(raw_values[idx]);
            }
        }

        if (history.empty()) continue;

        double iqr = compute_iqr(history);
        compressed[i] = compress_scaling(raw_values[i], iqr, c);
    }

    // Print sample raw values, compressed values, and expected
    std::cout << "Diagnostic: Raw vs Compressed vs Expected for REAL_MORLET_10\n";
    std::cout << std::string(100, '=') << "\n\n";

    std::cout << std::setw(8) << "Bar"
              << std::setw(14) << "Raw Value"
              << std::setw(14) << "Compressed"
              << std::setw(14) << "Expected"
              << std::setw(14) << "Error"
              << std::setw(12) << "Raw/IQR\n";
    std::cout << std::string(76, '-') << "\n";

    std::vector<size_t> sample_indices = {1000, 1500, 2000, 2500, 3000, 3500, 4000, 5000, 6000, 7000, 8000};

    for (size_t idx : sample_indices) {
        if (idx < ohlcv_bars.size() && idx >= window) {
            // Compute IQR for this bar
            std::vector<double> history;
            for (int j = 1; j <= window; ++j) {
                size_t hist_idx = idx - j;
                if (std::isfinite(raw_values[hist_idx])) {
                    history.push_back(raw_values[hist_idx]);
                }
            }

            double iqr = history.empty() ? 1.0 : compute_iqr(history);
            double raw_over_iqr = raw_values[idx] / iqr;

            if (std::isfinite(expected[idx]) && std::isfinite(compressed[idx])) {
                double error = compressed[idx] - expected[idx];

                std::cout << std::setw(8) << idx
                          << std::setw(14) << std::setprecision(8) << std::scientific << raw_values[idx]
                          << std::setw(14) << std::setprecision(2) << std::fixed << compressed[idx]
                          << std::setw(14) << expected[idx]
                          << std::setw(14) << error
                          << std::setw(12) << std::setprecision(4) << raw_over_iqr << "\n";
            }
        }
    }

    // Compute stats on raw values
    std::cout << "\n" << std::string(100, '=') << "\n";
    std::cout << "Raw Value Statistics (for bars with expected values):\n\n";

    std::vector<double> valid_raw;
    for (size_t i = 0; i < std::min(expected.size(), raw_values.size()); ++i) {
        if (std::isfinite(expected[i]) && std::isfinite(raw_values[i])) {
            valid_raw.push_back(raw_values[i]);
        }
    }

    if (!valid_raw.empty()) {
        std::sort(valid_raw.begin(), valid_raw.end());
        double raw_min = valid_raw.front();
        double raw_max = valid_raw.back();
        double raw_median = compute_median(valid_raw);
        double raw_iqr = compute_iqr(valid_raw);
        double raw_mean = std::accumulate(valid_raw.begin(), valid_raw.end(), 0.0) / valid_raw.size();

        std::cout << "  Min: " << std::scientific << std::setprecision(6) << raw_min << "\n";
        std::cout << "  Max: " << raw_max << "\n";
        std::cout << "  Mean: " << raw_mean << "\n";
        std::cout << "  Median: " << raw_median << "\n";
        std::cout << "  IQR: " << raw_iqr << "\n";
        std::cout << "  Range: " << (raw_max - raw_min) << "\n";
    }

    return 0;
}

#include "helpers/Fti.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

using namespace tssb;
using namespace tssb::validation;
using namespace tssb::helpers;

int main() {
    auto ohlcv_bars = OHLCVParser::parse_file("/mnt/c/masters/timothy masters/btc25_3.txt");
    std::vector<double> closes;
    for (const auto& bar : ohlcv_bars) {
        closes.push_back(bar.close);
    }

    // Expected values from CSV
    std::vector<double> expected_values = {
        1.456463, 1.656330, 1.664832, 1.762773, 2.023268,
        2.018507, 1.872317, 1.820574, 2.010899, 1.820857,
        1.810930, 1.468696, 1.303073, 1.581533, 1.633281
    };

    // Compute raw FTI values with default params (beta=0.95, noise_cut=0.20)
    FtiFilter filter(true, 10, 10, 6, 36, 0.95, 0.20);
    std::vector<double> raw_fti_values;

    for (size_t i = 0; i < expected_values.size(); ++i) {
        int bar = 1078 + i;
        std::span<const double> history(closes.data(), bar + 1);
        filter.process(history, true);
        raw_fti_values.push_back(filter.fti(10));
    }

    std::cout << "Testing various transformations of raw FTI to match expected:\n\n";

    // Test 1: Simple scaling
    std::cout << "Test 1: Linear scaling (expected = raw * k)\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(12) << "Raw"
              << std::setw(12) << "Expected"
              << std::setw(12) << "Ratio\n";
    std::cout << std::string(42, '-') << "\n";

    double sum_ratio = 0.0;
    for (size_t i = 0; i < expected_values.size(); ++i) {
        double ratio = expected_values[i] / raw_fti_values[i];
        sum_ratio += ratio;
        std::cout << std::setw(6) << (1078 + i)
                  << std::setw(12) << std::setprecision(6) << std::fixed << raw_fti_values[i]
                  << std::setw(12) << expected_values[i]
                  << std::setw(12) << ratio << "\n";
    }
    double avg_ratio = sum_ratio / expected_values.size();
    std::cout << "\nAverage ratio: " << avg_ratio << "\n\n";

    // Test 2: Power transformation
    std::cout << "Test 2: Power transformation (expected = raw^k)\n";
    std::cout << "Trying different exponents:\n";
    std::cout << std::setw(12) << "Exponent"
              << std::setw(14) << "MAE"
              << std::setw(14) << "Max Error\n";
    std::cout << std::string(40, '-') << "\n";

    double best_exp = 1.0;
    double best_mae = 1e10;

    for (double exp = 0.5; exp <= 1.5; exp += 0.05) {
        double sum_error = 0.0;
        double max_error = 0.0;
        for (size_t i = 0; i < expected_values.size(); ++i) {
            double transformed = std::pow(raw_fti_values[i], exp);
            double error = std::fabs(transformed - expected_values[i]);
            sum_error += error;
            max_error = std::max(max_error, error);
        }
        double mae = sum_error / expected_values.size();

        if (mae < best_mae) {
            best_mae = mae;
            best_exp = exp;
        }

        if (std::fmod(exp, 0.1) < 0.01) {
            std::cout << std::setw(12) << std::setprecision(2) << exp
                      << std::setw(14) << std::setprecision(6) << mae
                      << std::setw(14) << max_error << "\n";
        }
    }
    std::cout << "\nBest exponent: " << best_exp << " (MAE: " << best_mae << ")\n\n";

    // Test 3: Logarithmic transformation
    std::cout << "Test 3: Log transformation (expected = a + b*log(raw))\n";
    // Fit linear regression: expected = a + b*log(raw)
    double sum_log_raw = 0.0;
    double sum_expected = 0.0;
    double sum_log_raw_sq = 0.0;
    double sum_log_raw_expected = 0.0;

    for (size_t i = 0; i < expected_values.size(); ++i) {
        double log_raw = std::log(raw_fti_values[i]);
        sum_log_raw += log_raw;
        sum_expected += expected_values[i];
        sum_log_raw_sq += log_raw * log_raw;
        sum_log_raw_expected += log_raw * expected_values[i];
    }

    int n = expected_values.size();
    double b = (n * sum_log_raw_expected - sum_log_raw * sum_expected) /
               (n * sum_log_raw_sq - sum_log_raw * sum_log_raw);
    double a = (sum_expected - b * sum_log_raw) / n;

    std::cout << "Best fit: expected = " << a << " + " << b << " * log(raw)\n";

    double sum_error_log = 0.0;
    double max_error_log = 0.0;
    for (size_t i = 0; i < expected_values.size(); ++i) {
        double transformed = a + b * std::log(raw_fti_values[i]);
        double error = std::fabs(transformed - expected_values[i]);
        sum_error_log += error;
        max_error_log = std::max(max_error_log, error);
    }
    std::cout << "MAE: " << (sum_error_log / n) << ", Max Error: " << max_error_log << "\n\n";

    // Test 4: Square root transformation
    std::cout << "Test 4: Square root transformation (expected = a + b*sqrt(raw))\n";
    double sum_sqrt_raw = 0.0;
    double sum_sqrt_raw_sq = 0.0;
    double sum_sqrt_raw_expected = 0.0;

    for (size_t i = 0; i < expected_values.size(); ++i) {
        double sqrt_raw = std::sqrt(raw_fti_values[i]);
        sum_sqrt_raw += sqrt_raw;
        sum_sqrt_raw_sq += sqrt_raw * sqrt_raw;
        sum_sqrt_raw_expected += sqrt_raw * expected_values[i];
    }

    b = (n * sum_sqrt_raw_expected - sum_sqrt_raw * sum_expected) /
        (n * sum_sqrt_raw_sq - sum_sqrt_raw * sum_sqrt_raw);
    a = (sum_expected - b * sum_sqrt_raw) / n;

    std::cout << "Best fit: expected = " << a << " + " << b << " * sqrt(raw)\n";

    double sum_error_sqrt = 0.0;
    double max_error_sqrt = 0.0;
    for (size_t i = 0; i < expected_values.size(); ++i) {
        double transformed = a + b * std::sqrt(raw_fti_values[i]);
        double error = std::fabs(transformed - expected_values[i]);
        sum_error_sqrt += error;
        max_error_sqrt = std::max(max_error_sqrt, error);
    }
    std::cout << "MAE: " << (sum_error_sqrt / n) << ", Max Error: " << max_error_sqrt << "\n\n";

    // Test best transformation across all bars
    std::cout << "Applying best transformation (power=" << best_exp << ") to all bars:\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(12) << "Expected"
              << std::setw(12) << "Computed"
              << std::setw(12) << "Error"
              << std::setw(10) << "Error %\n";
    std::cout << std::string(52, '-') << "\n";

    for (size_t i = 0; i < expected_values.size(); ++i) {
        double transformed = std::pow(raw_fti_values[i], best_exp);
        double error = transformed - expected_values[i];
        double error_pct = 100.0 * error / expected_values[i];

        std::cout << std::setw(6) << (1078 + i)
                  << std::setw(12) << std::setprecision(6) << expected_values[i]
                  << std::setw(12) << transformed
                  << std::setw(12) << error
                  << std::setw(10) << std::setprecision(2) << error_pct << "%\n";
    }

    return 0;
}

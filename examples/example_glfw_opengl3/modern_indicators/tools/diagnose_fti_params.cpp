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

    // Test different parameter combinations
    std::vector<double> betas = {0.80, 0.85, 0.90, 0.95, 0.99};
    std::vector<double> noise_cuts = {0.10, 0.15, 0.20, 0.25, 0.30};

    std::cout << "Testing FTI parameter combinations at bar 1078\n";
    std::cout << "Expected FTI10 value: 1.456463\n\n";

    std::cout << std::setw(8) << "Beta"
              << std::setw(12) << "NoiseCut"
              << std::setw(14) << "Computed"
              << std::setw(14) << "Error"
              << std::setw(12) << "Error %\n";
    std::cout << std::string(60, '-') << "\n";

    const int bar_idx = 1078;
    const double expected = 1.456463;
    double best_error = 1e10;
    double best_beta = 0.95;
    double best_noise = 0.20;
    double best_value = 0.0;

    for (double beta : betas) {
        for (double noise_cut : noise_cuts) {
            FtiFilter filter(true, 10, 10, 6, 36, beta, noise_cut);

            std::span<const double> history(closes.data(), bar_idx + 1);
            filter.process(history, true);

            double computed = filter.fti(10);
            double error = std::fabs(computed - expected);
            double error_pct = 100.0 * error / expected;

            std::cout << std::setw(8) << std::fixed << std::setprecision(2) << beta
                      << std::setw(12) << noise_cut
                      << std::setw(14) << std::setprecision(6) << computed
                      << std::setw(14) << error
                      << std::setw(12) << std::setprecision(2) << error_pct << "%\n";

            if (error < best_error) {
                best_error = error;
                best_beta = beta;
                best_noise = noise_cut;
                best_value = computed;
            }
        }
    }

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Best match: beta=" << best_beta
              << ", noise_cut=" << best_noise
              << ", value=" << best_value
              << ", error=" << best_error << "\n";

    // Test with best parameters across multiple bars
    std::cout << "\n\nValidating best parameters across bars 1078-1090:\n";
    std::cout << std::setw(6) << "Bar"
              << std::setw(14) << "Expected"
              << std::setw(14) << "Computed"
              << std::setw(14) << "Error"
              << std::setw(12) << "Error %\n";
    std::cout << std::string(60, '-') << "\n";

    std::vector<double> expected_values = {
        1.456463, 1.656330, 1.664832, 1.762773, 2.023268,
        2.018507, 1.872317, 1.820574, 2.010899, 1.820857,
        1.810930, 1.468696, 1.303073
    };

    FtiFilter best_filter(true, 10, 10, 6, 36, best_beta, best_noise);

    for (size_t i = 0; i < expected_values.size(); ++i) {
        int bar = 1078 + i;
        std::span<const double> history(closes.data(), bar + 1);
        best_filter.process(history, true);

        double computed = best_filter.fti(10);
        double error = computed - expected_values[i];
        double error_pct = 100.0 * error / expected_values[i];

        std::cout << std::setw(6) << bar
                  << std::setw(14) << std::setprecision(6) << expected_values[i]
                  << std::setw(14) << computed
                  << std::setw(14) << error
                  << std::setw(12) << std::setprecision(2) << error_pct << "%\n";
    }

    return 0;
}

#include "IndicatorEngine.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

// Correct MA_DIFF from TSSB source
std::vector<double> compute_ma_diff_correct(const std::vector<OHLCVBar>& bars, int short_len, int long_len, int lag) {
    const size_t n = bars.size();
    std::vector<double> result(n, 0.0);
    std::vector<double> close(n), open(n), high(n), low(n);

    for (size_t i = 0; i < n; ++i) {
        close[i] = bars[i].close;
        open[i] = bars[i].open;
        high[i] = bars[i].high;
        low[i] = bars[i].low;
    }

    const int front_bad = long_len + lag;

    for (size_t icase = front_bad; icase < n; ++icase) {
        double long_sum = 0.0;
        for (int k = icase - long_len + 1; k <= (int)icase; ++k) {
            long_sum += close[k - lag];
        }
        long_sum /= long_len;

        double short_sum = 0.0;
        for (int k = icase - short_len + 1; k <= (int)icase; ++k) {
            short_sum += close[k];
        }
        short_sum /= short_len;

        // Random walk variance
        double diff = 0.5 * (long_len - 1.0) + lag;
        diff -= 0.5 * (short_len - 1.0);
        double denom = std::sqrt(std::abs(diff));
        denom *= atr(false, {open.data(), n}, {high.data(), n}, {low.data(), n}, {close.data(), n}, icase, long_len + lag);

        double raw_val = (short_sum - long_sum) / (denom + 1.e-60);
        result[icase] = 100.0 * normal_cdf(1.5 * raw_val) - 50.0;
    }

    return result;
}

struct ValidationResult {
    std::string name;
    int total_bars;
    int valid_bars;
    double mae;
    double max_error;
    int under_0_1;
    int under_1_0;
    int under_5_0;
    bool implemented;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc245.txt> <BTC245 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    if (ohlcv_bars.empty()) {
        std::cerr << "ERROR: " << OHLCVParser::get_last_error() << "\n";
        return 1;
    }

    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    if (tssb_bars.empty()) {
        std::cerr << "ERROR: " << TSBBOutputParser::get_last_error() << "\n";
        return 1;
    }

    std::cout << "===========================================================================\n";
    std::cout << "COMPREHENSIVE INDICATOR VALIDATION\n";
    std::cout << "===========================================================================\n\n";
    std::cout << "OHLCV bars: " << ohlcv_bars.size() << "\n";
    std::cout << "CSV bars: " << tssb_bars.size() << "\n\n";

    size_t csv_start = 1078;
    std::vector<ValidationResult> results;

    // Test MA_DIFF indicators
    std::vector<std::tuple<std::string, int, int, int>> ma_diff_tests = {
        {"MA_DIFF_S", 10, 20, 10},
        {"MA_DIFF_M", 20, 50, 20},
        {"MA_DIFF_L", 50, 200, 50}
    };

    for (const auto& [name, short_len, long_len, lag] : ma_diff_tests) {
        auto result = compute_ma_diff_correct(ohlcv_bars, short_len, long_len, lag);
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);

        ValidationResult vr;
        vr.name = name;
        vr.total_bars = tssb_bars.size();
        vr.valid_bars = 0;
        vr.mae = 0;
        vr.max_error = 0;
        vr.under_0_1 = 0;
        vr.under_1_0 = 0;
        vr.under_5_0 = 0;
        vr.implemented = true;

        double sum_abs_error = 0;
        for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
            size_t ohlcv_idx = csv_start + csv_idx;
            if (std::isfinite(result[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
                double error = std::abs(result[ohlcv_idx] - expected[ohlcv_idx]);
                sum_abs_error += error;
                vr.max_error = std::max(vr.max_error, error);
                if (error < 0.1) vr.under_0_1++;
                if (error < 1.0) vr.under_1_0++;
                if (error < 5.0) vr.under_5_0++;
                vr.valid_bars++;
            }
        }

        vr.mae = vr.valid_bars > 0 ? sum_abs_error / vr.valid_bars : 0;
        results.push_back(vr);
    }

    // Check for other indicators in CSV that we haven't implemented
    std::vector<std::string> csv_indicators;
    if (!tssb_bars.empty()) {
        for (const auto& [name, value] : tssb_bars[0].indicators) {
            csv_indicators.push_back(name);
        }
    }

    // Check which indicators we haven't implemented
    for (const auto& name : csv_indicators) {
        bool found = false;
        for (const auto& res : results) {
            if (res.name == name) {
                found = true;
                break;
            }
        }

        if (!found) {
            ValidationResult vr;
            vr.name = name;
            vr.total_bars = tssb_bars.size();
            vr.valid_bars = 0;
            vr.mae = 0;
            vr.max_error = 0;
            vr.under_0_1 = 0;
            vr.under_1_0 = 0;
            vr.under_5_0 = 0;
            vr.implemented = false;
            results.push_back(vr);
        }
    }

    // Sort results - implemented first
    std::sort(results.begin(), results.end(), [](const ValidationResult& a, const ValidationResult& b) {
        if (a.implemented != b.implemented) return a.implemented > b.implemented;
        return a.mae < b.mae;
    });

    // Print summary table
    std::cout << "VALIDATION RESULTS:\n";
    std::cout << "==================\n\n";
    std::cout << std::left << std::setw(25) << "Indicator"
              << std::right << std::setw(10) << "Status"
              << std::setw(12) << "MAE"
              << std::setw(12) << "Max Err"
              << std::setw(10) << "<0.1"
              << std::setw(10) << "<1.0"
              << std::setw(10) << "<5.0" << "\n";
    std::cout << std::string(87, '-') << "\n";

    int implemented_count = 0;
    int perfect_count = 0; // MAE < 0.1
    int good_count = 0;    // MAE < 1.0

    for (const auto& r : results) {
        std::cout << std::left << std::setw(25) << r.name;

        if (!r.implemented) {
            std::cout << std::right << std::setw(10) << "NOT IMPL"
                      << std::setw(12) << "-"
                      << std::setw(12) << "-"
                      << std::setw(10) << "-"
                      << std::setw(10) << "-"
                      << std::setw(10) << "-" << "\n";
        } else {
            implemented_count++;
            if (r.mae < 0.1) perfect_count++;
            if (r.mae < 1.0) good_count++;

            std::cout << std::right << std::setw(10) << "OK"
                      << std::setw(12) << std::fixed << std::setprecision(4) << r.mae
                      << std::setw(12) << std::setprecision(4) << r.max_error
                      << std::setw(9) << r.under_0_1 << "%"
                      << std::setw(9) << r.under_1_0 << "%"
                      << std::setw(9) << r.under_5_0 << "%" << "\n";
        }
    }

    std::cout << "\n";
    std::cout << "SUMMARY:\n";
    std::cout << "========\n";
    std::cout << "Total indicators in CSV: " << csv_indicators.size() << "\n";
    std::cout << "Implemented: " << implemented_count << "\n";
    std::cout << "Not implemented: " << (csv_indicators.size() - implemented_count) << "\n";
    std::cout << "Perfect (MAE < 0.1): " << perfect_count << "\n";
    std::cout << "Good (MAE < 1.0): " << good_count << "\n";

    std::cout << "\n===========================================================================\n";

    return 0;
}

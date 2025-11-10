#include "IndicatorEngine.hpp"
#include "MathUtils.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

// CORRECT MA_DIFF implementation from TSSB source code (COMP_VAR.CPP lines 420-451)
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
        // Compute long-term and short-term moving averages
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

        // Random walk variance adjustment (TSSB's actual formula!)
        double diff = 0.5 * (long_len - 1.0) + lag;      // Center of long block
        diff -= 0.5 * (short_len - 1.0);                 // Minus center of short block
        double denom = std::sqrt(std::abs(diff));        // SQUARE ROOT!
        denom *= atr(false, {open.data(), n}, {high.data(), n}, {low.data(), n}, {close.data(), n}, icase, long_len + lag);

        // The actual formula with c=1.5 built-in
        double raw_val = (short_sum - long_sum) / (denom + 1.e-60);
        result[icase] = 100.0 * normal_cdf(1.5 * raw_val) - 50.0;  // c=1.5, not 0.5!
    }

    return result;
}

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
    std::cout << "Testing CORRECT MA_DIFF formula from TSSB source code\n";
    std::cout << "===========================================================================\n\n";

    auto result = compute_ma_diff_correct(ohlcv_bars, 10, 20, 10);
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "MA_DIFF_S");

    size_t csv_start = 1078;

    std::cout << "First 20 values:\n\n";
    for (int i = 0; i < 20; ++i) {
        size_t idx = csv_start + i;
        double error = result[idx] - expected[idx];
        std::cout << "  Bar " << std::setw(4) << idx
                  << ": TSSB=" << std::fixed << std::setprecision(4) << std::setw(10) << expected[idx]
                  << ", Ours=" << std::setw(10) << result[idx]
                  << ", Err=" << std::setw(8) << error << "\n";
    }

    // Compute MAE
    double sum_abs_error = 0;
    int count = 0;
    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;
        if (std::isfinite(result[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
            sum_abs_error += std::abs(result[ohlcv_idx] - expected[ohlcv_idx]);
            ++count;
        }
    }

    double mae = count > 0 ? sum_abs_error / count : 0;
    std::cout << "\nOverall MAE: " << std::setprecision(6) << mae << "\n";

    // Error distribution
    int under_0_1 = 0, under_1_0 = 0, under_5_0 = 0;
    for (size_t csv_idx = 0; csv_idx < tssb_bars.size(); ++csv_idx) {
        size_t ohlcv_idx = csv_start + csv_idx;
        if (std::isfinite(result[ohlcv_idx]) && std::isfinite(expected[ohlcv_idx])) {
            double error = std::abs(result[ohlcv_idx] - expected[ohlcv_idx]);
            if (error < 0.1) under_0_1++;
            if (error < 1.0) under_1_0++;
            if (error < 5.0) under_5_0++;
        }
    }

    std::cout << "\nError distribution:\n";
    std::cout << "  |error| < 0.1: " << under_0_1 << " (" << (100.0*under_0_1/count) << "%)\n";
    std::cout << "  |error| < 1.0: " << under_1_0 << " (" << (100.0*under_1_0/count) << "%)\n";
    std::cout << "  |error| < 5.0: " << under_5_0 << " (" << (100.0*under_5_0/count) << "%)\n";

    std::cout << "\n===========================================================================\n";

    return 0;
}

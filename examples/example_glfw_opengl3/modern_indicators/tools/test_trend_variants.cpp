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

// ATR using Wilder's exponential smoothing
double atr_wilder(const std::vector<OHLCVBar>& bars, int index, int length) {
    if (length <= 0 || index < 1) {
        return 0.0;
    }

    const double smoothing = (length - 1.0) / length;

    // Initialize with first TR
    double tr = bars[1].high - bars[1].low;
    tr = std::max(tr, bars[1].high - bars[0].close);
    tr = std::max(tr, bars[0].close - bars[1].low);
    double atr_val = std::log(std::max({bars[1].high / bars[1].low,
                                         bars[1].high / bars[0].close,
                                         bars[0].close / bars[1].low}));

    // Accumulate first 'length' bars
    for (int i = 2; i <= std::min(length, index); ++i) {
        tr = bars[i].high - bars[i].low;
        tr = std::max(tr, bars[i].high - bars[i-1].close);
        tr = std::max(tr, bars[i-1].close - bars[i].low);
        double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                           bars[i].high / bars[i-1].close,
                                           bars[i-1].close / bars[i].low}));
        atr_val += tr_log;
    }

    if (index <= length) {
        return atr_val / std::min(index, length);
    }

    // Average the first 'length' bars
    atr_val /= length;

    // Exponentially smooth remaining bars
    for (int i = length + 1; i <= index; ++i) {
        double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                           bars[i].high / bars[i-1].close,
                                           bars[i-1].close / bars[i].low}));
        atr_val = smoothing * atr_val + (1.0 - smoothing) * tr_log;
    }

    return atr_val;
}

// ATR using EMA with 2/(n+1) factor
double atr_ema(const std::vector<OHLCVBar>& bars, int index, int length) {
    if (length <= 0 || index < 1) {
        return 0.0;
    }

    const double alpha = 2.0 / (length + 1.0);

    // Initialize with SMA of first 'length' bars
    double atr_val = 0.0;
    for (int i = 1; i <= std::min(length, index); ++i) {
        double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                           bars[i].high / bars[i-1].close,
                                           bars[i-1].close / bars[i].low}));
        atr_val += tr_log;
    }

    if (index <= length) {
        return atr_val / std::min(index, length);
    }

    atr_val /= length;

    // EMA smoothing for remaining bars
    for (int i = length + 1; i <= index; ++i) {
        double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                           bars[i].high / bars[i-1].close,
                                           bars[i-1].close / bars[i].low}));
        atr_val = alpha * tr_log + (1.0 - alpha) * atr_val;
    }

    return atr_val;
}

// Compute TREND with custom ATR function
double compute_trend_variant(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                            std::vector<double>& output, int atr_method) {
    int n = bars.size();
    output.assign(n, 0.0);

    // Compute Legendre coefficients
    std::vector<double> c1(lookback), c2(lookback), c3(lookback);
    double sum = 0.0;
    for (int i = 0; i < lookback; ++i) {
        double x = -1.0 + 2.0 * i / (lookback - 1);
        c1[i] = x;
        sum += x * x;
    }
    for (int i = 0; i < lookback; ++i) {
        c1[i] /= std::sqrt(sum);
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

        // Compute ATR based on method
        double atr_val;
        if (atr_method == 0) {
            // SMA (current implementation)
            double sum_tr = 0.0;
            for (int i = icase - atr_length + 1; i <= icase; ++i) {
                double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                                   bars[i].high / bars[i-1].close,
                                                   bars[i-1].close / bars[i].low}));
                sum_tr += tr_log;
            }
            atr_val = sum_tr / atr_length;
        } else if (atr_method == 1) {
            // Wilder's exponential smoothing
            atr_val = atr_wilder(bars, icase, atr_length);
        } else {
            // EMA with 2/(n+1)
            atr_val = atr_ema(bars, icase, atr_length);
        }

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
        output[icase] = 100.0 * tssb::normal_cdf(indicator) - 50.0;
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

    std::cout << "TESTING TREND INDICATOR VARIANTS\n";
    std::cout << "=================================\n\n";

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

    const char* method_names[] = {
        "SMA (current/TSSB source)",
        "Wilder's Exponential",
        "Standard EMA (2/(n+1))"
    };

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
        int best_method = -1;

        for (int method = 0; method < 3; ++method) {
            std::vector<double> output;
            compute_trend_variant(ohlcv_bars, test.lookback, test.atr_length, output, method);

            // Calculate MAE
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

            std::cout << std::setw(30) << method_names[method]
                      << "  MAE: " << std::fixed << std::setprecision(6) << std::setw(10) << mae
                      << "  Max: " << std::setw(10) << max_error;

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
                best_method = method;
            }
        }

        std::cout << "\nBest method: " << method_names[best_method]
                  << " (MAE: " << best_mae << ")\n";

        // Show first 5 bars with best method
        std::vector<double> best_output;
        compute_trend_variant(ohlcv_bars, test.lookback, test.atr_length, best_output, best_method);

        std::cout << "\nFirst 5 bars with best method:\n";
        std::cout << std::setw(6) << "Bar"
                  << std::setw(12) << "CSV"
                  << std::setw(12) << "Computed"
                  << std::setw(12) << "Error" << "\n";
        std::cout << std::string(42, '-') << "\n";

        for (int i = 0; i < 5; ++i) {
            size_t idx = first_valid + i;
            double error = best_output[idx] - expected[idx];
            std::cout << std::setw(6) << idx
                      << std::fixed << std::setprecision(4)
                      << std::setw(12) << expected[idx]
                      << std::setw(12) << best_output[idx]
                      << std::setw(12) << error << "\n";
        }
    }

    return 0;
}

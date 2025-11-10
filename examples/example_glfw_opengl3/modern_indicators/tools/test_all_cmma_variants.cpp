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

// Test different compression functions
void compute_cmma_variant(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                         std::vector<double>& output,
                         int compression_type, double pre_scale, double post_scale) {
    int n = bars.size();
    output.assign(n, 0.0);

    int front_bad = std::max(lookback, atr_length);

    for (int icase = front_bad; icase < n; ++icase) {
        // Compute MA of log prices EXCLUDING current bar
        double sum = 0.0;
        for (int k = icase - lookback; k < icase; ++k) {
            sum += std::log(bars[k].close);
        }
        sum /= lookback;

        // Compute ATR
        double sum_tr = 0.0;
        for (int i = icase - atr_length + 1; i <= icase; ++i) {
            double tr_log = std::log(std::max({bars[i].high / bars[i].low,
                                               bars[i].high / bars[i-1].close,
                                               bars[i-1].close / bars[i].low}));
            sum_tr += tr_log;
        }
        double atr_val = sum_tr / atr_length;

        if (atr_val > 0.0) {
            double denom = atr_val * std::sqrt(lookback + 1.0);
            double raw_val = (std::log(bars[icase].close) - sum) / denom;

            // Apply pre-scaling
            raw_val *= pre_scale;

            // Apply compression
            double compressed;
            switch (compression_type) {
                case 0: // normal_cdf
                    compressed = tssb::normal_cdf(raw_val);
                    break;
                case 1: // tanh
                    compressed = std::tanh(raw_val);
                    break;
                case 2: // tanh centered: (tanh + 1) / 2 to match normal_cdf range [0,1]
                    compressed = (std::tanh(raw_val) + 1.0) / 2.0;
                    break;
                case 3: // No compression (linear)
                    compressed = raw_val;
                    break;
                default:
                    compressed = tssb::normal_cdf(raw_val);
            }

            // Apply post-scaling and centering
            output[icase] = post_scale * compressed - (post_scale / 2.0);
        } else {
            output[icase] = 0.0;
        }
    }
}

void test_indicator(const std::vector<OHLCVBar>& ohlcv_bars,
                   const std::vector<TSBBIndicatorBar>& tssb_bars,
                   const std::string& name, int lookback, int atr_length) {

    std::cout << "\n" << std::string(75, '=') << "\n";
    std::cout << name << " (lookback=" << lookback << ", atr_length=" << atr_length << ")\n";
    std::cout << std::string(75, '=') << "\n\n";

    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, name);

    // Find first valid bar
    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            first_valid = i;
            break;
        }
    }

    struct Variant {
        const char* name;
        int compression_type;
        double pre_scale;
        double post_scale;
    };

    std::vector<Variant> variants = {
        // Current TSSB implementation
        {"100 * normal_cdf(1.0 * x) - 50  [CURRENT]", 0, 1.0, 100.0},

        // Tanh variants
        {"100 * tanh(x) [symmetric]", 1, 1.0, 100.0},
        {"100 * tanh(0.5*x)", 1, 0.5, 100.0},
        {"100 * tanh(2.0*x)", 1, 2.0, 100.0},
        {"100 * tanh(1.5*x)", 1, 1.5, 100.0},

        // Tanh centered to [0,1] like normal_cdf
        {"100 * (tanh(x)+1)/2 - 50", 2, 1.0, 100.0},
        {"100 * (tanh(0.5*x)+1)/2 - 50", 2, 0.5, 100.0},

        // Different normal_cdf scalings
        {"100 * normal_cdf(0.5*x) - 50", 0, 0.5, 100.0},
        {"100 * normal_cdf(2.0*x) - 50", 0, 2.0, 100.0},
        {"100 * normal_cdf(1.5*x) - 50", 0, 1.5, 100.0},

        // Different post-scaling
        {"50 * normal_cdf(x) - 25", 0, 1.0, 50.0},
        {"200 * normal_cdf(x) - 100", 0, 1.0, 200.0},
    };

    std::cout << std::setw(50) << "Compression Formula"
              << std::setw(12) << "MAE"
              << std::setw(15) << "Max Error" << "\n";
    std::cout << std::string(77, '-') << "\n";

    double best_mae = 1e10;
    int best_idx = -1;

    for (size_t v = 0; v < variants.size(); ++v) {
        const auto& variant = variants[v];
        std::vector<double> output;
        compute_cmma_variant(ohlcv_bars, lookback, atr_length, output,
                           variant.compression_type, variant.pre_scale, variant.post_scale);

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

        std::cout << std::setw(50) << variant.name
                  << std::fixed << std::setprecision(6)
                  << std::setw(12) << mae
                  << std::setw(15) << max_error;

        if (mae < best_mae) {
            best_mae = mae;
            best_idx = v;
        }

        if (mae < 0.01) {
            std::cout << "  ✓✓✓ PERFECT!";
        } else if (mae < 0.1) {
            std::cout << "  ✓✓ EXCELLENT";
        } else if (mae < 1.0) {
            std::cout << "  ✓ GOOD";
        }

        std::cout << "\n";
    }

    std::cout << "\nBest: " << variants[best_idx].name
              << " (MAE: " << best_mae << ")\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "COMPREHENSIVE CMMA COMPRESSION VARIANTS TEST\n";
    std::cout << "Testing: normal_cdf vs tanh, different pre/post scaling\n";
    std::cout << "=============================================\n";

    test_indicator(ohlcv_bars, tssb_bars, "CMMA_S", 10, 250);
    test_indicator(ohlcv_bars, tssb_bars, "CMMA_M", 50, 250);
    test_indicator(ohlcv_bars, tssb_bars, "CMMA_L", 120, 250);

    return 0;
}

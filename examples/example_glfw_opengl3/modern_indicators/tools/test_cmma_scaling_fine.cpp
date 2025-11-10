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

void compute_cmma_scaled(const std::vector<OHLCVBar>& bars, int lookback, int atr_length,
                        std::vector<double>& output, double post_scale) {
    int n = bars.size();
    output.assign(n, 0.0);
    int front_bad = std::max(lookback, atr_length);

    for (int icase = front_bad; icase < n; ++icase) {
        double sum = 0.0;
        for (int k = icase - lookback; k < icase; ++k) {
            sum += std::log(bars[k].close);
        }
        sum /= lookback;

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
            double compressed = tssb::normal_cdf(raw_val);
            output[icase] = post_scale * compressed - (post_scale / 2.0);
        } else {
            output[icase] = 0.0;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);

    std::cout << "FINE-GRAINED POST-SCALE SEARCH FOR CMMA\n";
    std::cout << "========================================\n\n";

    struct Test { std::string name; int lookback; int atr; };
    std::vector<Test> tests = {
        {"CMMA_S", 10, 250},
        {"CMMA_M", 50, 250},
        {"CMMA_L", 120, 250}
    };

    for (const auto& test : tests) {
        std::cout << "\n" << test.name << " (lookback=" << test.lookback << ")\n";
        std::cout << std::string(50, '-') << "\n";

        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.name);

        size_t first_valid = 0;
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            if (std::isfinite(expected[i])) {
                first_valid = i;
                break;
            }
        }

        // Test post_scale from 10 to 200 in steps of 1
        double best_mae = 1e10;
        double best_scale = 0;

        for (double scale = 10.0; scale <= 200.0; scale += 1.0) {
            std::vector<double> output;
            compute_cmma_scaled(ohlcv_bars, test.lookback, test.atr, output, scale);

            double mae = 0.0;
            int count = 0;
            for (size_t i = first_valid; i < ohlcv_bars.size(); ++i) {
                if (std::isfinite(expected[i])) {
                    mae += std::abs(output[i] - expected[i]);
                    count++;
                }
            }
            mae /= count;

            if (mae < best_mae) {
                best_mae = mae;
                best_scale = scale;
            }
        }

        std::cout << "Best post_scale: " << std::fixed << std::setprecision(1) << best_scale
                  << "  →  Range: [" << -best_scale/2 << ", " << best_scale/2 << "]\n";
        std::cout << "MAE: " << std::setprecision(6) << best_mae;

        if (best_mae < 0.01) std::cout << "  ✓✓✓ PERFECT!";
        else if (best_mae < 0.1) std::cout << "  ✓✓ EXCELLENT";
        else if (best_mae < 1.0) std::cout << "  ✓ GOOD";

        std::cout << "\n\nFormula: " << best_scale << " * normal_cdf(x) - " << best_scale/2 << "\n";
    }

    return 0;
}

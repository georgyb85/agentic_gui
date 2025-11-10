#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    auto tssb_bars = TSBBOutputParser::parse_file(argv[2]);
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Test VOL_MOM_S: VOLUME MOMENTUM 10 5
    auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, "VOL_MOM_S");

    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::VolumeMomentum;
    req.name = "VOL_MOM_S";
    req.params[0] = 10;  // short_length
    req.params[1] = 5;   // mult

    auto result = compute_single_indicator(series, req);

    // Find first valid bar
    size_t first_valid = 0;
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        if (std::isfinite(expected[i])) {
            first_valid = i;
            break;
        }
    }

    std::cout << "First valid bar: " << first_valid << "\n";
    std::cout << "Date/Time: " << ohlcv_bars[first_valid].date << " "
              << ohlcv_bars[first_valid].time << "\n\n";

    // Diagnose first few bars in detail
    std::cout << "DETAILED DIAGNOSIS OF FIRST 5 BARS:\n";
    std::cout << std::string(80, '=') << "\n";

    for (int j = 0; j < 5; ++j) {
        size_t i = first_valid + j;

        std::cout << "\nBar " << i << " (" << ohlcv_bars[i].date << " " << ohlcv_bars[i].time << "):\n";

        // Manually compute to debug
        int short_length = 10;
        int mult = 5;
        int long_length = short_length * mult;  // 50

        // Compute short sum
        double short_sum = 0.0;
        std::cout << "  Short-term volumes (last " << short_length << " bars):\n";
        for (int k = 0; k < short_length; ++k) {
            size_t idx = i - k;
            double vol = series.volume[idx];
            short_sum += vol;
            if (k < 3) {
                std::cout << "    [" << idx << "] = " << vol << "\n";
            }
        }
        std::cout << "    ...\n";
        double short_mean = short_sum / short_length;
        std::cout << "  Short sum: " << short_sum << "\n";
        std::cout << "  Short mean: " << short_mean << "\n";

        // Compute long sum
        double long_sum = short_sum;
        std::cout << "  Long-term additional volumes (bars " << (i - long_length + 1)
                  << " to " << (i - short_length) << "):\n";
        for (int k = i - long_length + 1; k < i - short_length + 1; ++k) {
            double vol = series.volume[k];
            long_sum += vol;
            if (k < i - long_length + 4) {
                std::cout << "    [" << k << "] = " << vol << "\n";
            }
        }
        std::cout << "    ...\n";
        double long_mean = long_sum / long_length;
        std::cout << "  Long sum: " << long_sum << "\n";
        std::cout << "  Long mean: " << long_mean << "\n";

        // Compute denominator
        double denom = std::exp(std::log(static_cast<double>(mult)) / 3.0);
        std::cout << "  Multiplier: " << mult << "\n";
        std::cout << "  Denom (cube root of mult): " << denom << "\n";

        // Compute raw value
        double ratio = short_mean / long_mean;
        double log_ratio = std::log(ratio);
        double raw = log_ratio / denom;
        std::cout << "  Ratio (short/long): " << ratio << "\n";
        std::cout << "  Log(ratio): " << log_ratio << "\n";
        std::cout << "  Raw (log_ratio / denom): " << raw << "\n";

        // Apply transformation
        double scaled = 3.0 * raw;
        std::cout << "  Scaled (3.0 * raw): " << scaled << "\n";

        // The normal_cdf is not directly available, so let's just show what we computed
        std::cout << "  Computed value: " << result.values[i] << "\n";
        std::cout << "  Expected value: " << expected[i] << "\n";
        std::cout << "  Error: " << (result.values[i] - expected[i]) << "\n";
    }

    return 0;
}

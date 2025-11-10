#include "IndicatorEngine.hpp"
#include "IndicatorId.hpp"
#include "IndicatorRequest.hpp"
#include "Series.hpp"
#include "validation/DataParsers.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <limits>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc245.txt> <BTC245 HM.CSV>\n";
        std::cerr << "\nValidates wavelet indicator implementations against TSSB baseline.\n";
        return 1;
    }

    const std::string ohlcv_path = argv[1];
    const std::string tssb_path = argv[2];

    std::cout << "================================================================================\n";
    std::cout << "WAVELET INDICATOR VALIDATION\n";
    std::cout << "================================================================================\n\n";

    // ========================================================================
    // Step 1: Parse OHLCV Data
    // ========================================================================
    std::cout << "Step 1: Loading OHLCV data from " << ohlcv_path << "...\n";

    auto ohlcv_bars = OHLCVParser::parse_file(ohlcv_path);
    if (ohlcv_bars.empty()) {
        std::cerr << "ERROR: " << OHLCVParser::get_last_error() << "\n";
        return 1;
    }

    std::cout << "  Loaded " << ohlcv_bars.size() << " OHLCV bars\n";
    std::cout << "  Date range: " << ohlcv_bars.front().date << " " << ohlcv_bars.front().time
              << " to " << ohlcv_bars.back().date << " " << ohlcv_bars.back().time << "\n\n";

    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // ========================================================================
    // Step 2: Parse TSSB Output
    // ========================================================================
    std::cout << "Step 2: Loading TSSB indicator output from " << tssb_path << "...\n";

    auto tssb_bars = TSBBOutputParser::parse_file(tssb_path);
    if (tssb_bars.empty()) {
        std::cerr << "ERROR: " << TSBBOutputParser::get_last_error() << "\n";
        return 1;
    }

    std::cout << "  Loaded " << tssb_bars.size() << " indicator bars\n";

    auto headers = TSBBOutputParser::get_headers();
    std::cout << "  Found " << (headers.size() - 3) << " indicators\n";
    std::cout << "  Indicators: ";
    for (size_t i = 3; i < std::min(headers.size(), size_t(8)); ++i) {
        std::cout << headers[i] << " ";
    }
    std::cout << "...\n\n";

    // ========================================================================
    // Step 3: Define Wavelet Indicators to Test
    // ========================================================================
    std::cout << "Step 3: Defining wavelet indicators to test...\n\n";

    struct WaveletTest {
        SingleIndicatorId id;
        std::string name;
        std::vector<double> params;
        std::string csv_column;
    };

    std::vector<WaveletTest> wavelet_tests = {
        // Morlet wavelets
        {SingleIndicatorId::RealMorlet, "REAL MORLET 10", {10.0}, "REAL_MORLET_10"},
        {SingleIndicatorId::RealMorlet, "REAL MORLET 20", {20.0}, "REAL_MORLET_20"},
        {SingleIndicatorId::ImagMorlet, "IMAG MORLET 10", {10.0}, "IMAG_MORLET_10"},
        {SingleIndicatorId::ImagMorlet, "IMAG MORLET 20", {20.0}, "IMAG_MORLET_20"},
        {SingleIndicatorId::RealDiffMorlet, "REAL DIFF MORLET 10", {10.0}, "R_DIFF_MORLET"},
        {SingleIndicatorId::RealProductMorlet, "REAL PRODUCT MORLET 10", {10.0}, "R_PROD_MORLET"},
        // Daubechies wavelets
        {SingleIndicatorId::DaubMean, "DAUB MEAN 32 2", {32.0, 2.0}, "DAUB_MEAN_32_2"},
        {SingleIndicatorId::DaubMin, "DAUB MIN 32 2", {32.0, 2.0}, "DAUB_MIN_32"},
        {SingleIndicatorId::DaubMax, "DAUB MAX 32 2", {32.0, 2.0}, "DAUB_MAX_32"},
        {SingleIndicatorId::DaubStd, "DAUB STD 32 2", {32.0, 2.0}, "DAUB_STD_32"},
        {SingleIndicatorId::DaubEnergy, "DAUB ENERGY 32 2", {32.0, 2.0}, "DAUB_ENERGY_32"},
        {SingleIndicatorId::DaubNlEnergy, "DAUB NL ENERGY 32 2", {32.0, 2.0}, "DAUB_NL_ENERGY"},
        {SingleIndicatorId::DaubCurve, "DAUB CURVE 32 2", {32.0, 2.0}, "DAUB_CURVE"},
    };

    // ========================================================================
    // Step 4: Compute Indicators
    // ========================================================================
    std::cout << "Step 4: Computing wavelet indicators...\n";

    IndicatorEngine engine;
    std::vector<ComparisonStats> all_stats;

    for (const auto& test : wavelet_tests) {
        std::cout << "  Computing: " << test.name << "...\n";

        // Create request
        SingleIndicatorRequest request;
        request.id = test.id;
        request.name = test.csv_column;
        for (size_t i = 0; i < test.params.size() && i < 4; ++i) {
            request.params[i] = test.params[i];
        }

        // Compute indicator
        auto results = engine.compute(series, {request}, {.parallel = false});

        if (results.empty() || !results[0].success) {
            std::cerr << "    ERROR: Failed to compute " << test.name;
            if (!results.empty()) {
                std::cerr << " - " << results[0].error_message;
            }
            std::cerr << "\n";
            continue;
        }

        const auto& result = results[0];

        // Extract expected values from TSSB output (aligned by date/time)
        auto expected = TSBBOutputParser::extract_indicator_aligned(tssb_bars, ohlcv_bars, test.csv_column);

        if (expected.size() != result.values.size()) {
            std::cerr << "    WARNING: Size mismatch - computed=" << result.values.size()
                      << ", expected=" << expected.size() << "\n";
        }

        // Compare
        IndicatorValidator validator(
            0.01,   // max_abs_error = 0.01
            1.0,    // max_mape = 1%
            0.99    // min_correlation = 0.99
        );

        auto stats = validator.compare(result.values, expected, test.name);
        all_stats.push_back(stats);

        std::cout << "    " << (stats.passed ? "PASS ✓" : "FAIL ✗") << "\n";
    }

    std::cout << "\n";

    // ========================================================================
    // Step 5: Generate Report
    // ========================================================================
    std::cout << "Step 5: Generating validation report...\n\n";

    std::string report = IndicatorValidator::generate_report(all_stats);
    std::cout << report;

    // Save report to file
    std::string report_path = "WAVELET_VALIDATION_REPORT.txt";
    std::ofstream report_file(report_path);
    if (report_file.is_open()) {
        report_file << report;
        report_file.close();
        std::cout << "Report saved to: " << report_path << "\n\n";
    }

    // ========================================================================
    // Step 6: Export Computed Values for Analysis
    // ========================================================================
    std::cout << "Step 6: Exporting computed values for detailed analysis...\n";

    // Export RAW values (before compression) for debugging
    std::string raw_export_path = "COMPUTED_WAVELETS_RAW.csv";
    std::ofstream raw_export_file(raw_export_path);

    std::string export_path = "COMPUTED_WAVELETS.csv";
    std::ofstream export_file(export_path);
    if (export_file.is_open()) {
        // Write header
        export_file << "Date,Time";
        for (const auto& test : wavelet_tests) {
            export_file << "," << test.csv_column;
        }
        export_file << "\n";

        // Compute all indicators again for export
        std::vector<std::vector<double>> computed_values;
        for (const auto& test : wavelet_tests) {
            SingleIndicatorRequest request;
            request.id = test.id;
            request.name = test.csv_column;
            for (size_t i = 0; i < test.params.size() && i < 4; ++i) {
                request.params[i] = test.params[i];
            }

            auto results = engine.compute(series, {request}, {.parallel = false});
            if (!results.empty() && results[0].success) {
                computed_values.push_back(results[0].values);
            } else {
                computed_values.push_back(std::vector<double>(series.close.size(),
                    std::numeric_limits<double>::quiet_NaN()));
            }
        }

        // Write data
        for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
            export_file << ohlcv_bars[i].date << "," << ohlcv_bars[i].time;
            for (const auto& values : computed_values) {
                if (i < values.size()) {
                    export_file << "," << std::fixed << std::setprecision(8) << values[i];
                } else {
                    export_file << ",";
                }
            }
            export_file << "\n";
        }

        export_file.close();
        std::cout << "Computed values exported to: " << export_path << "\n\n";
    }

    // ========================================================================
    // Summary
    // ========================================================================
    std::cout << "================================================================================\n";
    std::cout << "VALIDATION COMPLETE\n";
    std::cout << "================================================================================\n";

    size_t passed = 0;
    size_t failed = 0;
    for (const auto& stats : all_stats) {
        if (stats.passed) ++passed;
        else ++failed;
    }

    std::cout << "Total Indicators Tested: " << all_stats.size() << "\n";
    std::cout << "Passed: " << passed << " (" << (100.0 * passed / all_stats.size()) << "%)\n";
    std::cout << "Failed: " << failed << " (" << (100.0 * failed / all_stats.size()) << "%)\n";

    return (failed == 0) ? 0 : 1;
}

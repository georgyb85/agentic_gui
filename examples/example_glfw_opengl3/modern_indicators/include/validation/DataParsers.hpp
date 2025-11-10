#pragma once

#include "Series.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tssb {
namespace validation {

/**
 * @brief Structure to hold a single OHLCV bar with timestamp
 */
struct OHLCVBar {
    std::string date;      // YYYYMMDD format
    std::string time;      // HHMM format
    double open;
    double high;
    double low;
    double close;
    double volume;
};

/**
 * @brief Parser for TSSB-style OHLCV data files
 *
 * Format: Date Time Open High Low Close Volume
 * Example: 20241001 0000 63327.60 63606.00 63006.70 63531.99 1336.93
 */
class OHLCVParser {
public:
    /**
     * @brief Parse OHLCV data from file
     * @param filepath Path to OHLCV file (e.g., btc245.txt)
     * @return Vector of OHLCV bars, or empty on error
     */
    static std::vector<OHLCVBar> parse_file(const std::string& filepath);

    /**
     * @brief Convert vector of OHLCVBar to SingleMarketSeries
     * @param bars Vector of OHLCV bars
     * @return SingleMarketSeries for indicator computation
     */
    static SingleMarketSeries to_series(const std::vector<OHLCVBar>& bars);

    /**
     * @brief Get last parse error message
     */
    static std::string get_last_error();

private:
    static std::string last_error_;
};

/**
 * @brief Structure to hold TSSB indicator output for one bar
 */
struct TSBBIndicatorBar {
    std::string date;      // YYYYMMDD
    std::string time;      // HHMM
    std::string market;    // Market name (e.g., "BTC245")
    std::map<std::string, double> indicators;  // Indicator name -> value
};

/**
 * @brief Parser for TSSB indicator output CSV files
 *
 * Format: Date Time Market Indicator1 Indicator2 ... IndicatorN
 * First line contains column headers
 */
class TSBBOutputParser {
public:
    /**
     * @brief Parse TSSB output CSV file
     * @param filepath Path to CSV file (e.g., "BTC245 HM.CSV")
     * @return Vector of indicator bars, or empty on error
     */
    static std::vector<TSBBIndicatorBar> parse_file(const std::string& filepath);

    /**
     * @brief Get column headers from parsed file
     * @return Vector of indicator names in order
     */
    static std::vector<std::string> get_headers();

    /**
     * @brief Extract single indicator values across all bars
     * @param bars Vector of indicator bars
     * @param indicator_name Name of indicator to extract
     * @return Vector of values (NaN for missing)
     */
    static std::vector<double> extract_indicator(
        const std::vector<TSBBIndicatorBar>& bars,
        const std::string& indicator_name);

    /**
     * @brief Extract indicator values aligned by date/time with OHLCV data
     * @param bars Vector of indicator bars from TSSB
     * @param ohlcv_bars Vector of OHLCV bars to align with
     * @param indicator_name Name of indicator to extract
     * @return Vector of values aligned with ohlcv_bars (NaN for missing/misaligned)
     */
    static std::vector<double> extract_indicator_aligned(
        const std::vector<TSBBIndicatorBar>& bars,
        const std::vector<OHLCVBar>& ohlcv_bars,
        const std::string& indicator_name);

    /**
     * @brief Get last parse error message
     */
    static std::string get_last_error();

private:
    static std::string last_error_;
    static std::vector<std::string> headers_;
};

/**
 * @brief Statistical comparison results
 */
struct ComparisonStats {
    std::string indicator_name;
    size_t total_bars;
    size_t valid_bars;           // Both computed and expected are valid
    size_t missing_computed;     // Computed is NaN/missing
    size_t missing_expected;     // Expected is NaN/missing

    double mean_error;           // Mean of (computed - expected)
    double mean_abs_error;       // Mean of |computed - expected|
    double max_abs_error;        // Maximum |computed - expected|
    double rms_error;            // Root mean squared error
    double correlation;          // Pearson correlation coefficient
    double mean_abs_pct_error;   // Mean absolute percentage error (MAPE)

    bool passed;                 // True if within tolerance
    std::string status_message;
};

/**
 * @brief Validator for comparing computed vs expected indicator values
 */
class IndicatorValidator {
public:
    /**
     * @brief Set tolerance thresholds
     * @param max_abs_error Maximum absolute error allowed
     * @param max_mape Maximum mean absolute percentage error allowed (0-100)
     * @param min_correlation Minimum correlation required
     */
    IndicatorValidator(
        double max_abs_error = 0.01,
        double max_mape = 1.0,
        double min_correlation = 0.99
    );

    /**
     * @brief Compare computed vs expected values
     * @param computed Vector of computed values
     * @param expected Vector of expected values
     * @param indicator_name Name for reporting
     * @return Comparison statistics
     */
    ComparisonStats compare(
        const std::vector<double>& computed,
        const std::vector<double>& expected,
        const std::string& indicator_name
    ) const;

    /**
     * @brief Check if value is valid (not NaN or infinite)
     */
    static bool is_valid(double value);

    /**
     * @brief Generate detailed comparison report
     * @param stats Vector of comparison statistics
     * @return Formatted report string
     */
    static std::string generate_report(const std::vector<ComparisonStats>& stats);

private:
    double max_abs_error_;
    double max_mape_;
    double min_correlation_;

    double compute_correlation(
        const std::vector<double>& x,
        const std::vector<double>& y
    ) const;
};

} // namespace validation
} // namespace tssb

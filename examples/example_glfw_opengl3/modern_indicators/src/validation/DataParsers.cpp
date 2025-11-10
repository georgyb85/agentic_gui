#include "validation/DataParsers.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <limits>
#include <map>

namespace tssb {
namespace validation {

// ============================================================================
// OHLCVParser Implementation
// ============================================================================

std::string OHLCVParser::last_error_;

std::vector<OHLCVBar> OHLCVParser::parse_file(const std::string& filepath)
{
    last_error_.clear();
    std::vector<OHLCVBar> bars;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        last_error_ = "Failed to open file: " + filepath;
        return bars;
    }

    std::string line;
    size_t line_num = 0;

    while (std::getline(file, line)) {
        ++line_num;

        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        std::istringstream iss(line);
        OHLCVBar bar;

        // Parse: Date Time Open High Low Close Volume
        if (!(iss >> bar.date >> bar.time >> bar.open >> bar.high >> bar.low >> bar.close >> bar.volume)) {
            last_error_ = "Parse error at line " + std::to_string(line_num) + ": " + line;
            return std::vector<OHLCVBar>();  // Return empty on error
        }

        bars.push_back(bar);
    }

    if (bars.empty()) {
        last_error_ = "No data parsed from file";
    }

    return bars;
}

SingleMarketSeries OHLCVParser::to_series(const std::vector<OHLCVBar>& bars)
{
    SingleMarketSeries series;
    series.open.reserve(bars.size());
    series.high.reserve(bars.size());
    series.low.reserve(bars.size());
    series.close.reserve(bars.size());
    series.volume.reserve(bars.size());

    for (const auto& bar : bars) {
        series.open.push_back(bar.open);
        series.high.push_back(bar.high);
        series.low.push_back(bar.low);
        series.close.push_back(bar.close);
        series.volume.push_back(bar.volume);
    }

    return series;
}

std::string OHLCVParser::get_last_error()
{
    return last_error_;
}

// ============================================================================
// TSBBOutputParser Implementation
// ============================================================================

std::string TSBBOutputParser::last_error_;
std::vector<std::string> TSBBOutputParser::headers_;

std::vector<TSBBIndicatorBar> TSBBOutputParser::parse_file(const std::string& filepath)
{
    last_error_.clear();
    headers_.clear();
    std::vector<TSBBIndicatorBar> bars;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        last_error_ = "Failed to open file: " + filepath;
        return bars;
    }

    std::string line;
    size_t line_num = 0;
    bool header_parsed = false;

    while (std::getline(file, line)) {
        ++line_num;

        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        if (!header_parsed) {
            // Parse header line
            std::istringstream iss(line);
            std::string token;

            while (iss >> token) {
                headers_.push_back(token);
            }

            if (headers_.size() < 3) {
                last_error_ = "Invalid header: need at least Date, Time, Market";
                return std::vector<TSBBIndicatorBar>();
            }

            header_parsed = true;
            continue;
        }

        // Parse data line
        std::istringstream iss(line);
        TSBBIndicatorBar bar;

        // Parse Date, Time, Market
        if (!(iss >> bar.date >> bar.time >> bar.market)) {
            last_error_ = "Parse error at line " + std::to_string(line_num);
            return std::vector<TSBBIndicatorBar>();
        }

        // Parse indicator values
        for (size_t i = 3; i < headers_.size(); ++i) {
            double value;
            if (iss >> value) {
                bar.indicators[headers_[i]] = value;
            } else {
                // Missing or invalid value - treat as NaN
                bar.indicators[headers_[i]] = std::numeric_limits<double>::quiet_NaN();
            }
        }

        bars.push_back(bar);
    }

    if (bars.empty()) {
        last_error_ = "No data parsed from file";
    }

    return bars;
}

std::vector<std::string> TSBBOutputParser::get_headers()
{
    return headers_;
}

std::vector<double> TSBBOutputParser::extract_indicator(
    const std::vector<TSBBIndicatorBar>& bars,
    const std::string& indicator_name)
{
    std::vector<double> values;
    values.reserve(bars.size());

    for (const auto& bar : bars) {
        auto it = bar.indicators.find(indicator_name);
        if (it != bar.indicators.end()) {
            values.push_back(it->second);
        } else {
            values.push_back(std::numeric_limits<double>::quiet_NaN());
        }
    }

    return values;
}

std::vector<double> TSBBOutputParser::extract_indicator_aligned(
    const std::vector<TSBBIndicatorBar>& bars,
    const std::vector<OHLCVBar>& ohlcv_bars,
    const std::string& indicator_name)
{
    // Create a map of date/time -> indicator value for fast lookup
    std::map<std::pair<std::string, std::string>, double> indicator_map;

    for (const auto& bar : bars) {
        auto it = bar.indicators.find(indicator_name);
        double value = (it != bar.indicators.end()) ? it->second : std::numeric_limits<double>::quiet_NaN();
        indicator_map[{bar.date, bar.time}] = value;
    }

    // Align with OHLCV bars
    std::vector<double> values;
    values.reserve(ohlcv_bars.size());

    for (const auto& ohlcv : ohlcv_bars) {
        auto it = indicator_map.find({ohlcv.date, ohlcv.time});
        if (it != indicator_map.end()) {
            values.push_back(it->second);
        } else {
            values.push_back(std::numeric_limits<double>::quiet_NaN());
        }
    }

    return values;
}

std::string TSBBOutputParser::get_last_error()
{
    return last_error_;
}

// ============================================================================
// IndicatorValidator Implementation
// ============================================================================

IndicatorValidator::IndicatorValidator(
    double max_abs_error,
    double max_mape,
    double min_correlation)
    : max_abs_error_(max_abs_error)
    , max_mape_(max_mape)
    , min_correlation_(min_correlation)
{
}

bool IndicatorValidator::is_valid(double value)
{
    return std::isfinite(value);
}

ComparisonStats IndicatorValidator::compare(
    const std::vector<double>& computed,
    const std::vector<double>& expected,
    const std::string& indicator_name) const
{
    ComparisonStats stats;
    stats.indicator_name = indicator_name;
    stats.total_bars = std::min(computed.size(), expected.size());
    stats.valid_bars = 0;
    stats.missing_computed = 0;
    stats.missing_expected = 0;

    // Initialize accumulators
    double sum_error = 0.0;
    double sum_abs_error = 0.0;
    double sum_sq_error = 0.0;
    double sum_abs_pct_error = 0.0;
    stats.max_abs_error = 0.0;

    std::vector<double> valid_computed;
    std::vector<double> valid_expected;

    // Compute statistics
    for (size_t i = 0; i < stats.total_bars; ++i) {
        bool comp_valid = is_valid(computed[i]);
        bool exp_valid = is_valid(expected[i]);

        if (!comp_valid) {
            ++stats.missing_computed;
        }
        if (!exp_valid) {
            ++stats.missing_expected;
        }

        if (comp_valid && exp_valid) {
            ++stats.valid_bars;

            double error = computed[i] - expected[i];
            double abs_error = std::fabs(error);

            sum_error += error;
            sum_abs_error += abs_error;
            sum_sq_error += error * error;

            if (abs_error > stats.max_abs_error) {
                stats.max_abs_error = abs_error;
            }

            // Percentage error (avoid division by very small numbers)
            if (std::fabs(expected[i]) > 1e-10) {
                double pct_error = 100.0 * abs_error / std::fabs(expected[i]);
                sum_abs_pct_error += pct_error;
            }

            valid_computed.push_back(computed[i]);
            valid_expected.push_back(expected[i]);
        }
    }

    // Compute final statistics
    if (stats.valid_bars > 0) {
        stats.mean_error = sum_error / stats.valid_bars;
        stats.mean_abs_error = sum_abs_error / stats.valid_bars;
        stats.rms_error = std::sqrt(sum_sq_error / stats.valid_bars);
        stats.mean_abs_pct_error = sum_abs_pct_error / stats.valid_bars;
    } else {
        stats.mean_error = 0.0;
        stats.mean_abs_error = 0.0;
        stats.rms_error = 0.0;
        stats.mean_abs_pct_error = 0.0;
    }

    // Compute correlation
    if (valid_computed.size() >= 2) {
        stats.correlation = compute_correlation(valid_computed, valid_expected);
    } else {
        stats.correlation = 0.0;
    }

    // Determine pass/fail
    stats.passed = true;
    std::ostringstream status;

    if (stats.valid_bars == 0) {
        stats.passed = false;
        status << "NO VALID DATA";
    } else {
        bool max_err_ok = stats.max_abs_error <= max_abs_error_;
        bool mape_ok = stats.mean_abs_pct_error <= max_mape_;
        bool corr_ok = stats.correlation >= min_correlation_;

        if (!max_err_ok) {
            stats.passed = false;
            status << "MAX_ERROR_EXCEEDED(" << stats.max_abs_error << " > " << max_abs_error_ << ") ";
        }
        if (!mape_ok) {
            stats.passed = false;
            status << "MAPE_EXCEEDED(" << stats.mean_abs_pct_error << "% > " << max_mape_ << "%) ";
        }
        if (!corr_ok) {
            stats.passed = false;
            status << "CORRELATION_LOW(" << stats.correlation << " < " << min_correlation_ << ") ";
        }

        if (stats.passed) {
            status << "PASS";
        }
    }

    stats.status_message = status.str();
    return stats;
}

double IndicatorValidator::compute_correlation(
    const std::vector<double>& x,
    const std::vector<double>& y) const
{
    if (x.size() != y.size() || x.size() < 2) {
        return 0.0;
    }

    size_t n = x.size();

    // Compute means
    double mean_x = 0.0;
    double mean_y = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= n;
    mean_y /= n;

    // Compute correlation
    double sum_xy = 0.0;
    double sum_xx = 0.0;
    double sum_yy = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double dx = x[i] - mean_x;
        double dy = y[i] - mean_y;
        sum_xy += dx * dy;
        sum_xx += dx * dx;
        sum_yy += dy * dy;
    }

    double denom = std::sqrt(sum_xx * sum_yy);
    if (denom < 1e-10) {
        return 0.0;
    }

    return sum_xy / denom;
}

std::string IndicatorValidator::generate_report(const std::vector<ComparisonStats>& stats)
{
    std::ostringstream report;

    report << "================================================================================\n";
    report << "INDICATOR VALIDATION REPORT\n";
    report << "================================================================================\n\n";

    size_t total_indicators = stats.size();
    size_t passed = 0;
    size_t failed = 0;

    for (const auto& s : stats) {
        if (s.passed) {
            ++passed;
        } else {
            ++failed;
        }
    }

    report << "Summary:\n";
    report << "  Total Indicators: " << total_indicators << "\n";
    report << "  Passed: " << passed << " (" << (100.0 * passed / total_indicators) << "%)\n";
    report << "  Failed: " << failed << " (" << (100.0 * failed / total_indicators) << "%)\n";
    report << "\n";

    report << "--------------------------------------------------------------------------------\n";
    report << "Detailed Results:\n";
    report << "--------------------------------------------------------------------------------\n\n";

    for (const auto& s : stats) {
        report << "Indicator: " << s.indicator_name << "\n";
        report << "  Status: " << (s.passed ? "PASS ✓" : "FAIL ✗") << " - " << s.status_message << "\n";
        report << "  Total Bars: " << s.total_bars << "\n";
        report << "  Valid Bars: " << s.valid_bars;
        if (s.valid_bars < s.total_bars) {
            report << " (Missing: computed=" << s.missing_computed
                   << ", expected=" << s.missing_expected << ")";
        }
        report << "\n";

        if (s.valid_bars > 0) {
            report << "  Mean Error: " << s.mean_error << "\n";
            report << "  Mean Abs Error: " << s.mean_abs_error << "\n";
            report << "  Max Abs Error: " << s.max_abs_error << "\n";
            report << "  RMS Error: " << s.rms_error << "\n";
            report << "  MAPE: " << s.mean_abs_pct_error << "%\n";
            report << "  Correlation: " << s.correlation << "\n";
        }
        report << "\n";
    }

    report << "================================================================================\n";

    return report.str();
}

} // namespace validation
} // namespace tssb

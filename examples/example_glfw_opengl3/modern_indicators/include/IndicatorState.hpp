#pragma once

#include "Series.hpp"
#include <deque>
#include <map>
#include <string>

namespace tssb {

/// Rolling window state for incremental indicator computation
class IndicatorState {
public:
    /// Construct with maximum lookback needed
    explicit IndicatorState(int max_lookback = 500);

    /// Append a new OHLCV bar
    void append_bar(double open, double high, double low, double close, double volume);

    /// Get current number of bars in state
    std::size_t size() const { return close_.size(); }

    /// Get maximum lookback capacity
    int max_lookback() const { return max_lookback_; }

    /// Check if state has enough data for computation
    bool has_enough_data(int required_lookback) const {
        return static_cast<int>(close_.size()) >= required_lookback;
    }

    /// Get series spans for indicator computation (read-only view)
    SeriesSpans to_series_spans() const;

    /// Store/retrieve last computed indicator value (for stateful indicators)
    void set_last_value(const std::string& indicator_name, double value);
    double get_last_value(const std::string& indicator_name, double default_value = 0.0) const;

    /// Clear all data
    void clear();

private:
    std::deque<double> open_;
    std::deque<double> high_;
    std::deque<double> low_;
    std::deque<double> close_;
    std::deque<double> volume_;

    std::map<std::string, double> last_values_;

    int max_lookback_;

    void trim_if_needed();
};

/// Thread-safe wrapper for IndicatorState
class ThreadSafeIndicatorState {
public:
    explicit ThreadSafeIndicatorState(int max_lookback = 500);

    void append_bar(double open, double high, double low, double close, double volume);

    std::size_t size() const;

    SeriesSpans to_series_spans() const;

    void set_last_value(const std::string& indicator_name, double value);
    double get_last_value(const std::string& indicator_name, double default_value = 0.0) const;

private:
    mutable std::mutex mutex_;
    IndicatorState state_;
};

} // namespace tssb

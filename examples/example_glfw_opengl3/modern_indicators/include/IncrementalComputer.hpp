#pragma once

#include "IndicatorState.hpp"
#include "IndicatorConfig.hpp"
#include "IndicatorRequest.hpp"
#include <vector>
#include <map>
#include <string>

namespace tssb {

/// Compute indicators incrementally for new bars only
class IncrementalIndicatorComputer {
public:
    /// Construct from config file
    explicit IncrementalIndicatorComputer(const std::string& config_file);

    /// Construct from indicator definitions
    explicit IncrementalIndicatorComputer(const std::vector<IndicatorDefinition>& definitions);

    /// Compute all indicators for the latest bar in state
    /// Returns: map of indicator_name → value
    std::map<std::string, double> compute_latest(IndicatorState& state) const;

    /// Get maximum lookback needed across all indicators
    int get_max_lookback() const { return max_lookback_; }

    /// Get indicator definitions
    const std::vector<IndicatorDefinition>& get_definitions() const {
        return definitions_;
    }

    /// Get number of indicators
    std::size_t indicator_count() const { return definitions_.size(); }

private:
    std::vector<IndicatorDefinition> definitions_;
    std::vector<SingleIndicatorRequest> requests_;
    int max_lookback_;

    void initialize();
    int compute_max_lookback() const;
};

/// Simple example usage pattern
///
/// ```cpp
/// // Setup
/// IndicatorState state(500);
/// IncrementalIndicatorComputer computer("var.txt");
///
/// // Warm up with historical data
/// for (const auto& bar : historical_bars) {
///     state.append_bar(bar.open, bar.high, bar.low, bar.close, bar.volume);
/// }
///
/// // Real-time update (called on each new bar)
/// state.append_bar(new_bar.open, new_bar.high, new_bar.low, new_bar.close, new_bar.volume);
/// auto indicators = computer.compute_latest(state);
///
/// // Use indicators
/// for (const auto& [name, value] : indicators) {
///     dataframe.append(name, value);
/// }
/// ```

} // namespace tssb

/// Example: Incremental indicator computation for real-time systems
///
/// Scenario:
/// - Receive 1-minute OHLCV bars continuously
/// - Every hour, aggregate to 1-hour bar
/// - Compute indicators for the new 1-hour bar
/// - Append to in-memory dataframe

#include "IncrementalComputer.hpp"
#include "IndicatorState.hpp"
#include <iostream>
#include <vector>
#include <chrono>

using namespace tssb;

// Simulated 1-minute bar
struct MinuteBar {
    double open, high, low, close, volume;
    std::string timestamp;
};

// Simulated 1-hour aggregated bar
struct HourBar {
    double open, high, low, close, volume;
    std::string timestamp;

    static HourBar aggregate(const std::vector<MinuteBar>& minute_bars) {
        if (minute_bars.empty()) {
            return {0, 0, 0, 0, 0, ""};
        }

        HourBar result;
        result.open = minute_bars.front().open;
        result.close = minute_bars.back().close;
        result.high = minute_bars[0].high;
        result.low = minute_bars[0].low;
        result.volume = 0;

        for (const auto& bar : minute_bars) {
            result.high = std::max(result.high, bar.high);
            result.low = std::min(result.low, bar.low);
            result.volume += bar.volume;
        }

        result.timestamp = minute_bars.back().timestamp;
        return result;
    }
};

// Simulated in-memory dataframe (like chronosflow)
class IndicatorDataFrame {
    std::vector<std::string> timestamps_;
    std::map<std::string, std::vector<double>> columns_;

public:
    void append_row(const std::string& timestamp,
                   const std::map<std::string, double>& indicators) {
        timestamps_.push_back(timestamp);

        for (const auto& [name, value] : indicators) {
            columns_[name].push_back(value);
        }
    }

    void print_last(int n = 5) const {
        std::cout << "\nLast " << n << " rows of indicator dataframe:\n";
        std::cout << "Timestamp";

        // Print column headers
        for (const auto& [name, _] : columns_) {
            std::cout << "\t" << name;
        }
        std::cout << "\n";

        // Print last n rows
        int start = std::max(0, static_cast<int>(timestamps_.size()) - n);
        for (int i = start; i < static_cast<int>(timestamps_.size()); ++i) {
            std::cout << timestamps_[i];
            for (const auto& [name, values] : columns_) {
                std::cout << "\t" << values[i];
            }
            std::cout << "\n";
        }
    }
};

int main() {
    std::cout << "=== Incremental Indicator Computation Example ===\n\n";

    // 1. Initialize incremental computer from config
    std::cout << "Loading indicator config...\n";
    IncrementalIndicatorComputer computer("example_config.txt");
    std::cout << "Loaded " << computer.indicator_count() << " indicators\n";
    std::cout << "Max lookback needed: " << computer.get_max_lookback() << " bars\n\n";

    // 2. Create state with sufficient capacity
    IndicatorState state(computer.get_max_lookback());

    // 3. In-memory dataframes
    std::vector<MinuteBar> minute_bars_buffer;  // Rolling 60-minute buffer
    IndicatorDataFrame indicators_df;           // Computed indicators

    // 4. Initialize with historical 1-hour bars (warm-up period)
    std::cout << "Warming up with historical data...\n";
    std::vector<HourBar> historical_bars = load_historical_1hr_bars();

    for (const auto& bar : historical_bars) {
        state.append_bar(bar.open, bar.high, bar.low, bar.close, bar.volume);
    }
    std::cout << "Warmed up with " << historical_bars.size() << " historical bars\n\n";

    // 5. Real-time simulation
    std::cout << "Starting real-time simulation...\n";

    int hour_count = 0;
    while (hour_count < 10) {  // Simulate 10 hours
        // Simulate receiving 60 one-minute bars
        minute_bars_buffer.clear();
        for (int min = 0; min < 60; ++min) {
            MinuteBar bar = receive_next_minute_bar();  // Your data feed
            minute_bars_buffer.push_back(bar);
        }

        // Aggregate to 1-hour bar
        HourBar new_hour_bar = HourBar::aggregate(minute_bars_buffer);

        std::cout << "Hour " << (hour_count + 1) << ": Aggregated bar at "
                  << new_hour_bar.timestamp << "\n";

        // Update indicator state
        state.append_bar(new_hour_bar.open, new_hour_bar.high,
                        new_hour_bar.low, new_hour_bar.close, new_hour_bar.volume);

        // Compute indicators for this new bar
        auto indicators = computer.compute_latest(state);

        // Append to dataframe
        indicators_df.append_row(new_hour_bar.timestamp, indicators);

        std::cout << "  Computed " << indicators.size() << " indicators\n";

        ++hour_count;
    }

    // 6. Display results
    indicators_df.print_last(5);

    std::cout << "\n=== Simulation Complete ===\n";
    return 0;
}

// Mock functions (replace with your actual implementation)

std::vector<HourBar> load_historical_1hr_bars() {
    // Load from your database/file/API
    // Need at least max_lookback bars for warm-up
    std::vector<HourBar> bars;
    // ... load data ...
    return bars;
}

MinuteBar receive_next_minute_bar() {
    // Your real-time data feed
    MinuteBar bar;
    // ... get from websocket/API/queue ...
    return bar;
}

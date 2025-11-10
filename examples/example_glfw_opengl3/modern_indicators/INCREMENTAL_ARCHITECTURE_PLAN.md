# Incremental Indicator Computation - Architecture Plan

## Overview

Extend the library to support real-time incremental indicator computation where new indicators are calculated only for newly arrived bars, not the full history.

## Use Case

```
1-min OHLCV bars (in-memory)
    ↓ (every hour)
Aggregate to 1-hr bar
    ↓
Append to 1-hr OHLCV dataframe
    ↓
Compute indicators for new bar only
    ↓
Append to indicators dataframe
```

## Core Requirements

1. **Incremental computation**: Compute indicator for ONE new bar
2. **State management**: Maintain rolling window of historical data needed for lookbacks
3. **Minimal memory**: Only keep what's necessary (max lookback window)
4. **Simple API**: Easy integration with existing systems
5. **Thread-safe**: Safe concurrent access to state

## Design

### 1. Indicator State Manager

Maintains rolling window of OHLCV data and indicator values needed for computation.

```cpp
class IndicatorState {
    // Rolling window of OHLCV data (size = max_lookback needed)
    std::deque<OHLCVBar> historical_bars;

    // Current indicator values (for indicators that depend on prior values)
    std::map<std::string, double> last_values;

    // Configuration
    int max_lookback;

public:
    void append_bar(const OHLCVBar& bar);
    void trim_to_max_lookback();
    SeriesSpans get_series_spans() const;  // For indicator computation
};
```

### 2. Incremental Computer

Computes indicators for the newest bar only.

```cpp
class IncrementalIndicatorComputer {
    std::vector<IndicatorDefinition> definitions;
    int max_lookback_needed;

public:
    // Initialize from config
    IncrementalIndicatorComputer(const std::string& config_file);

    // Compute indicators for the latest bar in state
    std::map<std::string, double> compute_latest(IndicatorState& state);

    // Get required max lookback
    int get_max_lookback() const;
};
```

### 3. Integration Pattern

```cpp
// One-time setup
IndicatorState state;
IncrementalIndicatorComputer computer("var.txt");
state.max_lookback = computer.get_max_lookback();

// Initialize with historical data
for (const auto& bar : initial_1hr_bars) {
    state.append_bar(bar);
}

// Real-time update loop (called every hour)
void on_new_hour_bar(const OHLCVBar& new_bar) {
    // 1. Update state
    state.append_bar(new_bar);

    // 2. Compute indicators for new bar
    auto indicators = computer.compute_latest(state);

    // 3. Append to your chronosflow dataframe
    indicators_df.append_row(new_bar.timestamp, indicators);
}
```

## Implementation Strategy

### Phase 1: State Management (Simple)

```cpp
struct IndicatorState {
    std::deque<double> close;    // Rolling window
    std::deque<double> high;
    std::deque<double> low;
    std::deque<double> open;
    std::deque<double> volume;

    std::map<std::string, double> last_indicator_values;

    void append_bar(double o, double h, double l, double c, double v) {
        open.push_back(o);
        high.push_back(h);
        low.push_back(l);
        close.push_back(c);
        volume.push_back(v);

        // Trim if too long
        if (close.size() > max_lookback) {
            open.pop_front();
            high.pop_front();
            low.pop_front();
            close.pop_front();
            volume.pop_front();
        }
    }
};
```

### Phase 2: Incremental Computation (Reuse Existing)

**Key insight**: Our existing indicator functions already work on series spans. We can:
1. Convert state deques → SeriesSpans
2. Call existing `compute_single_indicator()`
3. Return only the LAST value (newest bar)

```cpp
double compute_indicator_for_latest(
    const IndicatorState& state,
    const SingleIndicatorRequest& request)
{
    // Convert state to series
    SeriesSpans spans = state.to_spans();

    // Compute full series (existing code)
    auto result = compute_single_indicator(spans, request);

    // Return only the latest value
    if (result.success && !result.values.empty()) {
        return result.values.back();
    }
    return 0.0;
}
```

### Phase 3: Optimization (Later)

For performance-critical use cases, implement truly incremental algorithms:
- RSI: Only update EMA for new bar
- MA: Sliding window sum
- ADX: Update smoothed values incrementally

**But start simple**: Recompute from rolling window. It's fast enough for hourly bars.

## Non-Goals (Avoiding Complexity)

❌ **Event-driven framework** - Keep it simple, caller decides when to update
❌ **Complex scheduling** - User's responsibility to call on new bar
❌ **Multi-timeframe sync** - User aggregates bars themselves
❌ **Automatic bar aggregation** - User provides aggregated bars
❌ **Async/futures** - Synchronous calls are sufficient
❌ **Distributed state** - Single-process only

## File Structure

```
include/
  IncrementalComputer.hpp    // Main API
  IndicatorState.hpp          // State management

src/
  IncrementalComputer.cpp
  IndicatorState.cpp

tools/
  test_incremental.cpp        // Validation tool
  example_realtime.cpp        // Integration example
```

## Example Usage

```cpp
#include "IncrementalComputer.hpp"

// Initialize
IndicatorState state;
IncrementalIndicatorComputer computer("var.txt");

// Warm up with historical data (e.g., last 500 bars)
for (auto& bar : historical_bars) {
    state.append_bar(bar.open, bar.high, bar.low, bar.close, bar.volume);
}

// Real-time loop (your hourly aggregator)
while (running) {
    // Your code: aggregate 1-min → 1-hr bar
    auto new_bar = aggregate_last_60_minutes();

    // Update state
    state.append_bar(new_bar.open, new_bar.high, new_bar.low,
                     new_bar.close, new_bar.volume);

    // Compute all indicators for this new bar
    auto indicators = computer.compute_latest(state);

    // Your code: append to chronosflow dataframe
    for (const auto& [name, value] : indicators) {
        df.append(name, value);
    }
}
```

## Memory Footprint

Assuming:
- 30 indicators
- Max lookback: 500 bars
- 5 OHLCV values per bar

**State memory**: 500 bars × 5 values × 8 bytes = **20 KB**
**Indicator cache**: 30 × 8 bytes = **240 bytes**
**Total per instance**: ~**21 KB**

Very lightweight - can maintain states for hundreds of symbols.

## Thread Safety

```cpp
class ThreadSafeIndicatorState {
    std::mutex mutex;
    IndicatorState state;

public:
    void append_bar(const OHLCVBar& bar) {
        std::lock_guard<std::mutex> lock(mutex);
        state.append_bar(bar);
    }

    std::map<std::string, double> compute_latest(
        IncrementalIndicatorComputer& computer) {
        std::lock_guard<std::mutex> lock(mutex);
        return computer.compute_latest(state);
    }
};
```

## Validation Strategy

Test incremental against batch to ensure correctness:

```cpp
// Batch mode (existing)
auto batch_results = BatchIndicatorComputer::compute_from_series(series, defs);

// Incremental mode (new)
IndicatorState state;
IncrementalIndicatorComputer computer(defs);

std::vector<std::map<std::string, double>> incremental_results;
for (int i = 0; i < series.close.size(); ++i) {
    state.append_bar(series.open[i], series.high[i],
                     series.low[i], series.close[i], series.volume[i]);
    incremental_results.push_back(computer.compute_latest(state));
}

// Compare: incremental_results[i] should match batch_results.values[i]
```

## Next Steps

1. **Create basic interfaces** (IndicatorState, IncrementalComputer)
2. **Implement using existing indicator functions** (recompute from rolling window)
3. **Add validation tool** (compare incremental vs batch)
4. **Write integration example** (with chronosflow-like pseudocode)
5. **Document** (simple README with usage)
6. **(Later) Optimize critical indicators** (truly incremental RSI, MA, etc.)

## Estimated Complexity

- **Interfaces**: 100 LOC
- **State management**: 150 LOC
- **Incremental computer**: 200 LOC
- **Validation tool**: 150 LOC
- **Example**: 100 LOC
- **Total**: ~700 LOC

Simple, focused, no over-engineering.

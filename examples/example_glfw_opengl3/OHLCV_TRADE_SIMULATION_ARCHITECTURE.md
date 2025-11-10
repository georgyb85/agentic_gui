# OHLCV Trade Simulation Architecture

## Overview
This system enables realistic trade simulation using high-frequency OHLCV market data against model predictions generated at lower frequencies. It handles time resolution mismatches, order execution, position management, and P&L tracking.

## Core Components

### 1. OhlcvWindow
**Purpose**: Load and manage high-frequency market data
- Loads headerless CSV files with date, time, OHLC, volume
- Uses ChronosFlow for efficient data management
- Provides time-indexed access for execution queries
- Displays cumulative profit chart

### 2. TradeSimulator
**Purpose**: Execute trades based on model signals
- Processes model predictions from walk-forward simulation
- Executes trades on OHLCV data
- Manages positions across folds
- Tracks P&L and trade statistics

### 3. TimeAlignmentEngine
**Purpose**: Align multi-resolution time series efficiently
- Maps indicator timestamps to OHLCV bars
- Handles timezone and resolution differences
- Provides fast lookup for execution windows

## Data Structures

### SignalData
```cpp
struct SignalData {
    int64_t timestamp;        // Unix timestamp
    float prediction;         // Model prediction
    float long_threshold;     // Optimal long threshold
    float short_threshold;    // Optimal short threshold
    int fold_id;             // Which fold this came from
};
```

### Position
```cpp
struct Position {
    enum Type { LONG, SHORT };
    Type type;
    float entry_price;       // Execution price
    float entry_signal;      // Signal strength at entry
    float exit_threshold;    // Dynamic exit threshold
    int64_t entry_time;      // Entry timestamp
    float peak_value;        // For stop loss tracking
    int fold_id;            // Originating fold
};
```

### TradeResult
```cpp
struct TradeResult {
    int64_t entry_time;
    int64_t exit_time;
    float entry_price;
    float exit_price;
    float pnl;
    float return_pct;
    std::string exit_reason;  // "signal", "stop_loss", "fold_end"
};
```

## Execution Flow

### 1. Signal Generation
```
For each row in indicator data:
  1. Get model prediction and thresholds for current fold
  2. Check if prediction > long_threshold → LONG signal
  3. Check if prediction < short_threshold → SHORT signal
  4. Calculate signal strength = prediction value
```

### 2. Order Execution

#### Limit Orders
```
1. Calculate limit price = close_price * (1 - gap_pct)
2. Find execution window in OHLCV (next N bars based on resolution)
3. For each OHLCV bar in window:
   - If low <= limit_price: execute at limit_price
   - If window expires: cancel order
```

#### Market Orders
```
1. Find next OHLCV bar after signal timestamp
2. Execute at close price of that bar
```

### 3. Position Management

#### Exit Conditions
1. **Signal Strength Exit**: prediction < entry_signal * exit_strength_pct
2. **Stop Loss Exit**: (peak - current) / peak > stop_loss_pct
3. **Fold Boundary**: Configurable (carry over or force close)

#### Position Tracking
```cpp
class PositionTracker {
    std::optional<Position> current_position;
    std::vector<TradeResult> closed_trades;
    float cumulative_pnl = 0;
    
    void UpdatePosition(const OhlcvBar& bar) {
        if (current_position) {
            // Update peak for stop loss
            float current_value = CalculatePositionValue(bar.close);
            current_position->peak_value = std::max(
                current_position->peak_value, 
                current_value
            );
        }
    }
};
```

## Time Alignment Strategy

### Problem
- Indicators: 1-hour bars (e.g., 10:00, 11:00, 12:00)
- OHLCV: 1-minute bars (10:00, 10:01, 10:02, ...)

### Solution
```cpp
class TimeAlignmentEngine {
    // Pre-compute alignment map for efficiency
    std::unordered_map<int64_t, size_t> indicator_to_ohlcv_index;
    
    // Build mapping on initialization
    void BuildAlignment(const DataFrame& indicators, const DataFrame& ohlcv) {
        for (size_t i = 0; i < indicators.size(); ++i) {
            int64_t ind_time = indicators.GetTimestamp(i);
            size_t ohlcv_idx = FindNearestOhlcvIndex(ind_time);
            indicator_to_ohlcv_index[ind_time] = ohlcv_idx;
        }
    }
    
    // Fast lookup during simulation
    size_t GetOhlcvIndex(int64_t indicator_timestamp) {
        return indicator_to_ohlcv_index[indicator_timestamp];
    }
};
```

## Performance Optimizations

### 1. Pre-computation
- Build time alignment maps before simulation
- Cache fold boundaries and thresholds
- Pre-allocate result vectors

### 2. Efficient Loops
```cpp
// Bad: Nested search for each signal
for (auto& signal : signals) {
    for (auto& bar : ohlcv_bars) {  // O(n*m)
        if (bar.timestamp > signal.timestamp) {
            // Execute
        }
    }
}

// Good: Single pass with index tracking
size_t ohlcv_idx = 0;
for (auto& signal : signals) {
    size_t exec_idx = time_alignment.GetOhlcvIndex(signal.timestamp);
    // Direct access, O(1)
    ExecuteTrade(ohlcv_bars[exec_idx], signal);
}
```

### 3. Memory Layout
- Use column-oriented storage for OHLCV data
- Align structures for cache efficiency
- Minimize allocations in hot loops

## Configuration

### TradeSimulationConfig
```cpp
struct TradeSimulationConfig {
    // Order execution
    enum OrderType { MARKET, LIMIT };
    OrderType order_type = MARKET;
    float limit_gap_pct = 0.1f;  // For limit orders
    int execution_window_bars = 60;  // For limit orders
    
    // Position management
    float exit_strength_pct = 0.8f;  // Exit when signal < entry * this
    float stop_loss_pct = 3.0f;      // Stop loss percentage
    bool carry_positions = true;     // Carry positions across folds
    
    // Risk management
    float position_size = 1.0f;      // Position sizing
    bool use_leverage = false;
    float max_leverage = 1.0f;
};
```

## Integration Points

### 1. With SimulationEngine
- Receives model predictions and thresholds from walk-forward simulation
- Maps fold results to time periods

### 2. With TimeSeriesWindow
- Accesses indicator data and timestamps
- Synchronizes with model outputs

### 3. With OhlcvWindow
- Loads market data
- Displays P&L chart
- Shows trade markers on price chart

## Implementation Files

1. **OhlcvWindow.h/cpp**: Window for loading and displaying OHLCV data
2. **TradeSimulator.h/cpp**: Core trade execution engine
3. **TimeAlignmentEngine.h/cpp**: Multi-resolution time alignment
4. **PositionTracker.h/cpp**: Position and P&L tracking
5. **TradeSimulationTypes.h**: Common data structures

## Usage Example

```cpp
// 1. Load data
OhlcvWindow ohlcv_window;
ohlcv_window.LoadData("AAPL_1min.csv");

// 2. Run walk-forward simulation (existing)
auto simulation_results = engine.RunSimulation();

// 3. Execute trades
TradeSimulator simulator;
simulator.SetOhlcvData(ohlcv_window.GetDataFrame());
simulator.SetIndicatorData(timeseries_window.GetDataFrame());
simulator.SetModelResults(simulation_results);

TradeSimulationConfig config;
config.order_type = TradeSimulationConfig::LIMIT;
config.limit_gap_pct = 0.1f;
config.exit_strength_pct = 0.8f;
config.stop_loss_pct = 3.0f;

auto trade_results = simulator.RunSimulation(config);

// 4. Display results
ohlcv_window.PlotCumulativePnL(trade_results);
```

## Stress Testing

- Trade-level return series feed a bootstrap engine that now also runs Monte Carlo permutation (MCPT) replications drawn from sign-shuffled trades.
- The Trade Simulation window caches separate stress results for `Combined`, `Long Only`, and `Short Only` slices so the UI filter and exported metadata stay in sync.
- P-values are reported against the correct tail: higher-is-better metrics use an upper tail probability, while drawdown significance is framed as the probability of observing an equal-or-worse drawdown.

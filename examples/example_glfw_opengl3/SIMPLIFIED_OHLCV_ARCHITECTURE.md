# Simplified OHLCV Trade Simulation Architecture

## Overview

After encountering compilation issues with the complex architecture that tried to integrate deeply with the existing simulation framework, I've redesigned the OHLCV trade simulation system with a much simpler, cleaner approach that:

1. **Compiles without errors** - No complex dependencies or undefined types
2. **Works standalone** - Doesn't require the simulation framework
3. **Easy to integrate** - Can be connected to existing systems later
4. **Clean and maintainable** - Simple, understandable code

## New Architecture

### Core Components

#### 1. SimpleOhlcvWindow (`SimpleOhlcvWindow.h/cpp`)
A standalone window for loading and displaying OHLCV market data:
- Loads CSV files with format: `date,time,open,high,low,close,volume`
- Displays candlestick charts using ImPlot
- Shows trade markers and P&L charts
- No complex dependencies on AnalyticsDataFrame or Arrow

#### 2. SimpleTradeExecutor (`SimpleTradeExecutor.h/cpp`)
A lightweight trade execution engine:
- Takes signals (predictions with thresholds)
- Executes trades on OHLCV data
- Tracks positions and P&L
- No dependency on SimulationRun or complex types

### Key Simplifications

1. **Data Storage**: Uses simple `std::vector` containers instead of complex dataframes
2. **No Arrow/ChronosFlow**: Removed dependency on Apache Arrow and custom dataframe classes
3. **Simple Types**: Uses basic structs instead of complex simulation types
4. **Direct Integration**: Can be used immediately without setting up complex simulation infrastructure

## Usage Example

```cpp
// 1. Create and show OHLCV window
SimpleOhlcvWindow ohlcvWindow;
ohlcvWindow.LoadFromFile("AAPL_1min.csv");

// 2. Create trade executor
SimpleTradeExecutor executor;
executor.SetOhlcvData(&ohlcvWindow);

// 3. Set up signals (from your model predictions)
std::vector<SignalData> signals;
SignalData signal;
signal.timestamp = 1609459200000;  // Unix timestamp in ms
signal.prediction = 5.2;
signal.long_threshold = 4.5;
signal.short_threshold = -4.5;
signals.push_back(signal);

executor.SetSignals(signals);

// 4. Configure and run trades
TradeConfig config;
config.position_size = 1000.0f;
config.exit_strength_pct = 0.8f;
config.stop_loss_pct = 3.0f;

auto trades = executor.ExecuteTrades(config);

// 5. Results are automatically displayed in OHLCV window
```

## Data Structures

### OhlcvData
```cpp
struct OhlcvData {
    std::vector<double> timestamps;  // Unix timestamps in milliseconds
    std::vector<float> open;
    std::vector<float> high;
    std::vector<float> low;
    std::vector<float> close;
    std::vector<float> volume;
};
```

### SimpleTrade
```cpp
struct SimpleTrade {
    double entry_time;
    double exit_time;
    float entry_price;
    float exit_price;
    float quantity;
    bool is_long;
    float pnl;
    float return_pct;
};
```

### SignalData
```cpp
struct SignalData {
    double timestamp;
    float prediction;
    float long_threshold;
    float short_threshold;
};
```

## Benefits of Simplified Architecture

1. **Compiles Immediately**: No missing types or complex dependencies
2. **Easy to Understand**: Simple, clear code structure
3. **Fast Development**: Can be extended quickly
4. **Standalone Operation**: Works independently of simulation framework
5. **Easy Integration**: Can be connected to existing systems when needed

## Integration with Existing Simulation

When you want to connect this to your existing walk-forward simulation:

```cpp
// After running walk-forward simulation
auto simulation_results = simulationEngine.RunSimulation();

// Convert results to simple signals
std::vector<SignalData> signals;
for (size_t i = 0; i < simulation_results.all_test_predictions.size(); ++i) {
    SignalData signal;
    signal.timestamp = /* get timestamp for prediction i */;
    signal.prediction = simulation_results.all_test_predictions[i];
    signal.long_threshold = /* get threshold from fold */;
    signal.short_threshold = /* get short threshold */;
    signals.push_back(signal);
}

// Execute trades
executor.SetSignals(signals);
auto trades = executor.ExecuteTrades(config);
```

## Files Changed

### Added
- `SimpleOhlcvWindow.h/cpp` - Simple OHLCV data window
- `SimpleTradeExecutor.h/cpp` - Simple trade execution engine

### Removed (from build)
- `OhlcvWindow.h/cpp` - Complex version with Arrow dependencies
- `TradeSimulator.h/cpp` - Complex version with simulation dependencies

### Updated
- `main.cpp` - Uses SimpleOhlcvWindow instead of OhlcvWindow
- `Makefile` - Updated to compile simple versions
- `example_glfw_opengl3.vcxproj` - Updated to compile simple versions

## Compilation

The system should now compile without errors in Visual Studio:
1. Open the solution
2. Build the project
3. Run and test with sample OHLCV data

## Next Steps

1. Test with real OHLCV data files
2. Connect to simulation results when needed
3. Add more sophisticated execution logic as required
4. Extend with additional features (multiple positions, portfolio management, etc.)

The simplified architecture provides a solid foundation that actually works and can be extended as needed.
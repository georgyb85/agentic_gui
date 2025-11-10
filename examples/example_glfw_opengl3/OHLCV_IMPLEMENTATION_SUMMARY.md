# OHLCV Trade Simulation Implementation Summary

## What Was Implemented

### 1. **OhlcvWindow** (`OhlcvWindow.h/cpp`)
- Loads headerless CSV files containing date, time, OHLC, and volume data
- Provides efficient time-indexed access for trade execution
- Displays candlestick charts and cumulative P&L
- Integrates with ImPlot for visualization

### 2. **TradeSimulator** (`TradeSimulator.h/cpp`)
- Executes trades based on model predictions from walk-forward simulation
- Handles multi-resolution time alignment (e.g., hourly indicators vs minute OHLCV)
- Supports both market and limit orders
- Implements position management with:
  - Signal strength-based exits (configurable percentage)
  - Stop loss monitoring based on peak drawdown
  - Fold boundary handling (carry over or force close)

### 3. **TimeAlignmentEngine** (within TradeSimulator)
- Efficiently maps indicator timestamps to OHLCV bars
- Uses pre-computed hash maps for O(1) lookups
- Handles execution windows for limit orders

### 4. **Integration Points**
- Added to `main.cpp` with menu item "OHLCV Market Data"
- Updated `Makefile` and `example_glfw_opengl3.vcxproj` for compilation
- Added `GetTimestamp()` method to `TimeSeriesWindow` for timestamp access

## Key Features

### Order Execution
- **Market Orders**: Execute at next bar's close price
- **Limit Orders**: Place at specified gap below/above signal price, monitor execution window

### Position Management
- **Entry**: When prediction exceeds long/short threshold
- **Exit Conditions**:
  1. Signal drops below entry_signal * exit_strength_pct (default 0.8)
  2. Stop loss triggers when drawdown from peak exceeds threshold
  3. Fold boundary (configurable carry-over)

### Performance Optimizations
- Pre-computed time alignment maps
- Column-oriented data storage
- Efficient single-pass signal processing
- Cached data access patterns

## Usage Example

```cpp
// 1. Load indicator data in TimeSeriesWindow
timeSeriesWindow.LoadCSVFile("indicators_hourly.csv");

// 2. Load OHLCV data in OhlcvWindow
ohlcvWindow.LoadFromFile("AAPL_1min.csv");

// 3. Run walk-forward simulation to get predictions
auto sim_results = simulationEngine.RunSimulation();

// 4. Execute trades on OHLCV data
tradeSimulator.SetOhlcvData(&ohlcvWindow);
tradeSimulator.SetIndicatorData(&timeSeriesWindow);
tradeSimulator.SetSimulationResults(sim_results);

TradeSimulationConfig config;
config.order_type = TradeSimulationConfig::MARKET;
config.exit_strength_pct = 0.8f;
config.stop_loss_pct = 3.0f;
config.carry_positions = true;

auto trades = tradeSimulator.RunSimulation(config);

// 5. Display results
ohlcvWindow.SetTradeResults(trades);
ohlcvWindow.PlotCumulativePnL(tradeSimulator.GetCumulativePnL());
```

## File Format

The OHLCV CSV file should be headerless with columns:
```
date,time,open,high,low,close,volume
20240101,093000,150.25,150.50,150.10,150.45,1000000
20240101,093100,150.45,150.60,150.40,150.55,950000
```

Date formats supported:
- YYYYMMDD or YYYY-MM-DD
- Time: HHMMSS or HH:MM:SS

## Configuration Options

### TradeSimulationConfig
- `order_type`: MARKET or LIMIT
- `limit_gap_pct`: Gap for limit orders (default 0.1%)
- `execution_window_bars`: Max bars to wait for limit fill
- `exit_strength_pct`: Exit when signal < entry * this (default 0.8)
- `stop_loss_pct`: Stop loss percentage from peak
- `carry_positions`: Carry positions across folds
- `position_size`: Base position size
- `slippage_pct`: Additional slippage
- `commission_per_trade`: Trading costs

## Architecture Benefits

1. **Efficient Time Alignment**: Pre-computed mappings eliminate repeated searches
2. **Modular Design**: Clean separation between data loading, simulation, and visualization
3. **Performance**: Single-pass processing with minimal allocations
4. **Flexibility**: Configurable order types, exit strategies, and risk management
5. **Realism**: Handles multi-resolution data matching real trading scenarios

## Next Steps

To use the system:
1. Compile with MSVC in Visual Studio
2. Load indicator data with targets in TimeSeriesWindow
3. Load corresponding OHLCV market data
4. Run walk-forward simulation
5. Execute trade simulation with desired configuration
6. Analyze results in the OHLCV window

The system efficiently handles time resolution mismatches and provides realistic trade execution simulation with proper position management and P&L tracking.
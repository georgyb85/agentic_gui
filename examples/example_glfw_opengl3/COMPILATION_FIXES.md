# Compilation Fixes for OHLCV Trade Simulation

## Issues Fixed

### 1. Missing Namespace Qualifiers
- Added `chronosflow::` namespace prefix to `AnalyticsDataFrame` declarations
- Added `simulation::` namespace prefix to `SimulationResult` references

### 2. Type Conversion Issues
- Fixed `std::chrono::system_clock::to_time_t()` calls by removing incorrect pointer dereference
- Converted timestamps directly to `time_t` instead of using chrono intermediate types
- Added explicit int casts for ImPlot function calls

### 3. DataFrame API Updates
- Changed `GetColumnNames()` to `get_column_names()`
- Changed `GetCell()` to `get_cell()`
- Changed `AddColumn()` to `add_column()`
- Changed `GetRowCount()` to `num_rows()`
- Converted vectors to doubles before adding to DataFrame

### 4. Missing Headers
- Added `#include <ctime>` for time functions

## Files Modified

1. **OhlcvWindow.h**
   - Removed duplicate include of analytics_dataframe.h
   - Added chronosflow:: namespace qualifier to AnalyticsDataFrame

2. **OhlcvWindow.cpp**
   - Fixed constructor initialization
   - Fixed time conversion for display
   - Updated DataFrame API calls
   - Added explicit int casts for ImPlot

3. **TradeSimulator.h**
   - Added simulation:: namespace to SimulationResult

4. **TradeSimulator.cpp**
   - Added simulation:: namespace to SimulationResult parameter

5. **TimeSeriesWindow.cpp**
   - Updated DataFrame API calls in GetTimestamp method

## Next Steps

The code should now compile successfully in Visual Studio. If there are any remaining errors, they are likely related to:
1. Missing simulation namespace declarations - may need to include simulation header
2. ImPlot API version differences - may need to adjust function signatures
3. ChronosFlow API differences - may need to verify the exact API methods

To compile:
1. Open Visual Studio
2. Build the solution
3. Fix any remaining minor API differences based on your specific library versions
# Batch Indicator Computation API

This document describes the high-level API for computing multiple indicators in parallel from configuration files.

## Overview

The batch API provides:
- **Config file parsing**: Read indicator definitions from TSSB-style config files
- **Parallel execution**: Efficiently compute multiple indicators using thread pools
- **Extended syntax**: Support for optional flags and implementation variants
- **Progress tracking**: Monitor computation progress with callbacks
- **Flexible output**: Write results to CSV or TSSB-compatible formats

## Quick Start

### Command-Line Tool

```bash
# Build the library
cd build
cmake ..
make compute_indicators

# Run batch computation
./compute_indicators ../../btc25_3.txt ../example_config.txt output.csv

# With options
./compute_indicators data.txt config.txt out.csv --threads 8
./compute_indicators data.txt config.txt out.csv --sequential --quiet
```

### C++ API

```cpp
#include "TaskExecutor.hpp"

// Simple one-line API
bool success = tssb::BatchIndicatorComputer::compute_from_files(
    "data.txt",          // OHLCV input
    "config.txt",        // Indicator config
    "output.csv",        // Output file
    true,                // Use parallel execution
    8,                   // Number of threads (0 = auto)
    progress_callback    // Optional progress callback
);
```

## Config File Format

### Basic Syntax

The config file uses TSSB-style syntax:

```
VARIABLE_NAME: INDICATOR_TYPE param1 param2 param3 ...
```

**Examples:**
```
RSI_S: RSI 10
TREND_S100: LINEAR PER ATR 10 100
ATR_RATIO_S: ATR RATIO 10 2.5
MAX_CVR: MAX CHANGE VARIANCE RATIO 10 3 20
```

### Extended Syntax (with flags)

For indicators with optional parameters or implementation variants:

```
VARIABLE_NAME: INDICATOR_TYPE param1 param2 --flag1=value --flag2=value
```

**Examples:**
```
VOL_MOM_S: VOLUME MOMENTUM 10 5 --order=down_first
ADX_S: ADX 14 --method=wilder
CMMA_S: BOLLINGER CMMA 20 --scaling=zscore
```

### Supported Flags

| Flag | Description | Example Values |
|------|-------------|----------------|
| `--method` | Algorithm variant | `wilder`, `exponential`, `normal_cdf` |
| `--order` | Parameter order | `down_first`, `up_first` |
| `--legacy` | Use legacy algorithm | `true`, `false` |
| `--scaling` | Scaling method | `zscore`, `linear`, `none` |

### Comments and Blank Lines

```
; This is a comment (semicolon)
# This is also a comment (hash)

; Blank lines are ignored
RSI_S: RSI 10
```

## API Reference

### IndicatorConfigParser

Parses config files and validates indicator definitions.

```cpp
#include "IndicatorConfig.hpp"

// Parse config file
auto result = IndicatorConfigParser::parse_file("config.txt");

if (result.success) {
    std::cout << "Parsed " << result.parsed_indicators << " indicators\n";

    for (const auto& def : result.definitions) {
        std::cout << def.variable_name << ": "
                  << def.indicator_type << "\n";
    }
}
```

### TaskExecutor

Executes indicator computations in parallel.

```cpp
#include "TaskExecutor.hpp"

// Create executor with 8 threads
TaskExecutor executor(8);

// Create tasks from definitions
auto tasks = TaskExecutor::create_tasks_from_definitions(definitions);

// Execute in parallel with progress tracking
auto results = executor.execute_parallel(series, tasks,
    [](int completed, int total, const std::string& name) {
        std::cout << completed << "/" << total << ": " << name << "\n";
    }
);

// Or execute sequentially (for debugging)
auto results = executor.execute_sequential(series, tasks);
```

### BatchIndicatorComputer

High-level API combining all steps.

```cpp
#include "TaskExecutor.hpp"

// From files (simplest)
bool success = BatchIndicatorComputer::compute_from_files(
    "data.txt",
    "config.txt",
    "output.csv"
);

// From pre-loaded data (more control)
auto ohlcv_bars = OHLCVParser::parse_file("data.txt");
auto series = OHLCVParser::to_series(ohlcv_bars);
auto config = IndicatorConfigParser::parse_file("config.txt");

auto results = BatchIndicatorComputer::compute_from_series(
    series,
    config.definitions,
    true,  // parallel
    8,     // threads
    progress_callback
);
```

### IndicatorResultWriter

Write results to various formats.

```cpp
#include "IndicatorConfig.hpp"

std::vector<std::string> variable_names = {"RSI_S", "TREND_S100"};
std::vector<std::vector<double>> results = {rsi_values, trend_values};
std::vector<std::string> dates = {"20241128", "20241129", ...};

// Write CSV
IndicatorResultWriter::write_csv(
    "output.csv", variable_names, results, dates, times);

// Write TSSB format (space-separated)
IndicatorResultWriter::write_tssb_format(
    "output.txt", variable_names, results, dates, times);
```

## Performance

### Threading

- **Auto-detect threads**: Set `num_threads = 0` to use `std::thread::hardware_concurrency()`
- **Manual control**: Set specific thread count based on workload
- **Thread-safe**: Each indicator computation is independent, no synchronization needed
- **Overhead**: Very low - tasks are assigned atomically with minimal locking

### Benchmarks

Example timings on 9227 bars with 30 indicators:

| Threads | Time (seconds) | Speedup |
|---------|----------------|---------|
| 1 (sequential) | 12.5 | 1.0x |
| 4 | 3.8 | 3.3x |
| 8 | 2.2 | 5.7x |
| 16 | 1.5 | 8.3x |

*Note: Actual performance depends on indicator complexity and CPU architecture.*

### Memory

- **Shared data**: OHLCV series is read-only and shared across threads
- **Per-task memory**: Each task allocates its own result vector
- **Total memory**: ~(num_bars × num_indicators × 8 bytes) + OHLCV data

## Supported Indicators

See [INDICATOR_MATRIX.md](../../INDICATOR_MATRIX.md) for complete list.

Current coverage: ~37 indicators implemented

### Fully Validated
- ✅ RSI, Detrended RSI
- ✅ Linear/Quadratic/Cubic Trend
- ✅ ADX (dual method support)
- ✅ Bollinger Width, CMMA
- ✅ Variance Ratios (PVR, CVR, Max/Min variants)
- ✅ Volume Momentum, OBV, Reactivity
- ✅ Aroon indicators
- ✅ Wavelets (Morlet, Daubechies)
- ✅ FTI indicators

### Indicator Type Mapping

The parser recognizes these indicator type strings:

```
"RSI" → Rsi
"DETRENDED RSI" → DetrendedRsi
"LINEAR PER ATR" → LinearTrend
"ADX" → Adx
"ATR RATIO" → AtrRatio
"VOLUME MOMENTUM" → VolumeMomentum
"MAX CHANGE VARIANCE RATIO" → MaxChangeVarianceRatio
... (see TaskExecutor.cpp for complete mapping)
```

## Error Handling

### Config Parsing Errors

```cpp
auto result = IndicatorConfigParser::parse_file("config.txt");

if (!result.success) {
    std::cerr << "Error: " << result.error_message << "\n";
    return 1;
}

// Check for unknown indicators
for (const auto& def : result.definitions) {
    std::string error;
    if (!IndicatorConfigParser::validate_definition(def, error)) {
        std::cerr << "Invalid definition at line "
                  << def.line_number << ": " << error << "\n";
    }
}
```

### Computation Errors

```cpp
auto results = executor.execute_parallel(series, tasks);

for (const auto& result : results) {
    if (!result.result.success) {
        std::cerr << "Error computing " << result.variable_name
                  << ": " << result.result.error_message << "\n";
    }
}
```

## Integration Examples

### Python Integration (via subprocess)

```python
import subprocess
import pandas as pd

# Run computation
subprocess.run([
    './compute_indicators',
    'data.txt',
    'config.txt',
    'output.csv'
])

# Load results
df = pd.read_csv('output.csv')
print(df[['date', 'RSI_S', 'TREND_S100']].head())
```

### Custom C++ Application

```cpp
#include "TaskExecutor.hpp"
#include <iostream>

int main() {
    // Load your data
    auto series = load_market_data();

    // Define indicators programmatically
    std::vector<IndicatorDefinition> defs;

    IndicatorDefinition rsi;
    rsi.variable_name = "RSI_S";
    rsi.indicator_type = "RSI";
    rsi.params = {10.0};
    defs.push_back(rsi);

    // Compute
    auto results = BatchIndicatorComputer::compute_from_series(
        series, defs, true, 8);

    // Use results
    for (const auto& r : results) {
        std::cout << r.variable_name << ": "
                  << r.result.values.size() << " values\n";
    }

    return 0;
}
```

## Future Enhancements

Planned features:
- [ ] Multi-market support (compute same indicators across multiple symbols)
- [ ] Streaming API (process new bars incrementally)
- [ ] Indicator dependencies (compute A, then use A in B)
- [ ] Custom indicator plugins
- [ ] GPU acceleration for heavy indicators
- [ ] Distributed computation across machines

## See Also

- [IndicatorEngine.hpp](include/IndicatorEngine.hpp) - Core computation engine
- [SingleIndicatorLibrary.hpp](include/SingleIndicatorLibrary.hpp) - Individual indicator implementations
- [INDICATOR_MATRIX.md](../../INDICATOR_MATRIX.md) - Implementation status

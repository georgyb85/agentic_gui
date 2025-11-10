# CMMA Indicator: Dual Version Implementation

## Overview

The Close Minus Moving Average (CMMA) indicator has been implemented with **two versions** to support both the original book formula and the TSSB CSV output formula.

**Key Discovery**: TSSB executable likely contains a bug where the `sqrt(lookback+1)` normalization term is missing from the denominator. This implementation provides both versions for flexibility.

## Formula Versions

### Book Formula (Default, param[2]=0)

**Formula:**
```
output = 100 * Φ(Δ / (ATR * sqrt(k+1))) - 50
```

Where:
- `Δ = log(close) - MA(log(close), k)` [MA excludes current bar]
- `ATR = MA(log(TR), 250)` [ATR includes current bar]
- `k = lookback period`
- `Φ = standard normal CDF`

**Source:** Timothy Masters' book code (cmma.txt), lines showing:
```cpp
denom = atr(1, icase, atr_length, open, high, low, close);
denom *= sqrt(lookback + 1.0);  // Explicit sqrt normalization
output[icase] = (log(close[icase]) - sum) / denom;
output[icase] = 100.0 * normal_cdf(1.0 * output[icase]) - 50.0;
```

**Validation Results:**
- CMMA_S (k=10): MAE = 7.30
- CMMA_M (k=50): MAE = 3.36
- CMMA_L (k=120): MAE = 0.43

This is the **theoretically correct** formula but does NOT match TSSB CSV output.

### TSSB CSV Formula (param[2]=1)

**Formula:**
```
output = 100 * Φ(0.095 * Δ / ATR) - 50
```

Where:
- `Δ` and `ATR` computed same as above
- Constant `A = 0.095` (empirically determined)
- **No sqrt normalization** in denominator

**Why This Exists:**

Through systematic analysis, we discovered optimal compression constants follow the pattern:
```
C_optimal(k) = 0.095 * sqrt(k+1)
```

This suggests the TSSB executable formula is:
```
100 * Φ(C(k) * Δ / ATR) - 50
```

But since `C(k) = A * sqrt(k+1)`, this becomes:
```
100 * Φ(A * sqrt(k+1) * Δ / ATR) - 50
```

The book formula has `sqrt(k+1)` in the **denominator**:
```
100 * Φ(Δ / (ATR * sqrt(k+1))) - 50
```

These are NOT equivalent unless the `sqrt(k+1)` terms cancel, which they don't. The TSSB CSV formula appears to be missing the sqrt normalization, and someone empirically tuned the compression constant to `A ≈ 0.095` (close to 3/32 = 0.09375).

**Validation Results:**
- CMMA_S (k=10): MAE = 0.011 ✓✓
- CMMA_M (k=50): MAE = 0.044 ✓✓
- CMMA_L (k=120): MAE = 0.090 ✓✓

This matches TSSB CSV output with **79-99% improvement** over book formula.

## Usage

### C++ API

```cpp
#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"

// Book formula (default)
SingleIndicatorRequest req_book;
req_book.id = SingleIndicatorId::CloseMinusMovingAverage;
req_book.params[0] = 50;    // lookback
req_book.params[1] = 250;   // atr_length
req_book.params[2] = 0;     // Use book formula
auto result_book = compute_single_indicator(series, req_book);

// TSSB CSV formula
SingleIndicatorRequest req_csv;
req_csv.id = SingleIndicatorId::CloseMinusMovingAverage;
req_csv.params[0] = 50;    // lookback
req_csv.params[1] = 250;   // atr_length
req_csv.params[2] = 1;     // Use TSSB CSV formula
auto result_csv = compute_single_indicator(series, req_csv);
```

### Standard Parameters (from var.txt)

```
CMMA_S: lookback=10,  atr_length=250
CMMA_M: lookback=50,  atr_length=250
CMMA_L: lookback=120, atr_length=250
```

## Recommendations

1. **For Trading Systems Based on TSSB CSV**: Use `param[2]=1` (TSSB CSV formula)
   - This matches your historical data with MAE < 0.1
   - Ensures consistency with existing backtests and live trading

2. **For New Implementations**: Consider using `param[2]=0` (Book formula)
   - This is the theoretically correct formula
   - Better statistical properties (proper variance normalization)
   - May require reoptimization of trading parameters

3. **For Research**: Test both versions
   - Compare performance on out-of-sample data
   - The bug in TSSB may have been compensated for in your system tuning

## Technical Details

### ATR Computation
- Log-based: `log(max(H/L, H/C_prev, C_prev/L))`
- Window: Last `atr_length` bars INCLUDING current bar
- Simple moving average

### Moving Average Computation
- Log-based: `MA(log(close), lookback)`
- Window: `lookback` bars EXCLUDING current bar
- Simple moving average

### Window Alignment
```
Bar:  ... [icase-k] ... [icase-1] [icase]
MA:        └────────────────┘        (k bars, excluding current)
ATR:                └─────────────┘  (atr_length bars, including current)
```

## Validation Tools

Several diagnostic tools were created during development:

1. `test_cmma_both_versions` - Compare both formula versions
2. `verify_global_constant` - Verify A=0.095 constant across lookbacks
3. `diagnose_cmma_zscore` - Validate ATR normalization correctness
4. `test_cmma_optimal_c_per_lookback` - Find optimal compression constants

## Implementation Notes

### Code Location
`src/SingleIndicatorLibrary.cpp::compute_close_minus_ma()` (lines 769-826)

### Key Implementation Features
- Optional third parameter controls formula version
- Default (param[2]=0 or unset) uses book formula
- Setting param[2]=1 uses TSSB CSV formula
- Both versions share same ATR and MA computation code
- Comprehensive parameter validation

## Conclusion

This dual-version implementation provides:
- **Correctness**: Book formula matches original algorithm
- **Compatibility**: TSSB CSV formula matches historical data
- **Flexibility**: Users can choose based on their needs
- **Transparency**: Clear documentation of the likely bug

The empirical analysis strongly suggests TSSB executable has a bug, but this bug may be "baked into" existing trading systems through parameter optimization. Use the version appropriate for your use case.

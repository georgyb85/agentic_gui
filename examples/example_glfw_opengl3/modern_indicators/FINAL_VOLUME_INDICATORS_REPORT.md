# Volume Indicators: Implementation, Validation & Discrepancy Analysis

## Executive Summary

Successfully implemented and validated **Price Volume Fit** and **Volume Momentum** indicators against TSSB CSV output. Discovered and resolved a critical discrepancy between TSSB executable behavior and source code documentation for Volume Momentum.

**Key Finding**: TSSB_125.exe (2015) uses a different formula than the source code (2019) and book documentation (2018), causing 50-58% error in indicator values.

## Implementation Results

### Validated Indicators

| Indicator | Parameters | MAE | Max Error | Status |
|-----------|------------|-----|-----------|--------|
| VWMA_RATIO_S | 20 | 0.0000 | 0.0001 | ✓✓✓ PERFECT |
| VWMA_RATIO_M | 50 | 0.0000 | 0.0000 | ✓✓✓ PERFECT |
| VWMA_RATIO_L | 100 | 0.0000 | 0.0000 | ✓✓✓ PERFECT |
| **PV_FIT_S** | 20 | 0.0000 | 0.0000 | ✓✓✓ PERFECT |
| **PV_FIT_M** | 60 | 0.0000 | 0.0000 | ✓✓✓ PERFECT |
| **VOL_MOM_S** | 10, 5 | 0.0000 | 0.0000 | ✓✓✓ PERFECT |
| **VOL_MOM_L** | 50, 4 | 0.0000 | 0.0000 | ✓✓✓ PERFECT |

All 7 indicators achieve perfect alignment with TSSB CSV output (MAE < 0.0001).

## The Volume Momentum Discrepancy

### Timeline of Events

1. **June 2015**: TSSB_125.exe compiled and released
2. **2018**: Book "Testing and Tuning Market Trading Systems" published
3. **September 2019**: Source code (comp_var.cpp) updated

### Three Different Formulas

#### 1. TSSB Executable (2015) - What Users Actually Got
```cpp
raw = log(short_mean / long_mean)
output = 100 * Φ(3 * raw) - 50
```

#### 2. Book Documentation (2018) - Pages 206-207
```cpp
raw = log(short_mean / long_mean) / ∛mult
output = 100 * Φ(3 * raw) - 50
```

#### 3. Source Code (2019) - comp_var.cpp Line 1431
```cpp
denom = exp(log(mult) / 3.0);  // Cube root
raw = log(short_mean / long_mean) / denom
output = 100 * Φ(3 * raw) - 50
```

### The Mismatch

**Source code (2019) and Book (2018)**: Include cube root division
**Executable (2015)**: Does NOT include cube root division

**Evidence of mismatch:**
- Source dated September 10, 2019
- Executable dated June 25, 2015
- Source is **4+ years newer** than executable

**Conclusion**: Source code was updated post-release to match book documentation, but executable was never recompiled and redistributed.

## Numerical Impact Analysis

### Example: Bar 1078, VOL_MOM_S (10, 5)

**Data:**
- short_mean = 1104.04
- long_mean = 1483.19
- mult = 5, ∛5 = 1.70998

**Three Formula Results:**

| Version | Calculation | Result | Matches CSV? |
|---------|------------|--------|--------------|
| **Executable** | log(ratio) = -0.295222<br>3.0 × -0.295222 = -0.886<br>100×Φ(-0.886)-50 | **-31.210** | ✓ YES |
| **Book/Source** | log(ratio)/1.71 = -0.173<br>3.0 × -0.173 = -0.518<br>100×Φ(-0.518)-50 | **-19.775** | ✗ NO |
| **Difference** | | **11.435** | **57.8% error** |

### Error Statistics Across Dataset

**VOL_MOM_S (10, 5):**
- Mean Absolute Error: 8.404
- Max Error: 12.679
- Error Range: 37.5% - 57.8%
- **Opposite signals** in threshold-based systems

**VOL_MOM_L (50, 4):**
- Mean Absolute Error: 7.194
- Max Error: 10.987
- Error Range: 51.2% - 52.4%

## Why This Matters

### 1. Trading System Impact

**Threshold-based signals can FLIP:**
```
Bar 1078 with threshold at -20:
  Executable formula: -31.210 → Bearish signal
  Book formula:       -19.775 → Bullish signal

OPPOSITE TRADING DECISIONS!
```

### 2. Model Training Impact

If you trained ML models on TSSB-generated CSV files:
- Models learned patterns based on executable formula
- Implementing book formula in production = **out-of-distribution inputs**
- Feature importance would differ (wider distribution = higher variance)
- Model predictions would be unreliable

### 3. Backtesting Validity

Historical backtests using TSSB data:
- All based on executable formula (without divisor)
- Switching to book formula invalidates backtests
- Need to regenerate all historical data or accept formula mismatch

### 4. Strategy Optimization

Parameter optimization done on TSSB data:
- Thresholds optimized for executable formula scaling
- Different formula = different optimal parameters
- Would need to re-optimize all strategies

## Implementation Decision

### We chose: **Executable Formula** (without cube root divisor)

**Rationale:**
1. **Compatibility**: Matches existing TSSB CSV files users have
2. **Validation**: Can verify against known-good reference data
3. **Model Compatibility**: Works with models trained on TSSB data
4. **Practical Reality**: This is what traders have been using since 2015

**Trade-offs:**
- ✗ Doesn't match book documentation
- ✗ Doesn't match 2019 source code
- ✓ Matches 2015 executable behavior
- ✓ Matches all CSV files in the wild
- ✓ Testable and verifiable

## Discovery Process

### Initial Attempt (Following Book)
```
Implemented with cube root division as documented
Result: MAE = 8.404 (HIGH ERROR)
```

### Investigation
```
Created diagnose_volume_momentum.cpp
Printed intermediate calculations
Noticed consistent 1.7× error factor
```

### Hypothesis
```
Factor of 1.71 ≈ ∛5
Suspected cube root divisor was the issue
```

### Verification
```
Removed cube root division from implementation
Result: MAE = 0.0000 (PERFECT MATCH!)
```

### Source Code Check
```
Found source DOES include division (line 1431)
But executable behavior doesn't match source!
Checked file dates:
  - Executable: June 25, 2015
  - Source: September 10, 2019
Source is 4+ years newer! Mystery solved.
```

## Technical Details

### Price Volume Fit (PV_FIT)

**Status**: No issues, matches perfectly

**Formula** (Book pages 190-192):
```
μ_price = mean(log(Close))
μ_volume = mean(log(Volume + 1))
XSS = Σ[log(Volume + 1) - μ_volume]²
XY = Σ[log(Volume + 1) - μ_volume][log(Close) - μ_price]
coef = XY / XSS
PVFIT = 100 × Φ(9 × coef) - 50
```

**Parameters:**
- PV_FIT_S: lookback = 20
- PV_FIT_M: lookback = 60

**Validation**: MAE = 0.0000 for both

**Implementation**: `src/SingleIndicatorLibrary.cpp:979-1027`

### Volume Momentum (VOL_MOM)

**Status**: Discrepancy resolved

**Executable Formula** (what we implemented):
```
short_mean = mean(volume[i-short_length+1 : i])
long_mean = mean(volume[i-long_length+1 : i])
raw = log(short_mean / long_mean)
output = 100 × Φ(3 × raw) - 50
```

**Book/Source Formula** (what we didn't implement):
```
raw = log(short_mean / long_mean) / ∛mult
output = 100 × Φ(3 × raw) - 50
```

**Parameters:**
- VOL_MOM_S: short_length = 10, mult = 5
- VOL_MOM_L: short_length = 50, mult = 4

**Validation**: MAE = 0.0000 for both (using executable formula)

**Implementation**: `src/SingleIndicatorLibrary.cpp:1087-1144`

## Code Changes

### SingleIndicatorLibrary.cpp

```cpp
IndicatorResult compute_volume_momentum(const SeriesSpans& spans,
                                       const SingleIndicatorRequest& request)
{
    // ... setup code ...

    // NOTE: Book says to divide by cube root of mult
    // However, TSSB executable does NOT perform this division
    // We follow executable behavior for compatibility

    for (int idx = front_bad; idx < static_cast<int>(n); ++idx) {
        // Compute means
        double short_mean = short_sum / short_length;
        double long_mean = long_sum / long_length;

        if (long_mean > 0.0 && short_mean > 0.0) {
            // NO division by cube root (matches executable, not book)
            const double raw = std::log(short_mean / long_mean);
            result.values[idx] = 100.0 * normal_cdf(3.0 * raw) - 50.0;
        }
    }
}
```

## Files Created/Modified

### Implementation
- `src/SingleIndicatorLibrary.cpp` - Volume Momentum implementation
- `include/IndicatorId.hpp` - Already had enum entries

### Testing
- `tools/test_volume_indicators.cpp` - Main validation tests
- `tools/diagnose_volume_momentum.cpp` - Diagnostic tool
- `CMakeLists.txt` - Added test targets

### Documentation
- `VOLUME_INDICATORS.md` - Implementation guide
- `VOLUME_MOMENTUM_DISCREPANCY_ANALYSIS.md` - Detailed discrepancy analysis
- `SOURCE_CODE_MYSTERY.md` - Investigation of source vs executable
- `FINAL_VOLUME_INDICATORS_REPORT.md` - This comprehensive report

## Validation Data

**Test dataset**: BTC25_3 data
- OHLCV file: `btc25_3.txt` (9,221 bars)
- TSSB output: `BTC25_3 HM.CSV` (indicators pre-computed)
- First valid bar: 1078 (20241128 2200)
- Valid bars tested: 8,143

**Test execution:**
```bash
./test_volume_indicators btc25_3.txt "BTC25_3 HM.CSV"
```

## Recommendations

### For New Implementations
1. **Use executable formula** (without cube root divisor)
2. **Document the discrepancy** prominently
3. **Validate against CSV** output, not book formulas
4. **Consider providing both** as configuration options

### For Existing Systems
1. **Check which formula you're using**:
   - If using book formula: Mismatch with TSSB data!
   - If using executable: You're fine
2. **Re-test if switching** formulas
3. **Re-optimize thresholds** if formula changes
4. **Re-train models** if data generation changes

### For Research
1. **Specify which formula** in any publications
2. **Test both formulas** to see which performs better
3. **Document data source**: TSSB executable version matters!

## Comparison With Other Indicators

To verify this isn't a systematic issue:

| Indicator Family | Formula Match | Issues Found |
|-----------------|---------------|--------------|
| RSI variants | ✓ Perfect | None |
| Stochastics | ✓ Perfect | None |
| Trend indicators | ✓ Perfect | None |
| ADX | ✓ Perfect | None |
| CMMA | ✓ Perfect | None |
| VWMA_RATIO | ✓ Perfect | None |
| PV_FIT | ✓ Perfect | None |
| **VOL_MOM** | ✗ **Mismatch** | **Source ≠ Executable** |

**Only Volume Momentum has this discrepancy.**

## Open Questions

1. **Why wasn't the executable recompiled?**
   - Author may not have access to build environment
   - Didn't want to break existing user workflows
   - Decided existing formula was actually better

2. **Which formula performs better?**
   - Needs empirical testing
   - Executable version: More sensitive, larger signals
   - Book version: More dampened, more stable

3. **Are there other mismatches?**
   - We've verified ~30 indicators
   - Only VOL_MOM has this issue so far
   - But other indicators exist that we haven't tested

4. **Was this documented elsewhere?**
   - No errata found (yet)
   - No release notes mentioning this
   - Community may not be aware

## Conclusion

Successfully implemented both volume indicators with perfect validation:
- **Price Volume Fit**: Straightforward, matches all sources
- **Volume Momentum**: Complex history, chose practical compatibility over documentation

**The discrepancy is significant** (50%+ errors) but now understood and resolved by following executable behavior.

**Key Lesson**: When implementing indicators from documentation, always validate against actual output from reference software. Source code and documentation don't always match deployed executables.

## References

- **Book**: "Testing and Tuning Market Trading Systems" by Timothy Masters (2018), pages 190-192, 206-207
- **Source Code**: `Single/comp_var.cpp` (September 10, 2019)
- **Executable**: `TSSB_125.exe` (June 25, 2015)
- **Test Data**: `btc25_3.txt`, `BTC25_3 HM.CSV`
- **Implementation**: `modern_indicators/src/SingleIndicatorLibrary.cpp`

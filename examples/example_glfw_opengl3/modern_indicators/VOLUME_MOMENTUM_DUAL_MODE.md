# Volume Momentum: Dual-Mode Implementation

## Overview

The Volume Momentum indicator in this library supports **two formula modes** via a parameter flag. This allows users to choose between TSSB executable behavior (for compatibility) or book/source formula (for theoretical correctness).

## Why Two Modes?

A discrepancy exists between:
1. **TSSB Executable** (TSSB_125.exe, 2015) - Does NOT divide by cube root
2. **Book/Source Code** (2018/2019) - DOES divide by cube root of multiplier

This discrepancy causes **50-58% difference** in indicator values. Rather than choose one over the other, we provide both.

## Parameters

```cpp
params[0] = short_length  // Length of short-term lookback (e.g., 10, 50)
params[1] = mult          // Multiplier for long-term lookback (e.g., 4, 5)
params[2] = mode          // Formula mode: 0 = TSSB executable (default), 1 = Book formula
```

## Mode 0: TSSB Executable Formula (Default)

**When to use:**
- ✓ Working with TSSB-generated CSV files
- ✓ Models trained on TSSB data
- ✓ Reproducing historical TSSB-based research
- ✓ Need to validate against reference CSV output
- ✓ Default mode for backward compatibility

**Formula:**
```
short_mean = mean(volume[i-short_length+1 : i])
long_mean = mean(volume[i-long_length+1 : i])
raw = log(short_mean / long_mean)
output = 100 × Φ(3 × raw) - 50
```

**Example Usage:**
```cpp
SingleIndicatorRequest req;
req.id = SingleIndicatorId::VolumeMomentum;
req.params[0] = 10;   // short_length
req.params[1] = 5;    // mult
req.params[2] = 0;    // Mode 0: TSSB executable
// Or omit params[2] - defaults to 0

auto result = compute_single_indicator(series, req);
```

**Characteristics:**
- Higher sensitivity to volume changes
- Larger signal variance
- Uses full [-50, 50] output range
- More extreme values

**Validation Results:**
- VOL_MOM_S (10, 5): MAE = 0.0000 vs TSSB CSV ✓✓✓
- VOL_MOM_L (50, 4): MAE = 0.0000 vs TSSB CSV ✓✓✓

## Mode 1: Book/Source Formula

**When to use:**
- ✓ Following book documentation exactly
- ✓ Theoretical research
- ✓ Academic work requiring formula correctness
- ✓ Starting fresh without legacy TSSB data
- ✓ Want normalized sensitivity across different multipliers

**Formula:**
```
short_mean = mean(volume[i-short_length+1 : i])
long_mean = mean(volume[i-long_length+1 : i])
denom = ∛mult  (cube root of multiplier)
raw = log(short_mean / long_mean) / denom
output = 100 × Φ(3 × raw) - 50
```

**Example Usage:**
```cpp
SingleIndicatorRequest req;
req.id = SingleIndicatorId::VolumeMomentum;
req.params[0] = 10;   // short_length
req.params[1] = 5;    // mult
req.params[2] = 1;    // Mode 1: Book formula

auto result = compute_single_indicator(series, req);
```

**Characteristics:**
- Lower sensitivity (dampened by ~1.7× for mult=5)
- Smaller signal variance
- More centered around 0
- Normalized across different multipliers
- Fewer extreme values

**Validation Results:**
- VOL_MOM_S (10, 5): MAE = 8.404 vs TSSB CSV ✗ (expected mismatch)
- VOL_MOM_L (50, 4): MAE = 7.194 vs TSSB CSV ✗ (expected mismatch)

## Numerical Comparison

### Example: Bar 1078, VOL_MOM_S (10, 5)

**Input Data:**
- short_mean = 1104.04
- long_mean = 1483.19
- mult = 5, ∛5 = 1.70998

**Mode 0 Calculation:**
```
raw = log(1104.04 / 1483.19) = -0.295222
scaled = 3.0 × -0.295222 = -0.885666
output = 100 × Φ(-0.885666) - 50 = -31.210
```

**Mode 1 Calculation:**
```
raw = log(1104.04 / 1483.19) / 1.70998 = -0.172647
scaled = 3.0 × -0.172647 = -0.51794
output = 100 × Φ(-0.51794) - 50 = -19.775
```

**Difference:** 11.435 (57.8% of Mode 0 magnitude)

### Statistical Comparison (8,143 bars tested)

| Metric | VOL_MOM_S (10, 5) |  | VOL_MOM_L (50, 4) |  |
|--------|-------------------|---|-------------------|---|
|        | Mode 0 | Mode 1 | Mode 0 | Mode 1 |
| Mean Abs Difference | - | 8.404 | - | 7.194 |
| Max Difference | - | 12.679 | - | 10.987 |
| Typical Difference | - | 50-58% | - | 51-52% |

## Trading Signal Impact

### Threshold-Based System Example

Consider a simple system:
```
IF VOL_MOM > -20: Bullish volume trend
IF VOL_MOM < -30: Bearish volume trend
```

**Bar 1078:**
- Mode 0: -31.210 → **Bearish signal**
- Mode 1: -19.775 → **Bullish signal**
- **OPPOSITE SIGNALS!**

### Implication for Strategy Development

1. **Thresholds must be optimized per mode**
   - Mode 0 optimal threshold: e.g., -25
   - Mode 1 optimal threshold: e.g., -15
   - Cannot use same thresholds for both

2. **Signal frequency differs**
   - Mode 0: More frequent extreme signals
   - Mode 1: More centered, fewer extreme signals

3. **Risk management impact**
   - Different position sizing required
   - Different stop-loss placement
   - Different signal filtering needed

## Model Training Considerations

### If Training New Models

**Recommendation: Use Mode 0**
- More historical data available (TSSB CSV files)
- Can validate implementation easily
- Industry standard for TSSB users

### If You Have Existing Models

**Check which mode your training data used!**

If unsure, test both modes:
```python
# Python pseudo-code
mode0_predictions = model.predict(features_with_volmom_mode0)
mode1_predictions = model.predict(features_with_volmom_mode1)

# Compare accuracy
if mode0_accuracy > mode1_accuracy:
    print("Your model expects Mode 0")
else:
    print("Your model expects Mode 1")
```

### Feature Importance

Mode affects feature importance in ML models:
- Mode 0: Higher variance → Likely higher feature importance
- Mode 1: Lower variance → Likely lower feature importance

If switching modes, **retrain models** from scratch.

## Code Implementation

### In SingleIndicatorLibrary.cpp

```cpp
IndicatorResult compute_volume_momentum(const SeriesSpans& spans,
                                       const SingleIndicatorRequest& request)
{
    const int short_length = std::max(1, static_cast<int>(std::lround(request.params[0])));
    const int mult = std::max(2, static_cast<int>(std::lround(request.params[1])));

    // params[2] controls formula mode:
    //   0 (default) = TSSB executable behavior (no cube root division)
    //   1 = Book/source code formula (with cube root division)
    const bool use_book_formula = (request.params[2] > 0.5);

    // ... compute short_mean and long_mean ...

    double raw = std::log(short_mean / long_mean);

    // Apply cube root division if using book formula
    if (use_book_formula) {
        const double denom = std::exp(std::log(static_cast<double>(mult)) / 3.0);
        raw /= denom;
    }

    result.values[idx] = 100.0 * normal_cdf(3.0 * raw) - 50.0;
}
```

## Testing Both Modes

### Test Program

Use `test_volume_momentum_both_modes` to validate both modes:

```bash
./test_volume_momentum_both_modes btc25_3.txt "BTC25_3 HM.CSV"
```

**Expected Results:**
- Mode 0: MAE = 0.0000 vs CSV ✓✓✓
- Mode 1: MAE ~8.4 and ~7.2 vs CSV ✗ (expected)
- Difference between modes: ~8-12 points

### Unit Test Example

```cpp
// Test Mode 0 (default)
SingleIndicatorRequest req0;
req0.id = SingleIndicatorId::VolumeMomentum;
req0.params[0] = 10;
req0.params[1] = 5;
req0.params[2] = 0;  // or omit
auto result0 = compute_single_indicator(series, req0);

// Test Mode 1
SingleIndicatorRequest req1;
req1.id = SingleIndicatorId::VolumeMomentum;
req1.params[0] = 10;
req1.params[1] = 5;
req1.params[2] = 1;
auto result1 = compute_single_indicator(series, req1);

// Verify Mode 0 matches CSV
ASSERT_LT(std::abs(result0.values[1078] - (-31.210104)), 0.001);

// Verify Mode 1 is different
ASSERT_LT(std::abs(result1.values[1078] - (-19.775)), 0.01);

// Verify modes differ by expected amount
double diff = result0.values[1078] - result1.values[1078];
ASSERT_LT(std::abs(diff - 11.435), 0.01);
```

## Migration Guide

### If Currently Using Mode 0 Implicitly

Your code continues to work unchanged. Mode 0 is the default.

### If You Need Book Formula

Update your code to explicitly set `params[2] = 1`:

**Before:**
```cpp
req.params[0] = 10;
req.params[1] = 5;
```

**After:**
```cpp
req.params[0] = 10;
req.params[1] = 5;
req.params[2] = 1;  // Use book formula
```

### If Switching Modes

**Required actions:**
1. ✓ Update indicator configuration
2. ✓ Re-optimize strategy thresholds
3. ✓ Re-train ML models (if used)
4. ✓ Re-run backtests
5. ✓ Update documentation

## Recommendation

**For most users: Use Mode 0 (default)**

Mode 0 is the practical choice because:
- Matches existing TSSB data
- Enables validation
- Compatible with published research using TSSB
- No need to regenerate historical data

**Use Mode 1 only if:**
- Starting completely fresh
- Need theoretical consistency with book
- Academic research requiring exact formula
- Want normalized behavior across multipliers

## Performance Characteristics

Both modes have identical computational cost:
- Time complexity: O(n × lookback)
- Memory: O(n)
- The cube root division adds negligible overhead

## References

- **Implementation**: `src/SingleIndicatorLibrary.cpp:1087-1153`
- **Test Program**: `tools/test_volume_momentum_both_modes.cpp`
- **Book Reference**: Pages 206-207, Equations 7.25-7.26
- **Source Code**: `Single/comp_var.cpp` lines 1416-1437
- **Discrepancy Analysis**: `VOLUME_MOMENTUM_DISCREPANCY_ANALYSIS.md`

## FAQ

**Q: Which mode should I use?**
A: Mode 0 (default) for compatibility with TSSB. Mode 1 only if you specifically need book formula.

**Q: Can I switch modes mid-stream?**
A: Not recommended. Stick with one mode for consistency. If switching, redo all optimization and testing.

**Q: Why not just implement the "correct" formula?**
A: There is no single "correct" formula - the executable and book disagree. We provide both for maximum flexibility.

**Q: Does this affect other indicators?**
A: No. Only Volume Momentum has this discrepancy. All other indicators match all sources.

**Q: How do I know which mode existing CSV data uses?**
A: TSSB-generated CSV files use Mode 0 (executable behavior). If data source is unknown, test against CSV to determine.

**Q: Will this confuse users?**
A: Mode 0 is default - most users won't notice. Advanced users who read book documentation can explicitly choose Mode 1.

## Summary

| Aspect | Mode 0 (Default) | Mode 1 |
|--------|------------------|--------|
| **Formula** | No cube root division | With cube root division |
| **Matches TSSB CSV** | ✓ Yes | ✗ No |
| **Matches Book** | ✗ No | ✓ Yes |
| **Sensitivity** | Higher | Lower (~1.7× dampened) |
| **Use Case** | Production, compatibility | Research, theory |
| **Default** | ✓ Yes | ✗ No |
| **Recommendation** | ✓ **Recommended** | Special cases only |

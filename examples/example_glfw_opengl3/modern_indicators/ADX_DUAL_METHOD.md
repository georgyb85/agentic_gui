# ADX Dual-Method Implementation

## Overview

The ADX (Average Directional Index) indicator in this library supports **two computation methods**:

1. **Simple Moving Average (SMA)** - Default method (params[1]=0)
2. **Wilder's Exponential Smoothing** - Alternative method (params[1]=1)

## Why Two Methods?

During validation against TSSB CSV output, we discovered that the CSV was generated using the **SMA method**, while the TSSB source code (Single/COMP_VAR.CPP) implements **Wilder's exponential smoothing**.

As noted in the author's ADX documentation (adx.md):
> "There are several computational methods in use today... different implementations of ADX, with somewhat different charts, appear in trading software"

The author also advised in email correspondence to use the book's code for discrepancies, but since the trading system was based on TSSB CSV output, both methods are preserved in this library.

## Validation Results

Testing on BTC25_3 dataset with lookback=14:

| Method | MAE | Max Error | Status |
|--------|-----|-----------|--------|
| **SMA (default)** | 0.000131 | 0.001126 | ✓✓✓ **PERFECT MATCH with TSSB CSV** |
| **Wilder's** | 12.280659 | 55.172941 | Alternative method from book |

## Usage

### Method 0: SMA (Default)
```cpp
SingleIndicatorRequest req;
req.id = SingleIndicatorId::Adx;
req.params[0] = 14;   // lookback
req.params[1] = 0;    // SMA method (or omit - defaults to 0)
```

This method:
- Matches TSSB CSV output
- Uses simple moving averages throughout
- Recommended for systems based on TSSB CSV data

### Method 1: Wilder's Exponential Smoothing
```cpp
SingleIndicatorRequest req;
req.id = SingleIndicatorId::Adx;
req.params[0] = 14;   // lookback
req.params[1] = 1;    // Wilder's method
```

This method:
- Matches the book's ADX description
- Matches COMP_VAR.CPP source code
- Uses complex 3-phase initialization with exponential smoothing
- Recommended for systems based on the author's published algorithms

## Algorithm Differences

### SMA Method (Method 0)
1. Compute DM+, DM-, and TR for each bar
2. Apply SMA(lookback) to DM+, DM-, TR → smoothed values
3. Calculate DI+ and DI- from smoothed values
4. Calculate DX from DI+ and DI-
5. Apply SMA(lookback) to DX → ADX

**front_bad = 2 * lookback - 1**

### Wilder's Method (Method 1)
1. **Phase 1** (bars 1 to lookback): Accumulate DM+, DM-, TR
2. **Phase 2** (bars lookback+1 to 2*lookback-1):
   - Exponentially smooth DM+, DM-, TR with factor (lookback-1)/lookback
   - Accumulate ADX
3. **Phase 3** (bars 2*lookback onwards):
   - Exponentially smooth DM+, DM-, TR
   - Exponentially smooth ADX itself

**Smoothing factor = (lookback - 1) / lookback**
Example: For lookback=14, smoothing = 13/14 ≈ 0.9286

**front_bad = 2 * lookback - 1**

## Implementation Location

File: `src/SingleIndicatorLibrary.cpp`
Function: `compute_adx()`
Lines: ~566-764

## Testing

Run comprehensive tests with:
```bash
# Test default SMA method (should match CSV)
./test_adx btc25_3.txt "BTC25_3 HM.CSV"

# Test both methods side-by-side
./test_adx_both_methods btc25_3.txt "BTC25_3 HM.CSV"
```

## References

- **adx.md** - Author's detailed ADX documentation explaining multiple variants
- **Single/COMP_VAR.CPP** - Original TSSB source (Wilder's method)
- **test_all_adx_variants.cpp** - Research tool that tested 4 different ADX variants

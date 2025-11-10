# Questions for TSSB Author

1. **ATR Ratio Scaling**
   - What exact transformation maps the raw short/long ATR ratio onto the `-50 … 50` range in the legacy engine?
   - Are logarithms applied to ATR values (log true range vs linear ATR) before the transformation?
   - Does the transformation depend on the `HistLength` or `Multiplier` parameters beyond simply defining the two window lengths?

2. **FTI Family (FTI LARGEST FTI, etc.)**
   - Is the published algorithm still the one used by TSSB when producing the `BTC25 HM.csv` output, or were later adjustments made (e.g., use of high/low instead of close, different β/`noise_cut`, additional smoothing, or rescaling)?
   - Are any additional normalisations applied to the FTI values before they are written to the indicator file (for example, a transformation similar to the `normal_cdf` compression used by other indicators)?
   - For `FTI LARGEST FTI`, should the reported value be the raw FTI of the strongest period, or is it combined/averaged with neighbouring periods?
   - Do the other FTI outputs (`FTI MAJOR LOWPASS`, `FTI BEST PERIOD`, `FTI BEST WIDTH`, etc.) use identical preprocessing, or do any of them require different parameters or post-processing?

3. **Wavelet Indicators**
   - Could you provide the precise implementation details (wavelet family, coefficient normalisation, window length, scaling/compression) used for the Daubechies indicators (`DAUB_*`) and Morlet indicators (`REAL/IMAG MORLET`, `REAL DIFF/PROD MORLET`)?
   - Are there reference formulas or code snippets that clarify how the “NL ENERGY”, “CURVE”, “STD”, etc., variants are derived from the base wavelet coefficients?

4. **General Indicator Output**
   - Are there any global normalisation passes (e.g., per-indicator z-scoring across history) applied before the values are exported to CSV?
   - When indicators require logarithms, is the default base `e`, or was a different base ever used?

Any clarifications or pointers (documentation, updated source, change logs) on these points would help us finish porting the outstanding indicators while ensuring parity with TSSB’s published results.

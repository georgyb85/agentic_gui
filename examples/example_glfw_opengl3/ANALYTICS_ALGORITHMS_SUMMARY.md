# Analytics Algorithms Summary

## New HMM Tooling
- `hmm/HmmModel.{h,cpp}` implements a modern Gaussian hidden Markov model (EM forward-backward) with Eigen math.
- `hmm/HmmTargetCorrelation.{h,cpp}` runs the target-correlation sweep with MCPT permutations; UI lives in `HMMTargetWindow.{h,cpp}`.
- `hmm/HmmMemoryTest.{h,cpp}` measures sequential memory via permutation-based log-likelihood testing; exposed through `HMMMemoryWindow.{h,cpp}`.
- `hmm/HmmGpu.{h,cu}` provides CUDA-accelerated EM sweeps (states ≤ 8, features ≤ 6) used when GPU execution is enabled.

## Trade Simulator Enhancements
- `simulation/PerformanceStressTests.{h,cpp}` supplies bootstrap stress testing (Sharpe/profit factor CIs, drawdown quantiles, p-values) shared by the trade simulator.
- `TradeSimulator` now caches performance reports and exposes stress metrics to the UI.
- `TradeSimulationWindow` adds configurable stress-test controls and tabular reports summarising bootstrap confidence bands and risk probabilities.

## Stationarity Diagnostics
- `stationarity/MeanBreakTest.{h,cpp}` provides a SupF-style break-in-mean scan with F-distribution p-values.
- `StationarityWindow.{h,cpp}` wraps the test in an asynchronous Dear ImGui interface.

## Forward Selection Component Analysis
- `fsca/FscaAnalyzer.{h,cpp}` delivers an orthogonal forward-selection component analysis with variance accounting and loadings.
- `FSCAWindow.{h,cpp}` offers a GUI for configuring and visualizing FSCA output (variance table + loadings).

## Time Series Window Integration
- `TimeSeriesWindow` now launches HMM Target, HMM Memory, Stationarity, and FSCA panels directly from the toolbar.
- All analytics windows reuse `FeatureSelectorWidget` and Arrow-backed data sourcing for consistent UX.

## Build Notes
- GPU acceleration for HMMs is optional. By default the project builds CPU-only; define `HMM_WITH_CUDA` and add `hmm/HmmGpu.cu` to your build when the CUDA toolchain is installed.

# Modern Indicator Library – Architecture & Porting Guide

This document explains the structure, responsibilities, and extension points of the modern C++ indicator engine that replaces the legacy `Single` and `Mult` executables. It is intended for developers who need to maintain the current implementation, port additional legacy indicators, or add new families for the first time.

---

## 1. High-Level Goals

1. **Parity** – reproduce the behaviour of the existing TSSB indicator families while detaching from their monolithic, Windows-specific executables.
2. **Modern C++** – use RAII, standard containers, and type-safe interfaces (no raw pointers or manual memory management).
3. **Extensibility** – adding a new indicator is boiled down to writing a standalone function and registering it in the switch statement; no cross-cutting concerns.
4. **Parallel throughput** – enable concurrent evaluation of multiple indicators across the same price history via `std::async` or, later, structured parallel algorithms.
5. **Portability** – buildable with vanilla CMake on any platform that supports C++20.

The top-level repository folder for the new work is `modern_indicators/`.

```
modern_indicators/
├── ARCHITECTURE.md              (this document)
├── CMakeLists.txt               (standalone library build)
├── include/
│   ├── IndicatorEngine.hpp
│   ├── IndicatorId.hpp
│   ├── IndicatorRequest.hpp
│   ├── IndicatorResult.hpp
│   ├── MathUtils.hpp
│   ├── MultiIndicatorLibrary.hpp
│   ├── Series.hpp
│   └── SingleIndicatorLibrary.hpp
└── src/
    ├── IndicatorEngine.cpp
    ├── IndicatorId.cpp
    ├── MathUtils.cpp
    ├── MultiIndicatorLibrary.cpp
    └── SingleIndicatorLibrary.cpp
```

---

## 2. Core Concepts

### 2.1 Series & Requests

* `Series.hpp`
  * `SingleMarketSeries` – holds the open/high/low/close/volume/date vectors for one market. Exposes `size()` helpers.
  * `MultiMarketSeries` – wraps a vector of `SingleMarketSeries` to represent the multi-market scripts.

* `IndicatorRequest.hpp`
  * `IndicatorParameters` – simple wrapper around a fixed-length `std::array<double,4>` used to pass the numeric parameters that trail each script token.
  * `SingleIndicatorRequest` and `MultiIndicatorRequest` – pair an identifier (enum), parameters, and an optional user-provided display name.

* `IndicatorId.hpp`
  * Strong enums for all single- and multi-market families we currently support.
  * `to_string` helpers mirror the script keyword (`"RSI"`, `"CMMA RANK"`, …) and are used for diagnostics and reporting.

* `IndicatorResult.hpp`
  * Returned by the engine. Contains the indicator name, computed values, a success flag, and optional error text. This lets the caller present partial results even if one indicator fails to compute.

### 2.2 Engine & Execution

* `IndicatorEngine.hpp/.cpp`
  * Provides two overloads of `compute`, one for single-market batches and one for multi-market.
  * Accepts a vector of requests and optional `ExecutionOptions` (`parallel` flag).
  * When parallel execution is enabled (default) and there is more than one request, each indicator is evaluated asynchronously via `std::async(std::launch::async, …)`. The interface returns once all futures complete.
  * The synchronous path just loops over the requests – useful for deterministic unit tests or when the indicators mutate shared state (they should not).

### 2.3 Indicator Libraries

* `SingleIndicatorLibrary.hpp/.cpp`
  * Entry point: `compute_single_indicator()` – dispatches on `SingleIndicatorId` and calls the corresponding implementation function.
  * Each indicator gets a pure function `IndicatorResult compute_NAME(const SeriesSpans&, const SingleIndicatorRequest&)`.
      * `SeriesSpans` is a lightweight bundle of `std::span<const double>` pointing into the series vectors; avoids repeated array bounds checks and works well with algorithms.
      * Functions return an `IndicatorResult`. Consumers can inspect `success` and `error_message` to react to not-yet-implemented indicators.
  * Ported indicators:
      * `RSI`
      * `Detrended RSI`
      * `Stochastic` (raw/K/D behaviour with smoothing matches the legacy branch)
  * All other functions currently return `make_not_implemented(…)` but the skeleton is ready to accept full ports.

* `MultiIndicatorLibrary.hpp/.cpp`
  * Mirrors the structure of the single-market library. At present, the entry point returns “not yet ported” for every `MultiIndicatorId`. This is where the JANUS, CMMA, trend rank, etc. families will live once migrated.

---

## 3. Supporting Utilities

* `MathUtils.hpp/.cpp`
  * Houses numerical routines that were previously scattered throughout `STATS.CPP`, `LEGENDRE.CPP`, etc.
  * Includes:
      * Distribution functions (`normal_cdf`, `inverse_normal_cdf`, `igamma`, `F_CDF`).
      * ATR and variance helpers (exposed via `std::span` for cache-friendly loops).
      * `legendre_linear` – reproduces the generation of Legendre polynomial coefficients for trend/deviation indicators.
  * Functions are `constexpr` where possible and avoid recursion or heap allocation.

---

## 4. Execution Flow

1. User builds a batch of `SingleIndicatorRequest` objects (or multi-market equivalents).
2. Pass the requests and the price history into `IndicatorEngine::compute`.
3. The engine schedules each request (parallel by default) and stores the results in order.
4. Each indicator’s implementation:
     * Validates its parameters (`lookback` ranges, etc.).
     * Allocates or reuses scratch vectors on the stack (`std::vector<double>`).
     * Mirrors the legacy algorithm’s steps (accumulating sums, smoothing, etc.).
     * Fills `IndicatorResult::values` with exactly `series.size()` elements. Undefined regions should be set to legacy defaults (e.g., 50 for RSI).
5. Results are returned to the caller; errors are annotated per-indicator without aborting the batch.

---

## 5. Porting Checklist

Use the following steps when porting a legacy indicator into the modern library.

1. **Find the legacy block**  
   * Single-market indicators are in `Single/COMP_VAR.CPP`.  
   * Multi-market indicators are in `Mult/COMP_VAR.CPP`, `Mult/TREND_CMMA.CPP`, and `Mult/JANUS.CPP`.

2. **Identify parameters & invariants**  
   * Look at the `get_?params` call in `Single/SINGLE.CPP` or `Mult/MULT.CPP` to see the expected bounds (min/max, integer requirements).
   * Translate them to validation checks at the top of the modern function.

3. **Copy numerical core**  
   * Reuse helper functions from `MathUtils` where available (ATR, variance, Legendre, etc.).
   * Replace raw arrays with `std::vector<double>`, `std::span`, or stack variables. Avoid `new`/`delete`.  
   * Ensure loops use `std::size_t` / `int` consistently to avoid signed/unsigned bugs.

4. **Populate the result**  
   * Initialise the output vector with the same default values as the legacy branch (typically 0 or 50).
   * Compute the indicator for valid indices. Be mindful of `front_bad` / `back_bad` values in the original code; they indicate how many leading values should be neutralised.

5. **Return success**  
   * If everything succeeds, leave `IndicatorResult.success = true`.
   * If the legacy code would have returned an error (e.g., FTI parameter mismatch), set `success = false` and fill `error_message` accordingly.

6. **Update dispatcher**  
   * Remove the `make_not_implemented` placeholder in the switch branch for that indicator.

7. **Regression**  
   * Compare the new outputs against known-good values from the original executable. Adding unit tests is encouraged but depends on data availability.

---

## 6. Extending the Library

### 6.1 Adding a New Single-Market Indicator

1. Add a new entry to `SingleIndicatorId`.
2. Add the string mapping in `IndicatorId.cpp`.
3. Implement `compute_new_indicator(...)` in `SingleIndicatorLibrary.cpp`.
4. Add a case in the `switch` inside `compute_single_indicator`.
5. Update any higher-level orchestration to expose the new indicator to the UI/API.

### 6.2 Adding a Multi-Market Indicator

The process mirrors the single-market case, but the computations operate over `MultiMarketSeries`. Most multi-market indicators rely on helper classes (e.g., the JANUS class). When porting:

* Translate helper classes (`JANUS`, `Entropy`, `MutInf`, `FTI`) into modern RAII types that live in a `.cpp` file and expose C++20-friendly interfaces (no manual `MALLOC` / `FREE`).
* Ensure cross-market operations are vectorised where possible; follow the layout of the legacy algorithms for correctness first, then optimise.

### 6.3 Parallel Fine-Tuning

The current implementation uses `std::async` for coarse-grained parallelism (per indicator). For heavy workloads you can:

* Replace it with a thread pool (e.g., `std::jthread` + `std::latch` or a third-party pool).
* Adopt C++23’s execution policies when available.
* Guard shared state if indicator implementations use global caches. At present all functions should be stateless beyond their arguments.

---

## 7. Future Enhancements

1. **Complete indicator parity** – port every single entry from the `Single` and `Mult` executables. Pay special attention to the JANUS and FTI families.
2. **Wavelet & FTI modules** – a faithful port of the DAUB and Morlet indicators requires careful translation of the transform logic. Consider wrapping them in dedicated classes under `src/wavelets/` to keep `SingleIndicatorLibrary.cpp` readable.
3. **Testing infrastructure** – build a suite that compares indicator outputs versus recorded TSSB outputs for known datasets. Include stress tests for error-handling cases.
4. **API layer** – expose the engine via a REST service or Python binding to support the forthcoming web UI. The architecture here is modular enough to wrap without changes.
5. **Move to `std::mdspan` or ranges** – as compilers catch up with newer standards, refactor hot loops to use modern views for better readability and potential SIMD benefits.

---

## 8. Build & Integration

The new module builds as a static (or shared) library via `modern_indicators/CMakeLists.txt`. Example usage:

```bash
cd modern_indicators
cmake -S . -B build
cmake --build build
```

Link `tssb_modern_indicators` into your application and include the headers from `modern_indicators/include`.

---

## 9. Quick Reference

| Component                              | Responsibility                                             |
|----------------------------------------|------------------------------------------------------------|
| `IndicatorEngine`                      | Batch execution, parallel scheduling                       |
| `SingleIndicatorLibrary`               | Single-market indicator implementations                    |
| `MultiIndicatorLibrary`                | Multi-market indicator implementations                     |
| `MathUtils`                            | Numerical helpers shared across indicators                 |
| `IndicatorRequest` / `IndicatorResult` | Typed interfaces for inputs and outputs                    |
| `IndicatorId`                          | Enum catalog and script-token mapping                      |
| `Series`                               | Data containers for price histories                        |
| `helpers::EntropyCalculator` / `helpers::MutualInformationCalculator` | Information-theory utilities with vector-backed storage |
| `helpers::FtiFilter`                   | Follow-Through Index implementation without raw pointers   |
| `helpers::JanusCalculator`             | JANUS toolkit rebuilt around `std::vector`/`std::span`     |

---

## 10. Open Tasks (as of this snapshot)

* Port remaining single-market indicators (MA Difference, MACD, PPO, etc.).
* Port all multi-market families (trend ranks, CMMA, JANUS suite).
* Validate the migrated helper classes (`Entropy`, `MutInf`, `FTI`, `JANUS`) against historical datasets.
* Build automated regression tests against historical TSSB outputs.

Once these are complete, the modern library will be a drop-in replacement for the legacy executables, ready to support the upcoming web interface and any future indicator families.

# Indicator Builder Implementation Plan

This document breaks the Indicator Builder feature into concrete work items so we can introduce the window without disrupting the existing Time Series workflow.

## Goals
- Provide a dedicated Dear ImGui window where users can paste a TSSB-style script (like `var.txt`), validate it, and build indicators with the `modern_indicators` library.
- Reuse the OHLCV data already loaded in the Candlestick/OHLCV window.
- Display the generated indicators with the same table + plot affordances available in `TimeSeriesWindow`, including column selection for plotting.

## Subtasks
1. **Library Integration**
   - Add `modern_indicators` headers and sources to the desktop build (Makefile + `.vcxproj`).
   - Move the Linux make build from `-std=c++11` to `-std=c++17` to satisfy `std::optional`/`string_view` usage in the library.
   - Verify Arrow/Chronosflow headers continue to compile under the updated standard.

2. **Script Parsing & Validation**
   - Wrap `tssb::IndicatorConfigParser::parse_line` to handle multi-line text pasted in the UI (respect comments/blank lines).
   - Surface validation errors with precise line numbers and store a structured list of `IndicatorDefinition` entries for later execution.
   - Keep a canonical snapshot of the text so we can detect if the script changed while a build is running.

3. **OHLCV Data Bridge**
   - Add helper(s) that read the currently loaded OHLCV data from `CandlestickChart` (raw `OHLCVData` vectors) and convert them into a `tssb::SingleMarketSeries`.
   - Detect missing/empty OHLCV data and gate the “Build Indicators” button with a descriptive warning.
   - Preserve the timestamp vector (seconds -> `double`) so plots align with the rest of the app.

4. **Computation Engine**
   - Introduce an async job (`std::future`) that invokes `tssb::BatchIndicatorComputer::compute_from_series`.
   - Track job status (`Idle`, `Parsing`, `Computing`, `Completed`, `Error`) plus timing metrics for the status bar.
   - Convert `tssb::TaskResult` output into column-major buffers and keep metadata (min/max, null counts) ready for plotting.

5. **Result Table & Plot View**
   - Reuse the column header/table caching approach from `TimeSeriesWindow` (date/time/timestamp columns + indicator columns, capped at ~250 rows for performance).
   - Mirror the plot controls (auto-fit, ImPlot time axis formatting, cached vectors) so interaction is consistent with the existing window.
   - Highlight the selected column in the table and update the cached plot vectors when selection changes.

6. **IndicatorBuilderWindow UI**
   - New `IndicatorBuilderWindow` class with:
     - visibility toggle, menu entry, and docking support;
     - multi-line text area for scripts, “Validate Script” and “Build Indicators” buttons, and live statistics (# indicators, parameter count);
     - optional save/load script helpers (stretch goal, default to clipboard-friendly workflow).
   - Persist the latest successful definitions so the user can tweak scripts and rebuild quickly.

7. **App Integration & Documentation**
   - Instantiate the window in `main.cpp`, add it to the “Windows” menu, and wire it to the Candlestick/OHLCV data source.
   - Document the workflow in `SerializationPersistence_StatusReport.md` (or a new README section) so teammates know how to use the builder and what commands to run.
   - Outline manual verification steps (load OHLCV → paste script → build → inspect table/plot) to accompany PR/test notes.

Following these steps keeps the feature scoped and ensures we can extend it later (e.g., saving indicator scripts to Stage1 or exporting results to QuestDB/Postgres).

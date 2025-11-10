# Implementation Plan – Unified Serialization & Persistence (Stage 1)

This document refines the requested changes into actionable work items that can be implemented incrementally. The plan is split into three tracks matching the user’s notes: (1) shared QuestDB/ChronosFlow adapter, (2) unified parameter serialization for copy/paste + future Postgres persistence, (3) end‑to‑end dataset/run/simulation linking.

---

## 1. Shared QuestDB ↔ ChronosFlow Adapter

### Goals
- One class/module handles both “export” and “load” of Arrow/ChronosFlow data frames via QuestDB (ILP + REST).
- TimeSeries window, walkforward exporter, and any future components reuse the same API.

### Tasks
1. **Design interface**
   - Class `QuestDbDataFrameGateway`
     - `bool Export(const chronosflow::AnalyticsDataFrame&, const ExportSpec&, std::string* err);`
     - `arrow::Result<chronosflow::AnalyticsDataFrame> Import(const ImportSpec&);`
     - `ExportSpec` includes measurement name, tags, timestamp column, stride size, etc.
   - Accept environment overrides for host/port similar to current helpers.

2. **Extract existing logic**
   - Move `TimeSeriesWindow::BuildQuestDBLineProtocol` / CURL send logic into the gateway.
   - Move QuestDB REST fetch logic (currently only in scripts) into `Import`.
   - Gateway converts between Arrow tables and ChronosFlow, so call sites just pass frames.

3. **Refactor TimeSeries window**
   - Replace direct ILP/REST code with gateway calls.
   - Keep UI/validation identical; only backend moves into gateway.

4. **Prepare for reuse elsewhere**
   - Document how walkforward/trade exporters will use the same gateway when they need to push entire series (predictions, trade traces) rather than hand-crafted ILP strings.

### Deliverables
- `QuestDbDataFrameGateway.{h,cpp}`
- Updated TimeSeries window
- README section describing the new API

---

## 2. Unified Parameter Serialization (Clipboard + Postgres-ready)

### Goals
- Single serializer/deserializer struct handles features, hyperparameters, and full run configs for both Simulation and Trade windows.
- Add missing “Paste Parameters” button in Trade Simulation window.
- Future-ready: serialized text can be stored in Postgres (as JSON/text) and later rehydrated via the same parser.

### Tasks
1. **Create serialization utility**
   - `RunConfigSerializer` with `ToText(const RunConfig&)` and `FromText(const std::string&, RunConfig*)`.
   - Support sections (features, hyperparameters, walkforward config, trade config) with explicit headers to make parsing deterministic.

2. **Refactor existing copy buttons**
   - Simulation window “Copy Features/Hyperparameters/All” -> use `RunConfigSerializer`.
   - Maintain backwards compatibility with the current text format by accepting legacy tags if feasible.

3. **Implement Paste buttons**
   - Simulation window already has paste actions: rewire to the new parser.
   - Trade Simulation window: add “Paste Parameters” next to “Copy Parameters”, parse text, and populate `TradeSimulator::Config`.

4. **Unit tests / sample fixtures**
   - Add a small test (or at least sample strings) under `docs/fixtures` showing expected input/output so future stages can store the same payload in Postgres.

### Deliverables
- `RunConfigSerializer.{h,cpp}` (or namespace)
- Updated Simulation + Trade windows with new copy/paste UI
- Documentation snippet describing the text format

---

## 3. End-to-End Dataset → Run → Simulation Persistence

### Goals
- Formalize the relationship `(OHLCV, indicators) → multiple walkforward runs → multiple trade simulations`.
- Ensure timeseries live in QuestDB, metadata in Postgres, and each record knows how to find its dependents.
- Enable selecting a dataset/run/simulation within the UI and round-tripping to/from storage.

### Tasks
1. **Dataset-level metadata loader**
   - Add a panel or dialog to TimeSeries / OHLCV window to list datasets retrieved from Postgres (using `indicator_datasets`).
   - When the user selects “Load dataset 2025 BTC”, fetch OHLCV + indicators via the new QuestDB gateway and initialize ChronosFlow frames automatically.

2. **Persisted walkforward runs**
   - On “Save Run” (new button), prompt with editable measurement name and “Save to DB” checkbox.
   - Use `Stage1MetadataWriter` (respecting new mode setting) to store metadata.
   - Use `QuestDbDataFrameGateway` to push the per-bar prediction frame.

3. **Load existing runs**
   - New control (e.g., combo box) in Simulation window listing runs pulled from Postgres for the active dataset.
   - When selected, fetch predictions/thresholds from QuestDB, rebuild `SimulationRun`, and show it in the UI without rerunning the model.

4. **Trade simulation saves/loads**
   - Mirror the above workflow: editable namespace, “Save to DB/script” toggle, data pumped via gateway + metadata writer.
   - Loading a simulation should populate trade tables, config settings, and performance metrics directly from storage.

5. **Association enforcement**
   - Update Stage1 docs to state the required measurement/tag naming (`dataset_slug`, `run_measurement`, `sim_namespace`).
   - Ensure metadata rows record the source dataset/run IDs so queries like “show all simulations derived from dataset X” are trivial.

### Deliverables
- UI changes in TimeSeries, Simulation, and Trade Simulation windows.
- Updated Stage1 docs (schema diagram, workflow description).
- Optional helper CLI (`scripts/stage1_run_loader.py`) for debugging storage entries outside the GUI.

---

## Execution Notes

- The three tracks can ship independently; however, Track 1 (QuestDB gateway) should land before Tracks 2/3 start consuming the shared logic.
- For Track 2, add thorough logging when parsing fails so clipboard/imported strings are debuggable.
- Track 3 will require new Postgres read paths. Start with read-only helpers (e.g., `Stage1MetadataReader`) so UI code doesn’t have to embed SQL.
- Continue writing to `docs/fixtures/stage1_3/pending_postgres_inserts.sql` as a safety net even after direct writes succeed.

This plan keeps the current UX intact while carving out reusable infrastructure for persistence and future Postgres integration.

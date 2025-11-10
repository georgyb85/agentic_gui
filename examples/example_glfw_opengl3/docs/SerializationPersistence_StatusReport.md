# Serialization & Persistence Status Report (Stage 1)

This report reviews the three workstreams outlined in `docs/ImplementationPlan_SerializationAndPersistence.md` and the deployment pre-work captured under `docs/fixtures/stage1_3/`. It documents what is implemented today, highlights the remaining gaps, and lays out the concrete next steps required to finish Stage 1.

---

## 1. Shared QuestDB ↔ ChronosFlow Adapter

### Current Progress
- `QuestDbDataFrameGateway.{h,cpp}` already exists and wraps the ChronosFlow `AnalyticsDataFrame` export/import flow (QuestDbDataFrameGateway.h:14-37, QuestDbDataFrameGateway.cpp:124-312).
- The Time Series window delegates both “Export to QuestDB” and “Load from QuestDB” to this gateway, and successfully records dataset metadata via `Stage1MetadataWriter::RecordDatasetExport` (TimeSeriesWindow.cpp:776-845).
- Stage 1 fixture docs (`docs/fixtures/stage1_3/README.md`, `DEPLOY_TASKS.md`) reflect the manual steps that were run on the frontend node, so connectivity, schema, and ILP access are already proven.

### Gaps
- The gateway API only takes a measurement name; it lacks the configurable `ExportSpec`/`ImportSpec` (tags, timestamp column, chunk sizing, retries) that the plan calls for, making reuse outside the Time Series window brittle.
- Walkforward prediction exports (`QuestDbExports.cpp:145-213`) and trade trace exports (`QuestDbExports.cpp:214-347`) still go through their own ILP builders and are not ChronosFlow-aware.
- There is no abstraction that turns simulation data (predictions, trades, indicators) into ChronosFlow/Arrow tables, so future components cannot yet call the gateway.
- No documentation describes how other modules should consume the gateway; the README and the existing `*_SUMMARY.md` notes still talk about bespoke ILP emitters.

### Action Plan
1. **Design the public specs** – Introduce `ExportSpec`/`ImportSpec` structs with measurement, tag map, timestamp column, optional string/long columns, batch size, and retry policy plus a `GatewayConfig` that captures env overrides. Update `DataFrameGateway::Export/Import` signatures to accept the specs.
2. **Extract the ILP builder into reusable helpers** – Move the scalar-to-string logic that currently lives inside `DataFrameGateway::Export` into a private `BuildIlpPayload(const arrow::Table&, const ExportSpec&)` helper while retaining the ChronosFlow table acquisition.
3. **ChronosFlow builders for predictions/trades** – Add adapters (e.g., `chronosflow::FromSimulationRun(const SimulationRun&, const ExportSpec&)`) that map walkforward/trade vectors into `AnalyticsDataFrame` objects so the gateway can be reused verbatim.
4. **Retire `QuestDbExports`** – Rewrite the two legacy exporters to call the new gateway and remove the duplicate CURL/ILP code path once parity is verified by exporting a known walkforward run plus trade simulation and checking the QuestDB tables from `docs/fixtures/stage1_3/questdb_checks.sql`.
5. **Documentation + samples** – Update `OHLCV_TRADE_SIMULATION_ARCHITECTURE.md` (or another `_SUMMARY.md`) with the new adapter contract and add a short sample snippet that shows how to push/pull datasets, runs, and simulations via the shared class.

---

## 2. Unified Parameter Serialization (Clipboard + Postgres-ready)

### Current Progress
- Simulation results already provide three clipboard buttons, but each one builds free-form text inline (e.g., `simulation/ui/SimulationResultsWidget_v2.cpp:900-1045`).
- The trade simulator window only exposes a “Copy Parameters” button and formats another ad-hoc block (TradeSimulationWindow.cpp:260-276, 136-161).
- `UniversalConfigWidget` keeps an internal `CopiedConfiguration` struct that allows in-app pasting, yet it is decoupled from the textual clipboard payload and does not survive process restarts.

### Gaps
- There is no `RunConfigSerializer`, so every copy button emits a slightly different format; there is also no deserializer that can accept clipboard text coming from another machine/editor.
- Trade simulation lacks a “Paste Parameters” entry, preventing the user from restoring configurations that were previously copied to plain text.
- None of the serialized payloads are persisted in Postgres; Stage1 metadata only stores JSON blobs for hyperparameters and walk configs, not the canonical clipboard text.
- No fixtures or tests define the expected format, which makes future compatibility changes risky.

### Action Plan
1. **Define the serializer schema** – Create `RunConfigSerializer` that can emit and parse a structured text format with explicit section headers (e.g., `[[FEATURES]]`, `[[HYPERPARAMETERS]]`, `[[WALKFORWARD]]`, `[[TRADE]]`). Support versioning and legacy markers (e.g., `# Model:`) so existing clipboard dumps remain readable.
2. **Implement parsing/formatting** – Add `ToText(const RunConfigSnapshot&)`/`FromText(const std::string&, RunConfigSnapshot*)`. A `RunConfigSnapshot` should include dataset slug, feature list, target, walk-forward config, selected hyperparameters, and optional trade simulator config.
3. **Wire up Simulation UI** – Replace the inline clipboard writers in `SimulationResultsWidget_v2` and `UniversalConfigWidget` with calls to the serializer; have the paste buttons pull from the OS clipboard, validate via `FromText`, and populate `CopiedConfiguration`.
4. **Extend Trade Simulation UI** – Add a “Paste Parameters” button to `TradeSimulationWindow::DrawConfiguration`, parse clipboard payloads via `RunConfigSerializer`, and show validation errors inline.
5. **Persist snapshots** – Store the serializer output in new Postgres columns (see the database task list) whenever `Stage1MetadataWriter::RecordWalkforwardRun` and `RecordSimulationRun` are invoked; also append the blobs to `pending_postgres_inserts.sql` for offline replay.
6. **Fixtures/tests** – Drop two reference payloads under `docs/fixtures/serialization/` and add a lightweight unit test (e.g., `RunConfigSerializer_test.cpp`) that round-trips them to guard against regressions.

---

## 3. Dataset → Walkforward Run → Trade Simulation Linking

### Current Progress
- Dataset exports already register metadata (`indicator_datasets`) when a QuestDB export completes (TimeSeriesWindow.cpp:829-841).
- Simulation saves persist metadata plus trade buckets/trades (TradeSimulationWindow.cpp:954-1043) and push trade traces to QuestDB via `questdb::ExportTradingSimulation`.
- Stage1 fixture docs provide the operational steps (`docs/fixtures/stage1_3/DEPLOY_TASKS.md`) and sample inserts.
- Stage1RestClient now powers `Stage1DatasetManager`, `Stage1MetadataReader`, `Stage1MetadataWriter`, and the Stage1 Server Debugger window, so dataset/run listings plus QuestDB diagnostics all go through the Drogon API at `https://agenticresearch.info` (Stage1DatasetManager.cpp, Stage1MetadataReader.cpp, stage1_metadata_writer.cpp, Stage1ServerWindow.cpp).

### Gaps
- Walkforward persistence now hits the API, but nothing enforces that a dataset is selected/exported before training. That allows users to start a simulation without a valid `dataset_id`, which still triggers FK errors on save and leaves QuestDB namespaces orphaned.
- Stage1DatasetManager and the refreshed Stage1ServerWindow surface datasets/runs, yet the core OHLCV + Time Series windows still require manual CSV loads; there is no “load dataset from Stage1” flow that hydrates both OHLCV and indicator frames together.
- Active dataset context is not propagated into Trade Simulation, so downstream panels cannot reliably discover the OHLCV/indicator pair that produced a run, nor can they auto-populate buy/hold baselines when a run is reloaded from Stage1.
- No linkage exists between OHLCV datasets and indicator datasets inside the UI, even though the workflow assumes they are paired.
- Documentation (`*_SUMMARY.md`) does not explain the hierarchy or naming conventions, creating ambiguity when multiple users export overlapping assets.

### Action Plan
1. **Dataset chooser** – Surface the Stage1 dataset catalog directly inside the OHLCV/Time Series windows (reuse `Stage1MetadataReader` + `Stage1DatasetManager` logic) so loading a dataset hydrates both ChronosFlow frames, records the selected `dataset_id`, and shares it with Simulation/Trade windows.
2. **Walkforward persistence guardrails** – Gate `SimulationWindow` actions on that dataset context, ensure `Stage1MetadataWriter::RecordWalkforwardRun` always receives a valid slug/measurement pair, and verify `questdb::ExportWalkforwardPredictions` succeeds before claiming the run is saved.
3. **Run/Simulation browser** – Keep the new Stage1 run loader modal, but extend it to push the loaded dataset context into Trade Simulation, and add a sister browser there that lists saved simulations plus their QuestDB namespaces.
4. **Naming + docs** – Document the deterministic slugging rules (dataset_slug → run measurement → simulation namespace) in `OHLCV_TRADE_SIMULATION_ARCHITECTURE.md` and update any relevant `_SUMMARY.md` files.
5. **Telemetry & logging** – Add structured logs around Postgres/QuestDB read failures and sync them with `docs/fixtures/stage1_3/validation_logs/` for easier troubleshooting once the round-trip workflow is live.

---

## Cross-cutting Next Steps
1. **Finish the Stage1 metadata integration** – Keep `RecordWalkforwardRun`/`RecordSimulationRun` wired to the HTTP writer, guard them with dataset context checks, and continue mirroring JSON payloads to the spool file for offline replay as spelled out in `docs/fixtures/stage1_3/README.md`.
2. **Consolidate environment handling** – Document both the QuestDB settings (`STAGE1_QUESTDB_HOST`, `QDB_ILP_HOST`, etc.) and the new Stage1 API knobs (`STAGE1_API_BASE_URL`, `STAGE1_API_TOKEN`) in `COMPILATION_FIXES.md` (or a new onboarding note) so contributors know which variables drive the REST client.
3. **Testing workflow** – After each major refactor (gateway reuse, serializer adoption, dataset loading), run `make`, launch `./example_glfw_opengl3`, and walk through: dataset load → run load or training → copy/paste → trade sim save/load, capturing screenshots for the PR template.
4. **Docs housekeeping** – Once new flows land, update the relevant `_SUMMARY.md` artifacts listed in the repo guidelines so future contributors can trace the persistence story quickly.

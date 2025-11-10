# Laptop Stage 1 TODO (Steps 1.1–1.3)

## Current Snapshot
- Repo holds the Dear ImGui app, Drogon backend sources, and trading-dashboard reference copy. Actual deployments will pull the appropriate subdirectories from here.
- Stage 1.1 documentation refreshed (`ONLINE_TRADING_SYSTEM_PLAN.md`, `STAGE1_ALIGNMENT_RESULTS.md`) reflecting laptop/frontend/backend roles.

## Stage 1.1 – Architecture & Alignment
- [x] Confirm and document end-to-end dataflow (ChronosFlow → QuestDB/Postgres → Drogon → trading-dashboard).
- [ ] Capture credential placeholders and network notes for QuestDB/Postgres access in `backend/DEPLOY_INSTRUCTIONS.md`.
- [ ] Record per-dataset provenance (source CSV, preprocessing steps) in `docs/fixtures/stage1_3/README.md` once live exports occur.

## Stage 1.2 – Schema & Data Pipeline Hardening
- [x] Author reference Postgres schema (`docs/fixtures/stage1_3/postgres_schema.sql`) and ILP samples for QuestDB.
- [x] Update Dear ImGui exporter code to capture QuestDB measurement names + column schemas when `btc25.txt`/indicator CSVs are loaded.
- [x] Implement automatic Postgres inserts for `indicator_datasets` whenever an export to QuestDB succeeds (dataset metadata, measurement name, schema snapshot).
- [ ] Perform real exports from Dear ImGui into QuestDB on `45.85.147.236`; update the manifest in `docs/GUI_EXPORT_WORKFLOW.md` with actual row counts returned by QuestDB queries.
- [ ] Run validation queries defined in `docs/fixtures/stage1_3/questdb_checks.sql` and capture outputs under `docs/fixtures/stage1_3/validation_logs/` (new directory).
- [ ] Produce Postgres seed CSVs via `psql \copy` from the live database (replace sample values in `docs/fixtures/stage1_3/postgres_samples.csv` with real dumps).

## Stage 1.3 – Backend Data Access Layer Support
- [x] Package fixtures (CSV slices, ILP lines, curl probes) in `docs/fixtures/stage1_3/` for backend/frontend teams.
- [x] Add automatic Postgres persistence for walkforward completion (insert into `walkforward_runs` + `walkforward_folds` with prediction measurement name) and capture logs for failures.
- [x] Implement “Save Simulation” workflow: trigger Postgres inserts into `simulation_runs`, `simulation_trades`, and `simulation_trade_buckets`, and optionally export trades to QuestDB when requested.
- [x] Provide scripted CLI or batch exports so future datasets can be regenerated without manual UI steps (extend `docs/GUI_EXPORT_WORKFLOW.md`).
- [ ] Maintain `docs/fixtures/stage1_3/KNOWN_ISSUES.md` with real anomalies after exports, ensuring backend agents know about data edge cases.

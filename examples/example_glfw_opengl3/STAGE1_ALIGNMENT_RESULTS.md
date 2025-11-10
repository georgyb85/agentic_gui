# Stage 1.1–1.2 Alignment Results

## Context Snapshot
- **Environment:** Laptop dev workspace (this repo), frontend server `45.85.147.236` (QuestDB + Postgres install + trading-dashboard), backend GPU node `39.114.73.97` (CUDA + Drogon services, ephemeral).
- **Data entry point:** ChronosFlow/TSSB loaders convert local CSVs into Arrow tables and add UTC epoch seconds via `chronosflow::AnalyticsDataFrame::with_unix_timestamp` (`analytics_dataframe.cpp:360-441`).
- **QuestDB export path:** `TimeSeriesWindow::ExportToQuestDB` (`TimeSeriesWindow.cpp:655-712`) batches Arrow rows into ILP payloads and streams them to `telnet://45.85.147.236:9009`.

## Stage 1.1 – Architecture & Dataflow Alignment

### End-to-End Historical Workflow
1. **Desktop ingest:** User loads indicator CSV in the Dear ImGui Time Series window; Arrow schema and TSSB metadata are captured (`TimeSeriesWindow.cpp:173-220`).
2. **Timestamp normalization:** `with_unix_timestamp` casts date/time columns to `int64` and uses `timegm` to produce UTC seconds, ensuring timezone-agnostic alignment (`analytics_dataframe.cpp:360-429`).
3. **Manual export:** User triggers `Export to QuestDB`, selects a measurement name (e.g., `btc25_1`), and the exporter serializes each row into ILP, skipping invalid/NaN values and writing in UTC nanoseconds (`TimeSeriesWindow.cpp:655-719`).
4. **QuestDB storage:** Remote QuestDB (frontend node) creates/extends the measurement immediately, inferring column types from the payload. Column names/indicator set are determined by the loaded CSV (lookback rows appear only in QuestDB, not the original CSV header).
5. **Metadata capture (Stage 1.2 target):** Once walkforward/trading runs complete on the laptop, the Dear ImGui app must automatically persist run configuration, hyperparameters, and measurement names to Postgres (`45.85.147.236`).
6. **Backend/Frontend consumption:** Drogon controllers will read QuestDB/Postgres over LAN, expose JSON endpoints, and the trading-dashboard UI will replace stubbed data with live payloads.

### Dynamic Measurement Naming & Timezone Conventions
- All TSSB timestamps are interpreted as wall-clock times in **UTC** once exported (`timegm` call at `analytics_dataframe.cpp:423-428`).
- Indicator datasets must expose numeric `Date` and `Time` columns (HHMM or HHMMSS); exporter infers format and stores both raw integers and Unix seconds for back-reference.
- Measurement naming pattern:
  - Indicators/primary dataset: `<symbol><year>_<slice>` (e.g., `btc25_1`).
  - Walkforward runs: `<symbol><year>_run<index>` (QuestDB measurement storing predictions/thresholds).
  - Trading simulations: `<symbol><year>_sim<index>` (optional QuestDB measurement for per-bar equity/trades).
- Postgres stores the association between these measurement names and their metadata so backend/frontends can resolve them at runtime.

### Frontend Stub Inventory → Target Endpoints

| Area | File | Current data source | Target backend endpoint | Notes |
| --- | --- | --- | --- | --- |
| Walkforward dashboard | `trading-dashboard/src/apps/walkforward/index.tsx:13-151` | Randomized `generateMock*` helpers | `GET /api/walkforward/runs`, `GET /api/walkforward/runs/{id}` | Replace run list, charts, and fold tables with QuestDB/Postgres-backed data. |
| Trade simulator table | `trading-dashboard/src/apps/tradesim/components/TradeTable.tsx:3-57` | Hard-coded `mockTrades` array | `GET /api/tradesim/runs/{id}/trades` | Structure mirrors QuestDB `trading_sim_traces` rows enriched with Postgres metadata. |
| Local Feature Selection (LFS) | `trading-dashboard/src/apps/lfs/index.tsx:19-138` | Static `mockFeatures`/`mockTargets` arrays | `GET /api/datasets/{dataset}/features` and `GET /api/datasets/{dataset}/targets` | Endpoint will read QuestDB schema metadata; analysis trigger posts to Drogon. |

### Environment Matrix

| Layer | Host | Role | Notes |
| --- | --- | --- | --- |
| Laptop (repo) | local | Data prep, manual exports, UI prototyping | May lack backend/frontend runtime deps; used for CLI/export tooling. |
| Frontend server | `45.85.147.236` | Hosts QuestDB (ILP 9009, REST 9000), Postgres service (5432), and the deployed trading-dashboard | Postgres cluster exists but Stage 1 DB/user/tables still need to be created here; allow backend ingress for DB access. |
| Backend GPU node | `39.114.73.97` (vast.ai) | Runs Drogon API + CUDA/XGBoost workers | Ephemeral host; **no local databases**. Connects to QuestDB/Postgres on `45.85.147.236` using provisioned credentials. |

## Stage 1.2 – Schema & Pipeline Hardening

### QuestDB Measurements (ILP)

| Measurement | Tags (`SYMBOL`) | Core fields | Purpose |
| --- | --- | --- | --- |
| `indicator_bars` | `dataset`, `source`, `granularity` | `date_i` (`LONG`), `time_i` (`LONG`), `timestamp_unix` (`LONG`), one field per indicator/target column (`DOUBLE`/`LONG`) | Stores normalized indicator datasets exported from ChronosFlow. |
| `walkforward_predictions` | `run_id`, `fold`, `dataset`, `target` | `bar_index` (`LONG`), `prediction` (`DOUBLE`), `target_value` (`DOUBLE`), `long_threshold` (`DOUBLE`), `short_threshold` (`DOUBLE`), optional `score` (`DOUBLE`) | Keeps per-bar model outputs with fold context; timestamps mirror indicator bars. |
| `trading_sim_traces` | `simulation_id`, `run_id`, `mode` (`long`/`short`/`dual`) | `entry_price`, `exit_price`, `position_size`, `pnl`, `return_pct`, `cumul_return_pct`, `entry_signal`, `exit_signal` (`DOUBLE`), `exit_timestamp_unix` (`LONG`) | Captures executed trades; QuestDB timestamp records entry time. A companion `trading_sim_equity` measurement can hold bar-by-bar equity curves if needed. |

*Example ILP line (indicator export):*
```
indicator_bars,dataset=btc_4h_v1,source=chronosflow,granularity=1h date_i=20240501i,time_i=140000i,timestamp_unix=1714572000i,macd=1.234,signal=0.987,target_close=0.42 1714572000000000000
```

### Postgres Schema (new; to be created on frontend server)

| Table | Key columns | Description |
| --- | --- | --- |
| `indicator_datasets` | `dataset_id` (UUID PK), `symbol`, `granularity`, `source`, `questdb_tag`, `row_count`, `first_bar_ts`, `last_bar_ts`, `created_at` | Registry for exported datasets; stores the QuestDB measurement/tag applied during export (e.g., `btc25_1`). |
| `walkforward_runs` | `run_id` (UUID PK), `dataset_id` (FK), `prediction_measurement`, `target_column`, `feature_columns` (JSONB), `hyperparameters` (JSONB), `walk_config` (JSONB), `status`, `requested_by`, `started_at`, `completed_at`, `duration_ms`, `summary_metrics` (JSONB), `created_at` | Captures configuration, metrics, and links to the QuestDB measurement containing predictions/thresholds (e.g., `btc25_run1`). |
| `walkforward_folds` | PK `(run_id, fold_number)`, `train_start_idx`, `train_end_idx`, `test_start_idx`, `test_end_idx`, `samples_train`, `samples_test`, `best_iteration`, `best_score`, `thresholds` (JSONB), `metrics` (JSONB) | Fold-level detail (thresholds, hit rates, profit factors) referenced by frontend detail views. |
| `simulation_runs` | `simulation_id` (UUID PK), `run_id` (FK), `dataset_id` (FK), `input_run_measurement`, `questdb_namespace`, `mode`, `config` (JSONB), `status`, `started_at`, `completed_at`, `summary_metrics` (JSONB), `created_at` | Tracks trading simulations, including the QuestDB measurement storing trade traces. |
| `simulation_trades` | `trade_id` (UUID PK), `simulation_id` (FK), `bar_timestamp` (`TIMESTAMPTZ`), `side`, `size`, `entry_price`, `exit_price`, `pnl`, `return_pct`, `metadata` (JSONB) | Persists per-trade outcomes so backend/frontend can display exact fills without relying on QuestDB exports. |
| `simulation_trade_buckets` | PK `(simulation_id, side)`, `trade_count`, `win_count`, `profit_factor`, `avg_return_pct`, `max_drawdown_pct`, `notes` | Aggregated trade statistics complementing per-trade rows. |

*Indexes:* create B-tree indexes on `walkforward_runs(dataset_id, created_at DESC)` and `simulation_runs(run_id, created_at DESC)` to power listing endpoints; `walkforward_folds(run_id)` for detail retrieval.

### Export & Validation Plan
- **Manual export:** use the existing Dear ImGui “Export to QuestDB” button (documented in `docs/GUI_EXPORT_WORKFLOW.md`) to push indicator datasets. Tag names and measurement conventions must match the tables above.
- **Post-export checks:** run lightweight QuestDB queries (row counts, min/max timestamps) and compare against the source dataset to confirm completeness.
- **Backfill workflow:** capture each export in a run log (dataset slug, measurement name, validation query) so the backend and frontend teams can trace lineage.
- **Future automation:** once the desktop CLI is reinstated we can formalise diff scripts and manifests, but it is out of scope for Stage 1.

### Outstanding Actions & Notes
- Provision Postgres database + user on `45.85.147.236` using the schema above (see `docs/fixtures/stage1_3/postgres_schema.sql` for DDL) before backend controllers can persist metadata.
- Update Dear ImGui walkforward/trading code paths to call Postgres insertion helpers automatically after each run completes (store measurement names, hyperparameters, metrics, per-trade rows).
- Laptop app now writes Postgres INSERT statements to `docs/fixtures/stage1_3/pending_postgres_inserts.sql`; apply this spool on the frontend server after each session.
- Writing directly to `backend/QUESTDB_INGESTION.md` is blocked in this environment (permission denied); the schema above should be copied into that doc when editing on the server.
- Companion Postgres schema file should live under `backend/docs/` or `docs/storage/` once write access is available.
- Next implementation steps: build exporter CLI, create DB migrations for the Postgres tables, and replace frontend mocks with API calls hitting the new Drogon endpoints.
- Stage 1.3 fixture bundle (CSV slices, SQL checks, ILP samples, probes) lives in `docs/fixtures/stage1_3/` for backend/frontend validation. The laptop app now appends Postgres-ready UUID inserts to `docs/fixtures/stage1_3/pending_postgres_inserts.sql`; apply the file on the frontend server after each export/save session.

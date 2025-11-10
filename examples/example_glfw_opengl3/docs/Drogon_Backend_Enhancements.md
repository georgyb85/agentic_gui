# Stage1 Drogon Backend – Enhancement Tasks (Nov 2025)

The desktop app now exposes a dataset manager, async run loading, and improved QuestDB streaming.  
To decouple the UI from direct DB access (and prep a future web client), the Drogon backend must absorb those responsibilities.  
Use this doc as the task list for the backend agent.

---

## 1. Align API With Updated Data Contracts

1. **Dataset Triplet Schema**
   - `stage1_datasets` now tracks `dataset_slug`, `symbol`, `ohlcv_measurement`, `indicator_measurement`, row counts, and timestamp bounds.
   - Expose these fields (plus derived metrics from `dataset_catalog`) via `GET /api/datasets`.
   - Include `related_runs` and `related_simulations` counts to drive the desktop dataset browser.

2. **Dataset Detail Endpoint**
   - `GET /api/datasets/{id}` must return the full triplet payload:
     ```json
     {
       "dataset_id": "uuid",
       "dataset_slug": "btc25_3",
       "symbol": "BTC",
       "granularity": "1h",
       "ohlcv_measurement": "btc25_3",
       "indicator_measurement": "btc25_3_hm",
       "ohlcv_rows": 9227,
       "indicator_rows": 8143,
       "ohlcv_first_ts": 1704067200,
       "ohlcv_last_ts": 1706745600,
       "indicator_first_ts": 1704074400,
       "indicator_last_ts": 1706745600,
       "metadata": {...}
     }
     ```
   - Include `questdb_state` info (see §3) so the UI can display table health without SSH.

3. **Dataset Upsert**
   - Update `POST /api/datasets` to accept the new Stage1MetadataWriter payload (same fields as above plus `source`, `created_at`).
   - When `record_questdb=true`, trigger QuestDB export server-side (see §3.2).

## 2. Runs & Simulations Contracts

1. **Run List Filtering**
   - `GET /api/runs?dataset_id=...` should mirror `Stage1MetadataReader::ListRunsForDataset` output (run id, measurement, status, started/completed timestamps).
   - Support pagination and optional `status`, `measurement`, and `search` filters.

2. **Run Detail Payload**
   - `GET /api/runs/{id}` must return exactly what the desktop reader expects:
     - `target_column`, `feature_columns`
     - JSON `walk_config`
     - JSON `hyperparameters` (XGBoost schema)
     - `summary_metrics`
     - Array of folds with `thresholds` + `metrics`
   - Provide a `download_predictions=true` flag to stream QuestDB rows (CSV) when the UI needs to plot predictions.

3. **Run Persistence Endpoint**
   - `POST /api/runs` will replace the direct Postgres insert currently handled by `Stage1MetadataWriter`.
   - Schema: identical to `WalkforwardRecord` + `folds` array. Validate dataset_id existence (respond 409 if unknown).
   - Persist both Postgres rows and the QuestDB measurement inside the request (include measurement name + payload, or point to a temporary CSV upload).
   - Return: `{ "run_id": "...", "prediction_measurement": "...", "questdb_rows_written": N }`.

4. **Simulation Endpoints**
   - `POST /api/simulations` should accept the `Stage1MetadataWriter::SimulationRecord` + trades array.
   - Provide `GET /api/simulations?run_id=` for listing and `GET /api/simulations/{id}` for detail (with aggregated buckets).

## 3. QuestDB Responsibilities

1. **Health & Schema Inspection**
   - Expose `GET /api/questdb/measurements?prefix=btc` returning `name`, `row_count`, `first_ts`, `last_ts`, `designated_timestamp`, `partition_by`.
   - Provide `GET /api/questdb/measurement/{name}` for the Stage1 Debugger window (metadata + sample rows count).

2. **Server-Side Export**
   - Implement a service that receives Arrow/Parquet/CSV uploads (from desktop) and writes to QuestDB via ILP.
   - Accept both synchronous (blocking) and asynchronous modes:
     - `POST /api/questdb/import` (blocking) for smaller tables.
     - `POST /api/questdb/import/async` returning `job_id` + `GET /api/questdb/jobs/{job_id}` for progress.

3. **Server-Side Import**
   - Mirror the desktop `ImportWalkforwardPredictions` logic: `POST /api/questdb/export` with SQL returns streamed CSV, but also support `GET /api/runs/{id}/predictions` returning JSON (chunked) for fold rehydration.
   - Respect the new desktop timeout requirements (60s default). Allow override via query param.

## 4. Async Job & Status Reporting

1. **Job Model**
   - Create `stage1_jobs` table (job_id UUID, type, status, payload, progress, error, created_at, updated_at).
   - Use it for long exports/imports so the GUI can show progress (the new Save Run spinner should poll `/api/jobs/{id}`).

2. **WebSocket or SSE (optional)**
   - If feasible, add SSE endpoint `/api/events` broadcasting job updates so the desktop/web clients can subscribe.

## 5. Authentication & Multi-Client Readiness

1. **Token Support**
   - Enable `auth.enable_auth=true` and implement a simple token check via `X-Stage1-Token`.
   - Provide `POST /api/auth/token` (temporary) to issue tokens using a shared secret; log for future replacement.

2. **CORS / Rate Limits**
   - Configure CORS for future web UI (allow `https://agenticresearch.info` origin).
   - Add basic rate limiting (e.g., `drogon::Limiter`) so public endpoints can’t be spammed.

## 6. Desktop Integration Checklist

| Desktop Feature | Backend Requirements |
|-----------------|----------------------|
| Dataset Manager | `/api/datasets`, `/api/datasets/{id}`, `/api/questdb/measurements` |
| Export Dataset  | `/api/datasets` (POST) + `/api/questdb/import` |
| Save Run        | `/api/runs` + job polling |
| Load Run        | `/api/runs`, `/api/runs/{id}`, `/api/runs/{id}/predictions` |
| Simulation Save | `/api/simulations` + `/api/questdb/import` (trades) |
| Stage1 Debugger | `/api/questdb/*` inspection endpoints |

Document the final REST contract (OpenAPI/Swagger preferred) so both the desktop and upcoming web client can rely on the same spec.

## 7. Testing & Deliverables

1. Unit tests for DatasetService, RunPersistenceService, QuestDbService (use Drogon’s testing harness or gtest).
2. Integration tests hitting a staging QuestDB/Postgres (can seed with fixtures from `/docs/fixtures/stage1_3`).
3. Updated README covering:
   - Auth flow
   - Job polling
   - Dataset/run payload schemas
4. Demo script showing:
   - Creating a dataset via API
   - Uploading OHLCV/indicator frames
   - Saving a run & polling job status
   - Loading the run payload + predictions

---

**Note for backend agent:** coordinate with the desktop team before changing payload schemas; the current UI uses `Stage1MetadataWriter`/`Stage1MetadataReader` structures verbatim, so keep field names identical to avoid breaking existing builds. Add versioned endpoints if you must change the response format.  
Future phases will retire direct DB connections entirely once these APIs are battle-tested. Running this checklist gets us ready for that cutover.

# Backend TODO (Stage1 Drogon)

These tasks are intentionally scoped and ordered; please stick to them and avoid adding unsolicited features. No authentication/rate limiting changes are needed right now—we’ll tackle those later.

---

## 1. Job Worker Pool (no cron jobs)
1. Implement a lightweight worker loop (inside the Drogon process or a companion binary) that continuously polls `stage1_jobs` for `PENDING` rows.
2. Workers should atomically claim jobs (`status=PENDING` → `RUNNING`, set `started_at`) and process them immediately. If no jobs exist, wait/sleep briefly; **do not** rely on cron.
3. On success/failure, update `progress`, `status`, `result`, `error`, and `completed_at` so `/api/jobs/{id}` reflects real state.
4. Expose worker metrics/logging (job id, job type, duration) for debugging.

## 2. QuestDB Import/Export Completion
1. Finalize `/api/questdb/import` and `/api/questdb/import/async`:
   - Accept CSV (or Arrow) payloads, store them temporarily (e.g., `/opt/stage1_server/uploads`).
   - Enqueue a `questdb_import` job referencing the temp file + target measurement.
   - Worker uses the existing ILP batching logic (open TCP 9009, stream line protocol, close) to push data to QuestDB. No “telnet” shell commands; reuse our ILP helper.
2. Add meaningful validation: reject bodies missing required columns (e.g., `timestamp_unix`, `prediction`). Respond 400 with a list of missing fields.
3. Return job id + instructions for polling, and document the response structure.

## 3. Dataset/Run API Alignment
1. Ensure `GET /api/datasets` and `/api/datasets/{id}` return the full dataset triplet our desktop expects (dataset_id, slug, ohlcv & indicator measurements, row counts, timestamp bounds, etc.).
2. When `POST /api/datasets` succeeds, persist the QuestDB measurement names produced by the import endpoints so desktop doesn’t have to guess.
3. Verify `GET /api/runs/{id}` includes folds, thresholds, features, timestamps exactly as `Stage1MetadataReader` consumes today (no schema drift).
4. Confirm `/api/runs/{id}/predictions` streams the same columns the desktop importer expects (timestamp, bar_index, fold_number, prediction, target, thresholds).

## 4. Tests & Demo Verification
1. Now that 404s are fixed, actually run `demo.sh` and capture real responses; update README/report if needed.
2. Add a few deterministic tests (Drogon test cases or scripted curl checks) covering:
   - GET /api/health
   - GET /api/datasets (seed with fixtures)
   - POST /api/questdb/import/async + job polling (mock QuestDB)
   - GET /api/runs/{id}/predictions (mock QuestDB)
3. Keep tests minimal; no need for a huge harness—just enough to prevent regressions.

## 5. Desktop Integration Support
1. Add endpoints the desktop needs to fully switch over:
   - `GET /api/datasets/{id}/runs` to mirror the Stage1 Dataset Manager’s list
   - `GET /api/simulations/{id}/trades` (pull stored trades once we persist them)
2. Document the REST contract (README or a small markdown section) so the desktop knows exact payloads, required headers, error formats.

## 6. Deferred Items (do NOT work on yet)
- Authentication, rate limiting, WebSockets/SSE, monitoring dashboards, OpenAPI spec, etc. will be tackled in a later phase. Leave current auth filter disabled.

Stick to the list above; if something is ambiguous, ask before coding. The goal is to get the existing desktop flows unblocked, not to re-architect the server.

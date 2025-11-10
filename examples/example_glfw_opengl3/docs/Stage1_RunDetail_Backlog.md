# Stage1 Run Detail API Bug

We cannot render saved walkforward runs inside the desktop app because the Stage1
REST API no longer returns fold data.

## Reproduction
1. Export any dataset + walkforward run from the desktop client (which POSTs to `/api/runs` with the `folds` array populated).
2. Query the run:
   ```bash
   curl -s https://agenticresearch.info/api/runs/<run_id> | jq .
   ```
3. Response only includes top-level metadata (`run_id`, `prediction_measurement`, `hyperparameters`, `serialized_config`, …) – there is **no** `folds` array or per-fold metrics.
4. `/api/walkforward/runs/<run_id>` returns HTTP 404, so there is no alternate endpoint with fold data.

## Impact
- Desktop “Load Saved Run…” modal fetches run metadata successfully, but the UI shows zero folds, no performance tables, and no plots.
- Trade Simulation cannot seed itself from saved runs because it relies on the fold thresholds and metadata.

## Required Backend Fixes
1. **Persist folds when accepting `POST /api/runs`.**
   - Current implementation stores the run in `walkforward_runs` (confirmed by `/api/datasets/{id}/runs`), but the associated rows never appear in `walkforward_folds`.
   - Parse the incoming JSON `folds` array and insert each record (fold number, ranges, metrics, thresholds, etc.).

2. **Return folds in `GET /api/runs/{id}`.**
   - Response should match the schema the desktop previously consumed (same as the legacy SQL reader): a `run` object plus a `folds` array with JSON blobs for thresholds + metrics.
   - Add unit tests that hit the controller, insert a run + folds fixture via the repository, and verify the JSON contains the expected data.

3. **(Optional but recommended)** re‑expose the legacy endpoint (`GET /api/walkforward/runs/{id}`) or update the documentation to reflect the new behaviour so the desktop has a guaranteed fallback.

Once the backend delivers fold data again, the existing desktop code will immediately start showing plots/metrics (no further client changes required).

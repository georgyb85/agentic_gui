# Stage1 / GPU Refactor Plan

This doc outlines how to remove the timestamp hacks across the desktop exporter, Stage1 backend, QuestDB/Postgres schema, the Kraken GPU node, and the trading-dashboard. Goal: every consumer works from a single, canonical timestamp representation and does not need to reconstruct time from TSSB `Date`/`Time`.

---

## 1. Desktop / ChronosFlow Exporter

1. **Normalize timestamps at export time**
   - When ChronosFlow reads TSSB data it already converts to Unix ms. Include that value in the exported CSV/JSON for both OHLCV and indicator records (e.g., `timestamp_ms` column).
   - Ensure lookback rows (the warm-up indicators at the start) are preserved but flagged so Stage1 knows how many “non trainable” rows to expect.

2. **Update dataset packaging**
   - Export a metadata manifest alongside the raw rows that lists:
     - `bar_interval_ms`
     - `lookback_rows` (how many OHLCV bars precede the first indicator)
     - `first_bar_ts`, `last_bar_ts`
   - Manifest is consumed by Stage1 when ingesting.

3. **Backward compatibility tooling**
   - Provide a CLI/utility to re-export older datasets with the new manifest + timestamp column so they can be re-imported without manual editing.

---

## 2. Stage1 Backend (Drogon) + Schema

### 2.1 QuestDB schema changes
1. **Indicators/OHLCV tables**
   - Since we’re discarding the old data, the exporter can create fresh tables with a proper `timestamp` column (QuestDB auto‑creates tables on first insert).
   - Configure the exporter/Stage1 ingestion to include a `timestamp` field in every ILP/HTTP insert so the canonical column is always populated.
2. **Partitions**
   - Switch QuestDB to use monthly partitions (default is daily) to reduce table churn and improve retention management:
     - e.g., `PARTITION BY MONTH` in the ILP definition.
   - Document this choice so future datasets follow the same pattern.

### 2.2 Postgres schema changes
> **Reset plan:** we can drop the existing tables and rebuild fresh schema, so no data migration is required.

1. **Datasets metadata table**
   - Create tables for `datasets`, `runs`, `folds`, etc., that include:
     - `bar_interval_ms`, `lookback_rows`, `first_indicator_ts`, `first_ohlcv_ts`.
     - Any new manifest metadata needed by frontend/GPU nodes.
2. **Runs / folds**
   - Persist both index and timestamp range columns (`train_start_idx`, `train_start_ts_ms`, etc.).
   - When a run is inserted, Stage1 populates the timestamp range immediately, so all consumers can rely on it.
3. **Privileges**
   - Reuse the existing Postgres credentials; the reset just requires dropping and recreating tables under the same role.

### 2.3 API redesign
1. **/api/datasets/{id}/indicators**
   - Guarantee that the response includes `timestamp_ms` (or `timestamp` as ms) for every row, populated from the canonical column.
   - Allow `start_index`/`end_index` filters in addition to timestamp filters so consumers can retrieve rows by raw index.
2. **New helper endpoint (optional)**
   - `GET /api/datasets/{id}/index_map?start_idx=...&end_idx=...` returning `{ index, timestamp }` pairs to rebuild folds without fetching all indicator columns.
3. **Dataset manifest endpoint**
   - `GET /api/datasets/{id}/manifest` returning interval, lookback, first/last timestamps, etc.

---

## 3. Kraken GPU Node

1. **Stage1DatasetClient**
   - Once the API guarantees real timestamps, simplify the parser:
     - Remove TSSB `Date`/`Time` fallbacks.
     - Trust `timestamp_ms`.
     - Leverage the `manifest` endpoint for interval/lookback info instead of inferring from live rows.
2. **Run metadata usage**
   - When a run is loaded (e.g., during reconnect), rely on the stored timestamp range instead of the original index values. If both are stored, the controller can sanity-check them.
3. **Optional improvements**
   - If Stage1 exposes `start_index/end_index`, allow `/xgboost` to accept either indexes or timestamps.
   - Add validation: reject training windows shorter than the lookback or outside dataset bounds using the manifest.

---

## 4. Trading Dashboard

1. **Dataset context**
   - Consume the new manifest fields (interval, lookback, first timestamp) and display them in the dataset selector.
2. **Fold-to-timestamp conversion**
   - When a user selects “Examine,” convert fold indices to timestamps directly from the run metadata or by using the `/index_map` helper instead of fetching full indicator rows.
3. **Remove hacks**
   - Drop any code that tries to parse `Date`/`Time` manually; rely on the API’s canonical timestamp.
   - Eliminate fallback logic that guesses interval/offset.
4. **UI enhancements**
   - Warn if the user selects a range that goes beyond `last_indicator_ts` (per manifest).
   - Show manifest data (lookback, interval) in the fold info panel for clarity.

---

## 5. Stage1 Dro gon Ingestion Path (Desktop → Server)

1. **Import contract**
   - Update the existing desktop “Export dataset” flow so it POSTs both the raw rows (with `timestamp_ms`) and the manifest to Stage1.
   - Stage1 validates the manifest (e.g., interval matches row deltas, lookback consistent with indicator start).
   - Document the required format so future tickerplants can produce compatible data.
2. **Append flow**
   - When new OHLCV/indicator bars arrive from tickerplants, enforce the manifest invariants:
     - Indicators must lag OHLCV by exactly `lookback_rows`.
     - No missing timestamps; reject or mark data if gaps appear.
3. **Tickers + retrain path**
   - The midnight retrain job can now compute “last N hours” purely from timestamps, without special-case logic for warm-ups.

---

## 6. Deployment Steps

1. Update ChronosFlow exporter + manifest and re-export all historical datasets.
2. Patch Stage1 schema (QuestDB + Postgres), backfill timestamps, deploy new APIs.
3. Redeploy Kraken trading server with the simplified Stage1 client (no fallbacks).
4. Redeploy trading-dashboard once manifest endpoints are live (remove conversion hacks).

---

This staged approach lets us remove the timestamp “workarounds” entirely: every component receives the same, correct representation of time, so models, dashboards, and future tickerplants all speak one dialect. Once Stage1 guarantees canonical timestamps and publishes the manifest/index map, we can delete the current parsing code from both frontend and GPU services.***

## Implementation Notes (Dec 2024)

- Desktop exporter now emits a canonical `timestamp_ms` column via QuestDB ILP and writes a `manifest.json` alongside every dataset export. The same manifest JSON is pushed into Stage1’s metadata payload so Drogon can serve it back verbatim.
- A headless CLI (`Stage1DatasetReexporter.cpp`) reuses the ChronosFlow loader to batch re-export legacy TSSB files with the manifest/timestamp contract.
- Stage1 backend exposes `/api/datasets/{id}/manifest` and `/api/datasets/{id}/index_map`, and `/api/datasets/{id}/indicators` always returns aligned rows that include `timestamp_ms`.
- Kraken’s GPU node uses the simplified Stage1DatasetClient (no `Date`/`Time` reconstruction) and can optionally fetch manifests/index maps for validation.
- Trading dashboard fetches manifests/index maps so fold windows are converted to timestamps without downloading full indicator tables.

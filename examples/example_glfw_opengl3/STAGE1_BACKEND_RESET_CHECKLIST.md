# Stage1 Backend Reset Checklist (QuestDB + Postgres)

This guide is for the backend agent who will reset Stage1’s storage (no data migration required) and align it with the redesigned dataset contract. The exporter will re-ingest fresh datasets afterwards.

---

## Environment Assumptions

- Existing QuestDB and Postgres instances running on the same host(s) as before.
- Existing database credentials remain valid; we only drop/recreate tables.
- The exporter and Drogon server will be updated separately to insert canonical timestamps and manifest rows after you finish these steps.

---

## 1. QuestDB

Goal: start with clean indicator/OHLCV tables that include a canonical `timestamp` column and monthly partitions.

1. **Stop ingest jobs (if any running)** so no writers touch the old tables during cleanup.
2. **Drop old tables** via QuestDB CLI or REST:
   ```sql
   DROP TABLE IF EXISTS ohlcv_<dataset_slug>;
   DROP TABLE IF EXISTS indicators_<dataset_slug>;
   -- Repeat for each dataset if necessary (or wipe the entire DB if no data needs to be preserved).
   ```
3. **No schema migration needed**—QuestDB auto-creates tables when the new exporter starts inserting rows.
4. **Document ingestion settings** for the updated exporter:
   - Every insert must include a UTC `timestamp` (ms) column (e.g., `timestamp`, `ts`, or whichever name you standardize on).
   - Use `PARTITION BY MONTH` to reduce partition churn. Example ILP DDL (for reference):
     ```sql
     CREATE TABLE indicators_<slug> (
         timestamp TIMESTAMP,
         Date LONG,
         Time LONG,
         ...
     ) TIMESTAMP(timestamp) PARTITION BY MONTH;
     ```
   - The desktop exporter or Stage1 ingestion pipeline should perform the actual table creation by inserting the first row with the chosen schema.
5. **Restart ingestion jobs** once steps above are complete and the exporter has been reconfigured.

---

## 2. Postgres

Goal: rebuild Stage1’s metadata tables so they match the new manifest contract (fresh start, no migration).

1. **Log into Postgres** using the same credentials as before (e.g., `psql -U stage1 -d stage1_db`).
2. **Drop the old metadata tables** (if they exist):
   ```sql
   DROP TABLE IF EXISTS walkforward_folds CASCADE;
   DROP TABLE IF EXISTS walkforward_runs CASCADE;
   DROP TABLE IF EXISTS datasets CASCADE;
   -- Drop any additional Stage1-specific tables (jobs, simulations, etc.) if they will be re-seeded.
   ```
3. **Create the new schema** (example DDL excerpt—adjust column names/types to match the final manifest spec):
   ```sql
   CREATE TABLE datasets (
       dataset_id UUID PRIMARY KEY,
       dataset_slug TEXT NOT NULL,
       symbol TEXT,
       granularity TEXT,
       bar_interval_ms BIGINT NOT NULL,
       lookback_rows INTEGER NOT NULL,
       first_ohlcv_ts BIGINT,
       first_indicator_ts BIGINT,
       metadata JSONB DEFAULT '{}',
       created_at TIMESTAMPTZ DEFAULT NOW()
   );

   CREATE TABLE walkforward_runs (
       run_id UUID PRIMARY KEY,
       dataset_id UUID REFERENCES datasets(dataset_id),
       status TEXT,
       feature_columns JSONB,
       target_column TEXT,
       hyperparameters JSONB,
       walk_config JSONB,
       summary_metrics JSONB,
       started_at TIMESTAMPTZ,
       completed_at TIMESTAMPTZ,
       created_at TIMESTAMPTZ DEFAULT NOW()
   );

   CREATE TABLE walkforward_folds (
       run_id UUID REFERENCES walkforward_runs(run_id),
       fold_number INTEGER,
       train_start_idx BIGINT,
       train_end_idx BIGINT,
       test_start_idx BIGINT,
       test_end_idx BIGINT,
       train_start_ts_ms BIGINT,
       train_end_ts_ms BIGINT,
       test_start_ts_ms BIGINT,
       test_end_ts_ms BIGINT,
       samples_train INTEGER,
       samples_test INTEGER,
       metrics JSONB,
       thresholds JSONB,
       PRIMARY KEY (run_id, fold_number)
   );
   ```
4. **Verify privileges**: ensure the same Postgres role/users have `SELECT/INSERT/UPDATE` permissions as before (no change needed if you drop/recreate tables under the same owner).
5. **Optional**: Seed any initial entries (e.g., dataset catalog) if the exporter needs them before re-ingesting. Otherwise, the exporter/Drogon server will populate the tables as new datasets arrive.

---

## 3. Post-reset Checklist

1. Confirm QuestDB is empty (or at least that the old tables are gone).
2. Confirm the new Postgres tables exist and are empty.
3. Notify the application team that they can start re-exporting datasets and redeploy the Stage1/Drogon server.
4. Once the new data arrives, sample a row via `curl https://.../api/datasets/{id}/indicators?limit=5` to ensure `timestamp` contains UTC values instead of `1970-01-01`.

That’s it—after this reset, Stage1 will be ready for the updated manifest-aware exporter and the rest of the stack (GPU node, dashboard) can rely on canonical timestamps.***

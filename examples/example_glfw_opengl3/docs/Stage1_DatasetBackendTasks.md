# Stage 1 Dataset Triplet – Backend Task List

This checklist tells the database agent which schema/features must land before the desktop client can treat a “dataset” as the full `(OHLCV QuestDB table, indicator QuestDB table, metadata row)` triplet. The tasks assume the current Stage 1.3 schema is already deployed (see `docs/fixtures/stage1_3/postgres_schema.sql`) and that the credentials/IP rules from the previous runbook remain valid.

---

## 0. Preparation
- Take a fresh schema backup:  
  `pg_dump -h 127.0.0.1 -U stage1_app stage1_trading > /root/backups/stage1_$(date +%Y%m%d_%H%M%S).sql`
- Confirm no pending inserts: replay `docs/fixtures/stage1_3/pending_postgres_inserts.sql` before altering tables.
- Apply changes inside a transaction or with repeatable scripts; every step below should be idempotent (`IF NOT EXISTS`, `CREATE OR REPLACE`).

---

## 1. Master Dataset Table
Create a canonical table that stores the entire dataset triplet and associated stats.

```sql
CREATE TABLE IF NOT EXISTS stage1_datasets (
    dataset_id          UUID PRIMARY KEY,
    dataset_slug        TEXT NOT NULL UNIQUE,
    symbol              TEXT,
    granularity         TEXT,
    source              TEXT DEFAULT 'laptop_imgui',
    ohlcv_measurement   TEXT NOT NULL,
    indicator_measurement TEXT NOT NULL,
    ohlcv_row_count     BIGINT DEFAULT 0,
    indicator_row_count BIGINT DEFAULT 0,
    ohlcv_first_ts      TIMESTAMPTZ,
    ohlcv_last_ts       TIMESTAMPTZ,
    indicator_first_ts  TIMESTAMPTZ,
    indicator_last_ts   TIMESTAMPTZ,
    metadata            JSONB DEFAULT '{}'::jsonb,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_stage1_datasets_measurements
    ON stage1_datasets (ohlcv_measurement, indicator_measurement);
```

- Use triggers (or `DEFAULT now()` + `ON UPDATE`) to keep `updated_at` fresh if desired.

### 1.1 Backfill
Populate the new table from the existing `indicator_datasets` rows so no data is lost:
```sql
INSERT INTO stage1_datasets (dataset_id, dataset_slug, symbol, granularity,
                             source, indicator_measurement, indicator_row_count,
                             indicator_first_ts, indicator_last_ts, metadata)
SELECT dataset_id,
       COALESCE(questdb_tag, dataset_id::text) AS slug,
       NULLIF(symbol, '') AS symbol,
       NULLIF(granularity, '') AS granularity,
       COALESCE(source, 'legacy_import'),
       questdb_tag,
       row_count,
       first_bar_ts,
       last_bar_ts,
       jsonb_build_object('legacy_source', source)
FROM indicator_datasets
ON CONFLICT (dataset_id) DO NOTHING;
```
- Leave `ohlcv_measurement` empty for now; the desktop exporter will fill it later.

---

## 2. Reconcile Existing Tables

1. **Foreign key** – Ensure `indicator_datasets.dataset_id` references `stage1_datasets(dataset_id)`:
   ```sql
   ALTER TABLE indicator_datasets
       ADD CONSTRAINT indicator_datasets_stage1_fk
       FOREIGN KEY (dataset_id) REFERENCES stage1_datasets(dataset_id)
       ON DELETE CASCADE;
   ```

2. **Optional view for backward compatibility** – If other clients still read `indicator_datasets`, consider turning it into a `VIEW` that selects from `stage1_datasets` plus indicator-specific stats. (Skip if not needed yet.)

3. **Metadata writer hooks** – Add helper SQL the backend can call when the desktop wants to upsert a dataset:
   ```sql
   CREATE OR REPLACE FUNCTION upsert_stage1_dataset(
       p_dataset_id UUID,
       p_dataset_slug TEXT,
       p_symbol TEXT,
       p_granularity TEXT,
       p_source TEXT,
       p_ohlcv_measurement TEXT,
       p_indicator_measurement TEXT,
       p_ohlcv_rows BIGINT,
       p_indicator_rows BIGINT,
       p_ohlcv_first TIMESTAMPTZ,
       p_ohlcv_last TIMESTAMPTZ,
       p_indicator_first TIMESTAMPTZ,
       p_indicator_last TIMESTAMPTZ,
       p_metadata JSONB)
   RETURNS VOID AS $$
   BEGIN
       INSERT INTO stage1_datasets (...)
       VALUES (...)
       ON CONFLICT (dataset_id) DO UPDATE SET
           dataset_slug = EXCLUDED.dataset_slug,
           symbol = EXCLUDED.symbol,
           granularity = EXCLUDED.granularity,
           source = EXCLUDED.source,
           ohlcv_measurement = EXCLUDED.ohlcv_measurement,
           indicator_measurement = EXCLUDED.indicator_measurement,
           ohlcv_row_count = EXCLUDED.ohlcv_row_count,
           indicator_row_count = EXCLUDED.indicator_row_count,
           ohlcv_first_ts = EXCLUDED.ohlcv_first_ts,
           ohlcv_last_ts = EXCLUDED.ohlcv_last_ts,
           indicator_first_ts = EXCLUDED.indicator_first_ts,
           indicator_last_ts = EXCLUDED.indicator_last_ts,
           metadata = COALESCE(stage1_datasets.metadata, '{}'::jsonb) || COALESCE(EXCLUDED.metadata, '{}'::jsonb),
           updated_at = NOW();
   END;
   $$ LANGUAGE plpgsql;
   ```
   (Fill the `...` columns in the actual script.)

---

## 3. Walkforward/Simulation Tables

1. Guarantee that `walkforward_runs.dataset_id` and `simulation_runs.dataset_id` reference the new master table:
   ```sql
   ALTER TABLE walkforward_runs
       ADD CONSTRAINT walkforward_runs_dataset_fk
       FOREIGN KEY (dataset_id) REFERENCES stage1_datasets(dataset_id);

   ALTER TABLE simulation_runs
       ADD CONSTRAINT simulation_runs_dataset_fk
       FOREIGN KEY (dataset_id) REFERENCES stage1_datasets(dataset_id);
   ```

2. Add missing unique constraints if they don’t exist yet:
   ```sql
   ALTER TABLE walkforward_runs
       ADD CONSTRAINT walkforward_runs_dataset_measurement_uq
       UNIQUE (dataset_id, prediction_measurement);

   ALTER TABLE simulation_runs
       ADD CONSTRAINT simulation_runs_run_namespace_uq
       UNIQUE (run_id, questdb_namespace);
   ```

3. Keep `serialized_config` columns from the previous task list; they’ll store the clipboard payload emitted by the GUI.

---

## 4. Views & Helper Functions

### 4.1 Dataset catalog view
Expose both measurements plus rollups so the GUI can render a dataset list without multiple queries.

```sql
CREATE OR REPLACE VIEW dataset_catalog AS
SELECT d.dataset_id,
       d.dataset_slug,
       d.symbol,
       d.granularity,
       d.ohlcv_measurement,
       d.indicator_measurement,
       d.ohlcv_row_count,
       d.indicator_row_count,
       d.ohlcv_first_ts,
       d.ohlcv_last_ts,
       d.indicator_first_ts,
       d.indicator_last_ts,
       COALESCE(r.run_count, 0) AS run_count,
       COALESCE(s.sim_count, 0) AS simulation_count,
       d.created_at,
       d.updated_at
FROM stage1_datasets d
LEFT JOIN (
    SELECT dataset_id, COUNT(*) AS run_count
    FROM walkforward_runs
    GROUP BY dataset_id
) r ON r.dataset_id = d.dataset_id
LEFT JOIN (
    SELECT dataset_id, COUNT(*) AS sim_count
    FROM simulation_runs
    GROUP BY dataset_id
) s ON s.dataset_id = d.dataset_id;
```

### 4.2 Paging helpers
Update (or create) helper functions so the desktop can fetch lists:
```sql
CREATE OR REPLACE FUNCTION fn_list_datasets(p_limit INT DEFAULT 50, p_offset INT DEFAULT 0)
RETURNS TABLE (...) AS $$
    SELECT * FROM dataset_catalog
    ORDER BY updated_at DESC
    LIMIT p_limit OFFSET p_offset;
$$ LANGUAGE sql STABLE;
```

Update `fn_list_runs` / `fn_list_simulations` to join `stage1_datasets` so each run row returns both measurement names and dataset slug (required for loading in the UI).

### 4.3 dataset_hierarchy view
Rebuild the existing view to reference `stage1_datasets` and include:
- dataset_slug, ohlcv_measurement, indicator_measurement
- run_id, prediction_measurement, serialized_config
- simulation_id, questdb_namespace, status

This lets Stage1ServerWindow and Dataset Manager show full lineage in one query.

---

## 5. Utilities & Validation

1. **Backfill script** – Update `/root/stage1_3_extracted/postgres_validation_checks.sql`:
   - new section verifying every dataset row has both measurements populated,
   - report any walkforward run referencing a dataset that lacks measurements.

2. **Pending inserts spool** – Ensure the cron/ops task that replays `pending_postgres_inserts.sql` still runs nightly so offline desktop exports eventually populate the new tables.

3. **Access control** – If new roles/schemas were added, revalidate `GRANT` statements so `stage1_app` can `SELECT/INSERT/UPDATE` the new table and `upsert_stage1_dataset` function.

---

## 6. Testing Checklist

1. `psql` smoke tests:
   - `SELECT * FROM fn_list_datasets(5,0);`
   - `SELECT * FROM dataset_hierarchy LIMIT 5;`
   - Insert a dummy dataset via `upsert_stage1_dataset(...)`, then confirm it appears alongside its slug and measurement names.
2. Foreign-key enforcement:
   - Attempt to insert a walkforward run with a bogus `dataset_id` (should fail).
   - Insert a run for an existing dataset and ensure the unique constraint blocks duplicate `prediction_measurement`.
3. Validation script: run `/root/stage1_3_extracted/postgres_validation_checks.sql` and confirm the new dataset checks pass.

Document the exact SQL and verification commands in `stage1_3_database_implementation_report.md` once done so the frontend team can rely on the new schema.

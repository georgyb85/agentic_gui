# Stage 1 Serialization & Persistence – Database Server Tasks

The following work items extend the Stage 1.3 schema (`docs/fixtures/stage1_3/postgres_schema.sql`) so that the desktop refactors can persist and reload datasets, runs, and simulations without manual glue. Execute these on the Postgres host (45.85.147.236 unless a staging clone is in use) after replaying the existing schema and inserts from `docs/fixtures/stage1_3/DEPLOY_TASKS.md`.

## 1. Align Schema with the Upcoming Serializer
1. **Add clipboard payload columns**
   - `ALTER TABLE walkforward_runs ADD COLUMN IF NOT EXISTS serialized_config TEXT;`
   - `ALTER TABLE simulation_runs ADD COLUMN IF NOT EXISTS serialized_config TEXT;`
   - Purpose: store the exact text emitted by `RunConfigSerializer` so the desktop and backend can share the same payload that users copy/paste. Keep the JSONB columns for structured queries; the serialized text is the human-readable snapshot.
2. **Backfill existing rows** – For earlier fixtures, populate `serialized_config` with the concatenation of `feature_columns`, `hyperparameters`, and `walk_config` JSON until the desktop exporter starts writing the new field.

## 2. Improve Lookup Performance for Dataset/Run Selection
1. **Indexes on measurement columns**
   - `CREATE INDEX IF NOT EXISTS idx_walkforward_runs_measurement ON walkforward_runs (prediction_measurement);`
   - `CREATE INDEX IF NOT EXISTS idx_simulation_runs_namespace ON simulation_runs (questdb_namespace);`
   - Rationale: the UI will resolve runs/simulations by measurement/namespace when hydrating ChronosFlow frames, so we need indexed lookups beyond the existing dataset/time indexes (postgres_schema.sql:36-74).
2. **Unique constraints for deterministic names**
   - Enforce uniqueness on `(dataset_id, prediction_measurement)` and `(run_id, questdb_namespace)` to prevent accidental collisions when multiple exports reuse the same slug.

## 3. Provide Hierarchical Views/Queries
1. **Create a `dataset_hierarchy` view** that joins `indicator_datasets`, `walkforward_runs`, and `simulation_runs`, exposing columns such as dataset slug, run_id, run measurement, simulation_id, simulation namespace, created_at, and status. This view will back the UI’s dataset/run browser without embedding multi-join SQL inside the app.
2. **Add stored procedures or parameterized functions** (e.g., `fn_list_runs(p_dataset_id UUID, p_limit INT)`) so the desktop client can fetch paginated lists via simple `SELECT * FROM fn_list_runs(...)`.

## 4. Automate Fixture Replay & Auditing
1. **Cron job for `pending_postgres_inserts.sql`** – Mirror the desktop spool file into Postgres every night (or via rsync + psql) so the shared metadata store stays in sync even if the desktop ran offline.
2. **Validation queries** – Adapt `docs/fixtures/stage1_3/questdb_checks.sql` and add companion Postgres checks (row counts per dataset/run) so operators can quickly verify that ChronosFlow exports and metadata rows stay aligned after schema changes.

## 5. Housekeeping
1. **Retention/backups** – Enable the retention scripts and Postgres backups referenced in `docs/fixtures/stage1_3/DEPLOY_TASKS.md` step 5 before onboarding more datasets/runs.
2. **Document new env vars** – If the new Stage1 metadata reader exposes additional connection parameters, document them alongside `STAGE1_POSTGRES_*` inside the server runbook.

## 6. Remote Debug Access
1. **Allow-list desktops for Postgres tools** – The new Stage1 Server Debugger window (shipped with the Dear ImGui client) connects directly to Postgres using the `stage1_app` role. Add the requesting workstation’s public IPs (or the shared VPN subnet) to `pg_hba.conf` (e.g. `hostssl stage1_trading stage1_app 124.122.187.0/24 md5`) and reload Postgres. For laptops with dynamic addresses, prefer routing through the existing VPN/jump host so the source IP is stable; alternatively, provide users with an SSH tunnel recipe (see below) instead of editing `pg_hba.conf` for every IP change. Without one of these options the debugger will fail with `no pg_hba.conf entry` errors.
2. **Document the process** – Note the approved IP ranges and the reload command (`SELECT pg_reload_conf();` or `systemctl reload postgresql@14-main`) in the deployment checklist so future troubleshooting steps don’t require SSH access.
3. **SSH tunnel fallback (recommended for roaming clients)** – Document a simple port-forward command such as:
   ```bash
   ssh -N -L 5433:127.0.0.1:5432 stage1@45.85.147.236
   ```
   With the tunnel active, users launch the Stage1 debugger/CLI with `STAGE1_POSTGRES_HOST=127.0.0.1` and `STAGE1_POSTGRES_PORT=5433`, avoiding any dependency on their public IP.

## 7. Dataset Integrity
1. **Backfill missing `indicator_datasets` rows** – When new walk-forward runs are saved straight from the GUI, the dataset slug must already exist in `indicator_datasets` or Postgres will reject the insert. Create a helper SQL script that inserts (or upserts) a placeholder dataset entry given a slug/UUID so users don’t have to touch the table manually:
   ```sql
   INSERT INTO indicator_datasets (dataset_id, symbol, granularity, source, questdb_tag,
                                   row_count, first_bar_ts, last_bar_ts)
   VALUES (:uuid, :symbol, 'unknown', 'laptop_imgui', :slug, 0, NOW(), NOW())
   ON CONFLICT (dataset_id) DO NOTHING;
   ```
   Run it for any slug reported in the app logs (for example `btc_2025_4h`) before attempting to persist runs/folds.
2. **Validation step** – Extend the existing validation script to flag walkforward runs whose `dataset_id` isn’t present in `indicator_datasets` so the agent can backfill proactively.

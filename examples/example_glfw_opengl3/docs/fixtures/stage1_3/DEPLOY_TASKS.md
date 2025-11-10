# Stage 1.3 Data Deployment Checklist (Frontend Server 45.85.147.236)

You now have the entire `docs/fixtures/stage1_3/` directory copied onto the frontend node.  
Run the following tasks (in order) to hydrate Postgres and QuestDB with the desktop exports.

## 1. Apply / Update the Postgres Schema

```bash
export PGHOST=localhost
export PGUSER=stage1_app
export PGPASSWORD='St4g3!Tr4d1ng#2025'
export PGDATABASE=stage1_trading

psql -v ON_ERROR_STOP=1 -f docs/fixtures/stage1_3/postgres_schema.sql
```

- Creates/updates `indicator_datasets`, `walkforward_runs`, `walkforward_folds`, `simulation_runs`,  
  `simulation_trades`, and `simulation_trade_buckets`.
- Safe to re-run; all DDL is guarded by `IF NOT EXISTS`.

## 2. Replay Desktop Metadata Inserts

```bash
psql -v ON_ERROR_STOP=1 -f docs/fixtures/stage1_3/pending_postgres_inserts.sql
```

- Inserts every dataset/run/simulation captured on the laptop.
- The file uses deterministic UUIDs and `ON CONFLICT` clauses, so replays are idempotent.

## 3. Seed QuestDB with Reference Measurements (Optional Smoke Test)

If you want the small sample series that ship with the fixtures:

```bash
cat docs/fixtures/stage1_3/ilp_samples.txt | nc 127.0.0.1 9009
```

- The Dear ImGui desktop app will send the full datasets during normal exports; this step only verifies connectivity.

## 4. Sanity Checks

Postgres:

```bash
psql -c "SELECT dataset_id, questdb_tag, row_count FROM indicator_datasets ORDER BY created_at DESC LIMIT 5;"
psql -c "SELECT run_id, prediction_measurement, status FROM walkforward_runs ORDER BY completed_at DESC LIMIT 5;"
psql -c "SELECT simulation_id, questdb_namespace, mode FROM simulation_runs ORDER BY completed_at DESC LIMIT 5;"
```

QuestDB (REST):

```bash
curl "http://127.0.0.1:9000/exec?query=SELECT+name,+partitionBy,+max(timestamp)+FROM+tables()"
```

Expect to see `indicator_bars`, and (after real exports) `walkforward_predictions` and `trading_sim_traces`.

## 5. Hook Up Backups / Retention (once data is loaded)

- Enable the retention scripts documented in `trading-dashboard/docs/infra/questdb_retention_policy.md`.
- Add the Postgres backup cron job from `trading-dashboard/docs/infra/frontend.md`.

Once these steps are complete, the backend GPU node (`39.114.73.97`) can query live walk-forward metadata from Postgres and fetch the high-frequency series from QuestDB without any further desktop involvement.

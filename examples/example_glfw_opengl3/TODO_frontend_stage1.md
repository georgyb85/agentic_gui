# Frontend Server Stage 1 TODO (Steps 1.1–1.3)

Host: `45.85.147.236` (persistent). Houses QuestDB, Postgres, and production `trading-dashboard`.

## Stage 1.1 – Architecture & Alignment
- [ ] Clone/pull the latest `trading-dashboard` codebase from this repo and place it under the appropriate deployment directory.
- [ ] Verify QuestDB service health (ports 9000/9009) and document connection info in `docs/infra/frontend.md` (new file to create on the server).
- [ ] Inventory firewall rules so the backend node (`39.114.73.97`) can reach QuestDB ILP/REST and Postgres.

## Stage 1.2 – Schema & Data Pipeline Hardening
- [ ] Create Postgres role + database dedicated to Stage 1 (`CREATE ROLE stage1_app ...`, `CREATE DATABASE stage1_trading ...`).
- [ ] Apply the schema from `docs/fixtures/stage1_3/postgres_schema.sql` and confirm new tables (`indicator_datasets`, `walkforward_runs`, `simulation_runs`, `simulation_trades`, etc.) are present.
- [ ] Configure Postgres authentication (pg_hba.conf) to allow the backend node’s IP with least-privilege credentials; reload the service.
- [ ] Store the Postgres DSN and QuestDB endpoints in a secure secrets file for backend usage (do **not** commit to git).
- [ ] Schedule QuestDB retention/backup policy for the new measurements (`indicator_bars`, `walkforward_predictions`, `trading_sim_traces`).

## Stage 1.3 – Backend Data Access Layer Support
- [ ] Load laptop-provided datasets into QuestDB using the ILP lines (`docs/fixtures/stage1_3/ilp_samples.txt` as reference, real exports via Dear ImGui).
- [ ] Seed Postgres tables with real run metadata using laptop automation (verify rows land in `indicator_datasets`, `walkforward_runs`, `walkforward_folds`, `simulation_runs`, `simulation_trades`, `simulation_trade_buckets`); confirm via `psql` sanity queries.
- [ ] Expose read-only Postgres credentials to the backend node using environment configuration (`backend/.env.production` or secrets manager).
- [ ] Mirror credential values into Dear ImGui runtime config so Stage 1.2 laptop automation can write to Postgres without manual prompts.
- [ ] Deploy updated `trading-dashboard` build that points to the forthcoming Drogon Stage 1.3 endpoints (feature flags or staging env until controllers ship).
- [ ] Log operational runbook steps (service restart commands, backup locations) in `docs/infra/frontend.md`.

# Backend Node Stage 1 TODO (Steps 1.1–1.3)

Host: `39.114.73.97` (vast.ai GPU instance; ephemeral). Runs Drogon services only, connecting remotely to QuestDB/Postgres on `45.85.147.236`.

## Stage 1.1 – Architecture & Alignment
- [ ] Pull the `backend/` directory from this repo onto the node; ensure CMake/Dependencies match the current branch.
- [ ] Document runtime prerequisites (CUDA version, compiler, Drogon build flags) in `backend/DEPLOY_INSTRUCTIONS.md`.
- [ ] Coordinate with frontend ops to obtain the QuestDB/Postgres connection details once provisioned; store them in a secrets file (`backend/.env`) excluded from git.

## Stage 1.2 – Schema & Data Pipeline Hardening
- [ ] Add SQL migration files in `backend/migrations/` mirroring `docs/fixtures/stage1_3/postgres_schema.sql` (include up/down scripts covering dynamic measurement columns and `simulation_trades`).
- [ ] Implement a lightweight migration runner or update existing startup scripts to apply migrations against the remote Postgres database.
- [ ] Extend exporter utilities/CLI (if any) to support automated validation pulls from QuestDB/Postgres for smoke testing.
- [ ] Write integration stubs/tests that mock remote DB responses to confirm schema assumptions (store under `backend/tests/`).

## Stage 1.3 – Backend Data Access Layer
- [ ] Implement Drogon controllers (`IndicatorController`, `WalkforwardController`, `TradeSimController`) consuming QuestDB/Postgres via connection pools, resolving measurement names from Postgres before querying QuestDB.
- [ ] Add DTOs and serialization helpers consistent with the frontend contract defined in `STAGE1_ALIGNMENT_RESULTS.md` (include dynamic measurement fields).
- [ ] Create integration tests (QuestDB REST + Postgres) driven by the fixtures in `docs/fixtures/stage1_3/` to cover dynamic measurement lookups and trade persistence.
- [ ] Provide a `scripts/seed_stage1_data.sh` that loads the fixtures into staging environments for repeatable tests.
- [ ] Update `backend/docs/api_stage1.md` and `.env.example` with the new endpoints, env vars, and usage instructions.

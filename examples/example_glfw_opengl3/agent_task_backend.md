# Backend Agent Task Brief (Stage 1.1–1.2)

## Context
- Host: `39.114.73.97` (vast.ai node with CUDA-enabled Drogon backend).
- Connected services: QuestDB/Postgres on `45.85.147.236`.
- Scope: implement storage schemas, API surfaces, and documentation updates required for Stage 1 data access.

## Primary Goals
1. Materialize the Postgres schema described in `STAGE1_ALIGNMENT_RESULTS.md` and supply migrations under source control.
2. Extend QuestDB documentation (`backend/QUESTDB_INGESTION.md`) with the new historical measurements once write access is available on this host.
3. Prototype Drogon controllers that expose read-only endpoints for indicator catalogs, walkforward runs, and trading simulations using sample data.

## Detailed Tasks
- **Database Migrations**
  - Create SQL migrations under `backend/migrations/` for:
    - `indicator_datasets`
    - `walkforward_runs`
    - `walkforward_folds`
    - `simulation_runs`
    - `simulation_trade_buckets`
  - Include indexes and foreign keys exactly as outlined in the Stage 1 alignment doc.
  - Provide rollback scripts and a migration README documenting `psql` commands and env vars.
- **QuestDB Documentation Update**
  - Copy the QuestDB measurement definitions from `STAGE1_ALIGNMENT_RESULTS.md` into `backend/QUESTDB_INGESTION.md`.
  - Clarify ILP tag usage, batching guidance, and cleanup strategy for reruns (`DELETE FROM ... WHERE run_id='...'`).
- **Drogon Controllers**
  - Add placeholder controllers (e.g., `IndicatorController`, `WalkforwardController`, `TradeSimController`) that return mock JSON shaped according to the frontend needs.
  - Define DTO structs mirroring the Postgres schema; use in-memory samples until actual DB connectivity is wired.
  - Document REST routes in `backend/docs/api_stage1.md` with request/response examples.
- **Integration Prep**
  - Create `.env.example` detailing Postgres DSN, QuestDB REST URL, and any secrets.
  - Update `backend/DEPLOY_INSTRUCTIONS.md` with Stage 1 configuration steps (firewall ports, systemd overrides).
  - Provide a Make/CMake target or script to run the new controllers locally (`./backend run --mock`).

## Deliverables to Attach
- SQL migration files + README.
- Updated `backend/QUESTDB_INGESTION.md`.
- New controller source files and route registration.
- API documentation with sample payloads.
- `.env.example` for Stage 1 deployment.

## Verification Checklist
- `[ ]` Migrations apply cleanly against a local/staging Postgres (`psql -f` or migration runner).
- `[ ]` Drogan app compiles with new controllers enabled.
- `[ ]` Mock endpoints respond with JSON matching frontend expectations (use `curl` smoke tests).
- `[ ]` Documentation references `STAGE1_ALIGNMENT_RESULTS.md` for schema alignment.
- `[ ]` Configuration files avoid embedding real credentials (placeholders only).

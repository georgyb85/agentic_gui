# Backend Agent Task Brief (Stage 1.3)

## Context
- Host: `39.114.73.97` (CUDA-enabled Drogon node).
- Dependencies: QuestDB/Postgres on `45.85.147.236`, Stage 1.3 fixtures from the laptop workspace.
- Objective: deliver the Stage 1.3 backend data access layer defined in `ONLINE_TRADING_SYSTEM_PLAN.md`.

## Primary Goals
1. Implement production-ready Drogon controllers for indicators, walkforward runs, and trading simulations backed by QuestDB/Postgres.
2. Provide resilient data-access primitives (connection pooling, retries, pagination) with integration coverage.
3. Publish API contracts and deployment notes for the frontend and ops teams.

## Detailed Tasks
- **Data Access Layer**
  - Wrap QuestDB REST/ILP endpoints using asynchronous HTTP clients and Arrow decoders; expose helpers for ranged queries (`dataset`, `run_id`, date span).
  - Configure libpq-based Postgres pool with configurable limits (env vars), exponential backoff, and health checks.
  - Translate QuestDB/Postgres rows into shared DTO structs under `backend/include/` for reuse by controllers and tests.
- **Controller Implementation**
  - Create `IndicatorController`, `WalkforwardController`, and `TradeSimController` with routes:
    - `GET /api/datasets` + `/api/datasets/{id}` (catalog + latest timestamp summary).
    - `GET /api/datasets/{id}/series` (paginated time-series slice with histogram bins).
    - `GET /api/walkforward/runs` + `/api/walkforward/runs/{id}` + `/api/walkforward/runs/{id}/predictions`.
    - `GET /api/tradesim/runs` + `/api/tradesim/runs/{id}` + `/api/tradesim/runs/{id}/trades`.
  - Accept query params for pagination (`limit`, `offset`), date bounding (`start_ts`, `end_ts`), and fold/run filters.
  - Serialize responses using `nlohmann::json` (existing dependency) with camelCase keys matching frontend expectations.
- **Testing & Tooling**
  - Add integration tests under `backend/tests/` that spin up QuestDB/Postgres test containers (or mocks) seeded with laptop fixtures; validate status codes, payload shapes, and pagination.
  - Provide `scripts/seed_stage1_3_data.sh` to load fixtures into QuestDB/Postgres using ILP/psql.
  - Extend `run_tests.sh` to execute the new integration suite and lint controllers.
- **Documentation & Ops**
  - Update `backend/docs/api_stage1.md` with request/response examples, schema notes, and curl probes (reuse laptop fixtures).
  - Refresh `backend/DEPLOY_INSTRUCTIONS.md` with new env vars (`POSTGRES_POOL_SIZE`, `QUESTDB_BASE_URL`, `ARROW_BATCH_BYTES`) and firewall requirements.
  - Ship `.env.example` entries for all configuration points introduced in Stage 1.3.

## Deliverables to Attach
- New controller source/header files with route registration.
- Data access layer modules and DTO definitions.
- Integration tests + seeding scripts leveraging Stage 1.3 fixtures.
- Updated documentation (`api_stage1.md`, `DEPLOY_INSTRUCTIONS.md`, `.env.example`).

## Verification Checklist
- `[ ]` `ctest`/`run_tests.sh` passes with QuestDB/Postgres integration tests enabled.
- `[ ]` Controllers return real data when pointed at the staging QuestDB/Postgres instances.
- `[ ]` Pagination and filtering behave deterministically (validated via scripted curl or Postman collection).
- `[ ]` API docs reflect the final response schemas consumed by the frontend.
- `[ ]` Deployment guide and env templates cover all new variables and defaults.

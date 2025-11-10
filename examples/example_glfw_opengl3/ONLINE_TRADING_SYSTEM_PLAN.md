# Distributed Trading Platform Roadmap

Goal: transition the desktop-first Dear ImGui workflow into a web-delivered analytics and simulation stack. Live market ingestion/trading is intentionally out of scope; the focus is historical data replay, walkforward training, and trading simulations surfaced through the frontend. The roadmap is split into two stages aligned with your deployment milestones.

## Environment Roles (Stage 1 Baseline)
- **Laptop (this repo):** owns raw OHLCV/indicator files, Dear ImGui exporters, and serves as the source of truth for historical exports. The app dynamically names QuestDB measurements (e.g., `btc25_1`, `btc25_run3`) when exporting datasets or walkforward outputs. No long-lived databases run locally.
- **Frontend server – `45.85.147.236`:** persistent host for QuestDB, Postgres (database+user still to be created), and the production `trading-dashboard` web app. QuestDB schemas are materialised as soon as the laptop exports land; Postgres captures run/simulation metadata and hyperparameters pushed from the laptop.
- **Backend GPU node – `39.114.73.97` (ephemeral vast.ai):** runs the Drogon service layer. Hosts have no resident databases; they connect remotely to QuestDB/Postgres on the frontend server using credentials provisioned there and must handle dynamic measurement names supplied at runtime.

## Stage 1 – Shared Data Contract & Read/Visualize Experience
*Outcome:* consistent schemas across QuestDB/Postgres, reliable export tooling from the laptop workflow, backend endpoints that surface historical runs, and a frontend that renders those artifacts with production data instead of stubs.

### 1.1 Architecture & Alignment (Week 0-1)
- **Deliverables:** ratified dataflow diagrams, glossary, prioritized backlog, measurement naming guide.
- Document the end-to-end path from loading `btc25.txt`/indicator CSVs in Dear ImGui → QuestDB measurement creation → automatic Postgres inserts → Drogon → trading-dashboard; include timezone handling and the impact of indicator lookback windows.
- Define naming conventions for dynamically created QuestDB measurements (`<dataset>_<version>` for indicators, `<dataset>_run<n>` for walkforward predictions, `<dataset>_sim<n>` for trading simulations) and capture how those IDs map to Postgres records.
- Inventory current stubbed datasets in the frontend and map each to its future QuestDB measurement and Postgres table entry.
- Capture environment assumptions (laptop exporters, frontend DB host, backend controller host) in `backend/DEPLOY_INSTRUCTIONS.md` and list pending credentials/secrets.

### 1.2 Schema & Data Pipeline Hardening (Week 1-3)
- **Deliverables:** finalized QuestDB & Postgres schemas; Dear ImGui exporters auto-persist metadata; validation playbook.
- Lock Postgres table layouts (indicator datasets, walkforward runs, per-fold metrics, simulation runs, simulation trades) and document them in `backend/QUESTDB_INGESTION.md` + companion Postgres schema note. Backend repo owns the migrations, but they are executed against the Postgres instance on `45.85.147.236`.
- Update the Dear ImGui walkforward and trading simulation flows so that:
  - Indicator exports call existing QuestDB ILP writers while capturing the generated measurement name and column list.
  - Walkforward runs automatically write configuration, hyperparameters, and the QuestDB measurement used for predictions into Postgres when the run finishes.
  - Trading simulations automatically persist their inputs (selected run, thresholds, sizing) and outputs (performance metrics, per-trade rows) into Postgres, and optionally push trade series to QuestDB when requested.
- Build regression checks that re-import a sample slice from QuestDB/Postgres and diff against source CSV/Arrow data.
- Stage historical backfills (representative instruments/time ranges) into QuestDB/Postgres on the frontend server so frontend work can rely on real payloads.

### 1.3 Backend Data Access Layer (Week 2-4)
- **Deliverables:** Drogon controllers serving indicators, walkforward runs, and trading simulations with dynamic measurement awareness; API contract shared with frontend.
- Review existing controllers; define REST/JSON schemas (or Arrow payloads) that surface:
  - Indicator catalog entries including their dynamically generated QuestDB measurement name, column metadata, and sample ranges.
  - Walkforward run metadata (hyperparams, folds, metrics), plus the QuestDB measurement holding the predictions/thresholds for download.
  - Trading simulation metadata plus Postgres-backed trade lists and performance metrics; expose optional QuestDB measurement IDs if trade series were exported there.
- Implement QuestDB adapters capable of streaming large Arrow batches from the dynamic measurement names; add pagination/query filters (date range, indicator set, run_id). Remote connections point to `45.85.147.236`.
- Wire Postgres access layer for run/simulation metadata; include connection pooling and retry/backoff policies against the frontend-hosted database.
- Cover endpoints with integration tests in `run_tests.sh`, mocking QuestDB/Postgres where needed.

### 1.4 Frontend Integration & Visualization (Week 3-5)
- **Deliverables:** trading-dashboard views backed by live backend APIs (no stubs).
- Replace stub services with strongly typed API clients targeting the new Drogon endpoints.
- Update indicator explorer UI to render actual data (time-series charts, histogram overlays); handle empty/partial datasets gracefully.
- Implement walkforward and trading simulation browsers:
  - List runs with key metrics and hyperparameters.
  - Provide detail views with fold-by-fold stats, threshold tables, and download links.
  - Render simulation outcomes (P&L curves, drawdowns, trade tables) sourced from QuestDB/Postgres.
- Capture QA scripts for manual validation (which datasets to load, expected chart behaviors) in `docs/qa/`.

### 1.5 Release Readiness & Observability (Week 4-6)
- **Deliverables:** deployable stack for Stage 1, minimal monitoring, documentation.
- Produce Docker Compose (or local runbook) enabling QuestDB, Postgres, Drogon, and frontend to run together for demos.
- Set up basic logging/metrics (request logs, schema version tags) to ease debugging during Stage 2.
- Update `_SUMMARY.md` notes to reflect new dataflows and UI surfaces.
- Conduct an end-to-end walkthrough (export from laptop → backend ingest → frontend visualization) and record findings for Stage 2 prep.

## Stage 2 – Backend-Driven Training & Simulation Workloads
*Outcome:* eliminate the laptop dependency for compute by sourcing indicators directly from QuestDB, running walkforward and trading simulations server-side with CUDA acceleration, and exposing orchestration/monitoring via backend controllers and the frontend.

### 2.1 Training Pipeline Extraction (Week 6-8)
- **Deliverables:** shared library encapsulating walkforward logic; containerized GPU environment.
- Refactor walkforward code paths from the Dear ImGui app into a reusable module callable from both desktop and backend.
- Package CUDA/XGBoost/Arrow dependencies into a container image; ensure compatibility with QuestDB Arrow exports.
- Implement dataset loaders that pull features/targets exclusively from QuestDB (no local CSV dependency).

### 2.2 Backend Job Orchestration (Week 7-9)
- **Deliverables:** Drogon-controlled job intake and execution pipeline; run artifacts persisted automatically.
- Add API for submitting walkforward jobs (hyperparameters, dataset selectors, feature/target choices, cross-validation config).
- Introduce a lightweight job queue/executor (Redis, internal task runner) to manage GPU workload scheduling.
- Persist run lifecycle states (QUEUED/RUNNING/FAILED/COMPLETED) and logs to Postgres; surface status endpoints for frontend polling.
- Ensure predictions/thresholds are written back to QuestDB with consistent partitioning, and metadata/metrics land in Postgres.

### 2.3 Trading Simulation Service (Week 8-10)
- **Deliverables:** backend-only trading simulator consuming QuestDB/Postgres data; output persisted with lineage.
- Port `TradeSimulator` logic to run headless, driven via JSON parameters from a Drogon controller.
- Load predictions/thresholds and OHLCV directly from QuestDB; handle caching/batching to keep throughput high.
- Persist simulation configs, P&L curves, and trade-level outcomes to QuestDB/Postgres; link to originating run_id/simulation_id.
- Build parity tests comparing backend simulations against legacy desktop runs to validate correctness.

### 2.4 Frontend Enhancements for Backend Runs (Week 9-11)
- **Deliverables:** UI to launch walkforward/simulation jobs and monitor progress, backed by live APIs.
- Add job creation forms mirroring the desktop configuration experience, including validation for feature/target selection and hyperparameters.
- Display job queues, real-time status, and links to logs; provide buttons to download artifacts or re-run with tweaks.
- Extend simulation panels with controls for backend execution, parameter presets, and result comparisons (new vs historical runs).

### 2.5 Deployment, Monitoring & Hardening (Week 10-12)
- **Deliverables:** production-ready stack for backend-driven workloads; incident response basics.
- Expand Docker Compose/Helm to include GPU-capable worker services; document provisioning requirements.
- Instrument training/simulation pipelines with metrics (job duration, GPU utilization, error counts) and alerts.
- Establish backup/restore procedures for QuestDB/Postgres datasets created by backend jobs.
- Run load tests on critical endpoints (job submission, data retrieval) and tune limits/timeouts accordingly.

### 2.6 Rollout & Continuous Improvement (Week 12+)
- **Deliverables:** staged launch plan; feedback loop; maintenance cadence.
- Pilot Stage 2 with select users, comparing backend results to prior laptop-driven workflows; collect gap list.
- Publish migration guide highlighting the deprecation plan for desktop-managed exports.
- Schedule recurring reviews to prioritize additional features (e.g., automated hyperparameter sweeps, reporting exports) while ensuring ongoing system health.

## Ongoing Practices
- Keep a unified backlog reflecting Stage 1/Stage 2 stories, owners, and dependencies (Linear/Jira).
- Update `_SUMMARY.md` and relevant architecture docs whenever new flows or controllers go live.
- Maintain weekly syncs between data, backend, and frontend owners to track schema changes and deployment readiness.
- Version SQL migrations and QuestDB table definitions; include checksum verification in release checklists.

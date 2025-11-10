# Frontend Agent Task Brief (Stage 1.3)

## Context
- Host: frontend server `45.85.147.236` (trading-dashboard deployment alongside QuestDB/Postgres).
- Dependencies: Stage 1.3 Drogon APIs served from `39.114.73.97`, dataset fixtures supplied from the laptop repo.
- Mission: consume the new backend data layer, remove mocks, and harden UI workflows for Stage 1 release.

## Primary Goals
1. Implement a typed client SDK targeting the Stage 1.3 Drogon routes with retry, caching, and error normalization.
2. Replace mock data across walkforward, trade simulation, and indicator explorer flows with live API payloads.
3. Add QA tooling and documentation so operators can verify data integrity post-deploy.

## Detailed Tasks
- **Client & State Management**
  - Build `src/lib/api/client.ts` encapsulating fetch logic, base URL selection (env), auth headers if introduced, and standard error objects.
  - Define shared TypeScript interfaces mirroring backend DTOs (`DatasetSummary`, `WalkforwardRun`, `TradeSimRun`, etc.) and publish via `src/types/stage1_3.ts`.
  - Integrate React Query (preferred) or SWR for caching/prefetching; configure sensible stale times per route.
- **UI Wiring**
  - **Indicator Explorer:** hydrate datasets list, allow selecting date range, fetch paginated series, render histograms from backend-provided bins.
  - **Walkforward Dashboard:** load runs, switch folds, surface predictions/threshold downloads, and provide empty-state messaging based on HTTP 204/404.
  - **Trade Simulation Views:** fetch run catalog and trades table, render P&L/equity charts using backend series, handle long/short mode filters.
  - Remove all `mock*` helpers and fallback JSON; ensure code paths tolerate delayed backend availability via skeleton/loading components.
- **Tooling & QA**
  - Create `scripts/verifyStage1_3.ts` that hits every API route, validates schema (using `zod` or io-ts), and prints a colorized summary; wire to `npm run verify-stage1_3`.
  - Document manual QA checklist in `docs/qa/frontend_stage1_3.md` covering dataset selection, run drill-downs, error states, and performance expectations.
  - Add Cypress (or Playwright) smoke tests for critical flows; run in CI with mocked responses and optional live backend toggle.
- **Configuration & Deployment**
  - Update `.env.example`, `.env.production`, and deployment manifests with `VITE_STAGE1_BACKEND_URL` (or equivalent).
  - Ensure build artifacts exclude sensitive env vars; document how ops can override endpoints via environment configuration.
  - Capture rollout steps (clear caches, redeploy static assets) in `docs/releases/STAGE1_3_FRONTEND.md`.

## Deliverables to Attach
- API client module, types, and React Query hooks.
- Updated React components free of mock data, plus new loading/error UI.
- QA script, documentation, and optional end-to-end tests.
- Environment templates and release note for Stage 1.3 deployment.

## Verification Checklist
- `[ ]` `npm run verify-stage1_3` reports success (or clearly flags missing backend functionality).
- `[ ]` Walkforward, Trade Simulation, and Indicator views render real data end-to-end against the staging backend.
- `[ ]` Automated smoke tests pass in CI/CD.
- `[ ]` Documentation links to backend API references and laptop-provided fixtures.
- `[ ]` Environment templates and rollout notes reflect the final deployment topology.

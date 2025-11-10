# Frontend Agent Task Brief (Stage 1.1–1.2)

## Context
- Host: frontend server `45.85.147.236` (serves trading-dashboard, houses QuestDB/Postgres).
- Goal: replace mock UI data with API-driven content aligned with the Stage 1 backend contracts.

## Primary Goals
1. Implement API client layer pointing at Drogon endpoints defined in the Stage 1 backend plan.
2. Swap out mock data across Walkforward, Trade Simulation, and LFS apps.
3. Add validation tooling to verify data integrity and highlight missing backend fields.

## Detailed Tasks
- **API Client Setup**
  - Create `src/lib/api.ts` (or similar) exporting typed fetch helpers for:
    - `GET /api/datasets`
    - `GET /api/datasets/{id}/features`
    - `GET /api/walkforward/runs` + `GET /api/walkforward/runs/{id}`
    - `GET /api/tradesim/runs` + `GET /api/tradesim/runs/{id}/trades`
  - Types should align with the Postgres schema (`indicator_datasets`, `walkforward_runs`, etc.).
  - Include retry + error normalization (toast-friendly messages).
- **UI Integration**
  - **Walkforward Dashboard (`src/apps/walkforward/index.tsx`):**
    - Remove `generateMock*` helpers.
    - Fetch run list on load, support run switching, display fold metrics from API.
    - Provide loading and error states.
  - **Trade Simulation App (`src/apps/tradesim/components/TradeTable.tsx` et al.):**
    - Replace `mockTrades` with API-provided trade rows.
    - Add controls to choose `simulation_id` and refresh data.
  - **LFS App (`src/apps/lfs/index.tsx`):**
    - Populate feature/target selectors via `GET /api/datasets/{id}/features|targets`.
    - Wire the “Run Analysis” button to POST selected payload to backend (stub if backend not ready).
- **State Management & Caching**
  - Use React Query/Zustand or lightweight caching to avoid redundant network calls.
  - Persist last-selected dataset/run in local storage.
- **Validation & QA**
  - Add `npm run verify-stage1` script that hits each endpoint (via node script) and prints status summaries.
  - Document manual QA steps in `docs/qa/frontend_stage1.md`.
- **Configuration**
  - Update `.env.production` / `.env.local` templates with API base URL.
  - Ensure QuestDB/Postgres credentials are not exposed in the frontend bundle (backend proxies handle auth).

## Deliverables to Attach
- API client module and updated React components.
- QA script (`scripts/verifyStage1.ts`) with README instructions.
- Documentation (`docs/qa/frontend_stage1.md`) describing test flows and expected visuals.
- Updated env templates referencing backend host.

## Verification Checklist
- `[ ]` `npm run build` succeeds with API clients enabled.
- `[ ]` Walkforward dashboard renders live data (confirmed via browser/network tab).
- `[ ]` Trade simulation table displays rows returned by backend endpoint.
- `[ ]` LFS selectors populate from API and handle empty datasets gracefully.
- `[ ]` QA script reports success for all Stage 1 endpoints (or clearly flags missing backend fields).

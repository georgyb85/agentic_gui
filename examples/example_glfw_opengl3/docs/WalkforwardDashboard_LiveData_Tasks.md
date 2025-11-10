# Walkforward Dashboard – Live Stage1 Integration Tasks

The `trading-dashboard/src/apps/walkforward` view currently renders mock runs and folds.  
To ship a production-ready “Trading Control Panel” that mirrors the desktop app we need to wire it to the Stage1 Drogon API (`https://agenticresearch.info`).  
This document breaks the work down into discrete tasks for the frontend team.

## 1. API client & environment wiring
1. Add `VITE_STAGE1_API_BASE_URL` (default to `https://agenticresearch.info`) in `vite.config.ts` and `src/lib/config.ts`.
2. Create `src/lib/stage1/client.ts` exporting a thin wrapper over `fetch`/`axios`, handling:
   - Base URL + optional `X-Stage1-Token` header.
   - JSON parsing, error normalization, and request logging (only in dev).
3. Define TypeScript models for the Stage1 entities used by the dashboard:
   ```ts
   export interface Stage1DatasetSummary {
     dataset_id: string;
     dataset_slug: string;
     symbol: string;
     ohlcv_measurement: string;
     indicator_measurement: string;
     ohlcv_row_count: number;
     indicator_row_count: number;
     updated_at: string;
   }

   export interface Stage1RunSummary {
     run_id: string;
     dataset_id: string;
     dataset_slug: string;
     prediction_measurement: string;
     status: string;
     started_at: string | null;
     completed_at: string | null;
   }

   export interface Stage1FoldMetrics {
     fold_number: number;
     train_start_idx: number;
     train_end_idx: number;
     test_start_idx: number;
     test_end_idx: number;
     samples_train: number;
     samples_test: number;
     best_iteration: number | null;
     best_score: number | null;
     thresholds: Record<string, number>;
     metrics: Record<string, number>;
   }

   export interface Stage1RunDetail extends Stage1RunSummary {
     target_column: string;
     feature_columns: string[];
     hyperparameters: Record<string, unknown>;
     walk_config: Record<string, unknown>;
     summary_metrics: Record<string, unknown>;
     folds: Stage1FoldMetrics[];
   }
   ```
4. Surface typed fetch helpers:
   - `listDatasets(limit = 100, offset = 0)`
   - `listRuns(datasetId, limit = 200, offset = 0)`
   - `getRun(runId)`
   - `getRunPredictions(runId, format = "json")` (optional for future trade-sim seeding).

## 2. React Query hooks
Create hooks in `src/apps/walkforward/lib/hooks.ts`:
1. `useStage1Datasets()` → `useQuery(["stage1", "datasets"], listDatasets)`.
2. `useStage1Runs(datasetId)` → dependent query returning summaries.
3. `useStage1RunDetail(runId)` → fetches the selected run + folds.
4. Provide `useStage1Predictions(runId, enabled)` for cumulative chart once QuestDB data is required.

Each hook should return `data`, `isLoading`, `error`, and `refetch`. Bubble errors via shadcn toasts.

## 3. Dataset & run selection UI
1. Add a dataset selector to the Simulation header (`SimulationHeader.tsx`):
   - Replace the static model badge area with a `<Select>` listing Stage1 datasets (slug + symbol + row counts).
   - Persist the chosen dataset in local component state (or zustand if shared).
   - Show a shimmer/loading indicator while datasets load.
2. Add a “Load Saved Run…” button next to “Start Simulation”. Clicking opens a modal (reusing shadcn `Dialog`) that mirrors the desktop behaviour:
   - On open, call `useStage1Runs(selectedDatasetId)`; render a paginated table with measurement, status, started/completed timestamps.
   - Row selection enables the “Load Run” button; hitting it triggers `useStage1RunDetail`.
   - Show inline errors when API calls fail (“Select a dataset first”, HTTP errors, etc.)

## 4. Replace mock state with live data
Update `WalkforwardDashboard` (`src/apps/walkforward/index.tsx`):
1. Remove the `useEffect` that seeds `generateMock*` data. Instead:
   - Maintain a `loadedRuns` array of `Stage1RunDetail`.
   - When the modal returns a run, insert it (avoid duplicates) and set `activeRun` to its index.
2. Remove the fake progress interval; for now, disable “Start Simulation” (or keep it but clearly mark as “demo only”) until we implement actual training orchestration.
3. `chartData`: build the cumulative series from fold metrics:
   ```ts
   const chartData = run.folds.map((fold) => ({
     fold: fold.fold_number,
     runningLong: fold.metrics.running_sum ?? 0,
     runningDual: fold.metrics.running_sum_dual ?? fold.metrics.running_sum ?? 0,
     runningShort: fold.metrics.running_sum_short ?? 0,
   }));
   ```
   Update `PerformanceChart` to read these keys instead of `Run 1/Run 2`.
4. `summaryData`: derive stats (PF, hit rate, signal counts) from `fold.metrics`. Example:
   ```ts
   const signals = fold.metrics.n_signals ?? 0;
   aggregate.return += fold.metrics.sum ?? 0;
   ```
   Document any metric gaps so backend can extend the payload if needed.
5. `RunDetails` should consume the Stage1 shape:
   - Replace the current `RunDetail` interface with the API model.
   - When rendering the fold table, map Stage1 fields (`samples_train`, `metrics.hit_rate`, `thresholds.long_optimal`, etc.).
   - Update `copyFeatures`/`copyHyperparameters` to use Stage1 `feature_columns` & `hyperparameters`.
6. `FoldConfigPanel` and `FoldResults` should display the selected fold’s stage1 ranges:
   - Train/test windows → use `train_start_idx`/`train_end_idx` converted to strings.
   - Threshold inputs → use `thresholds.prediction_scaled`, etc.

## 5. Loading & error states
1. Add skeleton placeholders (e.g., shadcn `Skeleton`) for:
   - Dataset selector (header)
   - Runs table when the modal opens
   - Run details area while a run is being hydrated
2. Toast errors from hooks; show actionable messaging (e.g., “Stage1 API unreachable – check VPN?”).
3. Disable “Load Run” button while `useStage1RunDetail` is in-flight; show spinner.

## 6. Optional enhancements / stretch goals
1. Persist last-selected dataset & run in `localStorage` so the dashboard restores state after refresh.
2. After loading a run, allow the user to download the Stage1 JSON via “Export Config” (call `getRun` and stringify).
3. Expose a “View predictions” button that calls `/api/runs/{id}/predictions?format=json` and overlays the predicted vs target series in the chart (future integration with trade sim).

## Deliverables
1. Updated Walkforward dashboard (loads real datasets/runs/folds, no mocks).
2. API client + hooks with unit tests (mocking fetch).
3. Screenshots / short video showing:
   - Dataset dropdown populated from Stage1.
   - Load Run modal listing runs.
   - Fold table + chart populated with real data.
4. Documentation update (`README.md` or `docs/frontend_walkforward.md`) describing env vars and how to test with the live backend.

Once these steps are complete, the web “Trading Control Panel” will match the desktop walkforward window feature-for-feature while reading from the single source of truth (Stage1 Postgres + QuestDB via Drogon).

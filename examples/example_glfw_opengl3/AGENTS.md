# Repository Guidelines

## Project Structure & Module Organization
`main.cpp` initializes Dear ImGui/ImPlot and routes to feature windows housed beside their headers (for example `TimeSeriesWindow.cpp`, `TradeSimulationWindow.cpp`). Data preparation utilities live in `analytics_dataframe.cpp`, `chronosflow.cpp`, and `feature_utils.cpp`. The simulation stack resides in `simulation/` with core engines (`SimulationEngine.cpp`), model adapters (`simulation/models/`), and UI panels (`simulation/ui/`). Advanced selection and statistical tooling belong to `stepwise/`, while third-party math support is vendored in `eigen-3.4.0/`. Update the `*_SUMMARY.md` notes when you introduce major flows.

## Build, Test, and Development Commands
`make` builds the native GLFW/OpenGL binary; the output `./example_glfw_opengl3` appears in the repo root. `make clean` removes objects before switching compilers or feature flags. `make -f Makefile.emscripten all` targets WebAssembly once the emsdk environment variables are active. Windows contributors can use `build_win32.bat` or open `example_glfw_opengl3.vcxproj` in Visual Studio. After each change, run `./example_glfw_opengl3` and walk through the panels you touched.

## Coding Style & Naming Conventions
Stick with C++11, four-space indentation, and brace-on-same-line declarations (`void Foo() {`). Maintain PascalCase for types, camelCase for methods, and the existing `m_` prefix for mutable members (`m_current_position`). Prefer `const auto&` for read-only loops, early guard returns, and focused `//` comments that explain rationale. Place local project headers before STL and third-party includes, mirroring current files.

## Testing Guidelines
No automated harness exists; validate by launching the app and replaying representative data. Stage deterministic fixtures in `simulation/` or `stepwise/` when adding analytics, and document the expected location in the owning class. If you introduce unit tests (for example extending `xgboost_cache_test.cpp`), expose them through a `make test` target so collaborators can run `make test` before review.

## Commit & Pull Request Guidelines
Match the existing history style: `Component: concise summary (#issue)` with subjects under 72 characters. Expand in the body with motivation, key code touchpoints, and roll-out notes. Pull requests must list the commands executed (`make`, runtime verification), link to relevant issues, and attach screenshots or clips for UI changes. Call out new datasets, config files, or environment variables so teammates can reproduce the build.

## Environment & Configuration Tips
Install system packages for GLFW, OpenGL, `pkg-config`, and libcurl to satisfy the Makefile’s linker flags; document extra steps if your platform differs. Arrow support (`#include <arrow/compute/api.h>`) assumes the Arrow SDK is available—note custom build paths in your PR if required. The workspace writes user layouts to `imgui.ini`; avoid committing personalized state.

## System Overview

### Desktop Dear ImGui Application (`example_glfw_opengl3`)
- Serves as the primary research and authoring tool: users load OHLCV + indicator datasets from local TSSB/CSV sources, run walk-forward simulations, inspect fold-by-fold stats, and export artifacts.
- Windows of interest:
  - `TimeSeriesWindow` / `candlestick_chart` for exploring raw data.
  - `TradeSimulationWindow` + `SimulationWindowNew` for configuring walk-forward runs and test-model flows.
  - `Stage1DatasetManager` for exporting local data to the Stage1 backend (writes manifests, posts metadata via REST, and now streams OHLCV/indicator rows through Drogon append endpoints).
- Uses `Stage1MetadataWriter` + `Stage1RestClient` to register datasets/runs/simulations remotely while spooling SQL fallbacks under `docs/fixtures/stage1_3`.
- Environment knobs:
  - `STAGE1_ENABLE_EXPORTS` (default `1`) toggles Stage1 REST uploads.
  - `STAGE1_DIRECT_DB_EXPORTS=1` re-enables legacy direct Postgres writes.
  - `STAGE1_API_TOKEN` supplies the Drogon auth header when the server’s `AuthFilter` is on.

### Stage1 Drogon Backend (`stage1-drogon-backend/stage1_server`)
- Runs on the database node (no GPU). Responsibilities:
  - HTTP API for dataset manifests, QuestDB table proxies, run metadata, and index maps.
  - Controllers: `/api/datasets` (CRUD + manifest), `/api/datasets/{id}/ohlcv|indicators` for ranged fetch, `/api/datasets/{id}/ohlcv|indicators/append` for ingestion, `/api/runs`, `/api/simulations`, QuestDB proxy endpoints, and async jobs.
  - Persists metadata to PostgreSQL (`datasets`, `walkforward_runs`, `walkforward_folds`, etc.) and bar/indicator rows to QuestDB (monthly partitions recommended).
- Services layer (`services/*.cc`) wraps ORM mappers plus `QuestDbService`.
- Authentication is optional; if `auth.enable_auth` is true in `config.json`, every request must include `X-Stage1-Token` which the desktop/other clients source from `STAGE1_API_TOKEN`.
- Typical deployment commands live in `deploy.sh` / systemd service `stage1_drogon.service`.

### Kraken Trading Server (GPU Node)
- Codebase: `kraken-trading-system` (peer repo checked into this workspace for reference).
- GPU-resident Drogon app that exposes the `/xgboost` WebSocket controller:
  - Accepts dataset slice requests (train/validation/test windows) referencing Stage1 dataset IDs.
  - Uses `Stage1DatasetClient` to pull aligned OHLCV + indicator data via Stage1 REST APIs, normalizes timestamps, and feeds them into the XGBoost CUDA training path.
  - Maintains WebSocket sessions with reconnection support, training queues, and GPU memory guards (per architecture doc `XGBOOST_GPU_WEBSOCKET_ARCHITECTURE.md`).
  - Emits live training metrics, thresholds, and UBJ model artifacts for downstream consumption.
- Shares schemas/contracts with the Stage1 backend so inference results can be reconciled with exported walk-forward runs.

### Trading Dashboard Frontend (`trading-dashboard`)
- React/TypeScript SPA deployed separately (static hosting). Key features:
  - Walkforward run explorer mirroring the ImGui “Test Model” tab: load dataset runs from Stage1, inspect folds, kick off “Train + Test” via the Kraken `/xgboost` WebSocket endpoint.
  - Datasets panel pulls from Stage1 REST to surface metadata, indicator column names, and timestamp ranges.
  - Uses the same token/URL configuration as other components (`VITE_STAGE1_API_BASE_URL`, `VITE_KRAKEN_WS_URL`, optional `VITE_STAGE1_API_TOKEN`).
- Expectation: after the desktop exports a dataset and Kraken deploys a model endpoint, the dashboard can load the run, examine folds, and invoke live training to mirror the on-desktop experience.

### Data Flow Summary
1. Researcher loads raw TSSB/CSV data in the ImGui app, engineers indicators, and exports the dataset via `Stage1DatasetManager`.
2. Stage1 backend ingests the metadata + bar/indicator rows (Postgres + QuestDB), exposing them through `/api/datasets/*`.
3. Kraken GPU server fetches slices from Stage1 when clients request training or inference over WebSockets, returning metrics + model artifacts.
4. Trading Dashboard consumes both Stage1 HTTP endpoints (for datasets/runs) and Kraken WebSocket messages to let users inspect folds, trigger training, and visualize results in the browser.

Keep these relationships documented whenever you add new endpoints or change contracts so every agent (desktop, Stage1, Kraken, frontend) stays in sync.

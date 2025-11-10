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

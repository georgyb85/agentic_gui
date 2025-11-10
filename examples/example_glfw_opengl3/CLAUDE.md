# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a financial analysis and trading simulation application built on top of Dear ImGui's GLFW+OpenGL3 example. The project extends the basic ImGui example with sophisticated financial visualization and analysis tools.

## Build System

**Primary Build Method**: Visual Studio (MSVC) on Windows
- Solution file: `example_glfw_opengl3.vcxproj`
- Platform toolset: v143 (Visual Studio 2022)
- Build configurations: Debug/Release for x64

**Alternative Build**: Makefile for Linux/Mac
- Build command: `make`
- Clean command: `make clean`
- Dependencies: GLFW3, OpenGL, libcurl

## Core Architecture

### Main Components

1. **main.cpp** - Application entry point with ImGui/ImPlot initialization and main render loop
2. **CandlestickChart** (`candlestick_chart.h/cpp`) - OHLCV candlestick chart visualization with interactive features
3. **TimeSeriesWindow** (`TimeSeriesWindow.h/cpp`) - Time series data management and visualization using AnalyticsDataFrame
4. **NewsWindow** (`NewsWindow.h/cpp`) - Financial news display component
5. **HistogramWindow** (`HistogramWindow.h/cpp`) - Statistical histogram visualization
6. **BivarAnalysisWidget** (`BivarAnalysisWidget.h/cpp`) - Bivariate analysis tools
7. **SimulationWindow** (`SimulationWindow.h/cpp`, `SimulationWindow_XGBoost.cpp`) - Trading simulation with XGBoost integration

### Data Management

- **AnalyticsDataFrame** (`analytics_dataframe.h/cpp`) - Core data structure for time series data
- **OhlcvData** (`ohlcv_data.h`) - OHLCV data management
- **TimeSeries** (`TimeSeries.h`) - Time series data structure
- **chronosflow** namespace - Advanced time series processing utilities

### Visualization

- **ImPlot** integration for advanced plotting
- Custom plotters in `implot_custom_plotters.h/cpp`
- Candlestick chart rendering with tooltip support

### Utilities

- **utils.h/cpp** - Common utilities including data fetching functions
- **feature_utils.h/cpp** - Feature engineering utilities
- **tssb_timestamp.h/cpp** - Timestamp handling
- **dataframe_io.h/cpp** - Data I/O operations

## Key Dependencies

- **Dear ImGui** - Core UI framework (parent directory `../..`)
- **ImPlot** - Plotting extension for ImGui
- **GLFW3** - Window/input handling
- **OpenGL3** - Graphics rendering
- **libcurl** - HTTP requests for data fetching
- **XGBoost** - Machine learning for trading simulation (Release builds)
- **Apache Arrow** - Data processing (visible in Debug/Release folders)

## Development Guidelines

### Window Management Pattern
All custom windows follow this pattern:
- `Draw()` method for rendering
- `IsVisible()`/`SetVisible()` for visibility control
- Windows are instantiated as static objects in main.cpp
- Inter-window communication via setter methods (e.g., `SetHistogramWindow()`)

### Data Flow
1. Data fetched via HTTP using libcurl (see `utils.cpp`)
2. Stored in AnalyticsDataFrame or OhlcvData structures
3. Processed through chronosflow utilities
4. Visualized using ImPlot

### Adding New Features
- New windows should follow the existing window pattern
- Add to main.cpp instantiation and menu system
- Use existing data structures (AnalyticsDataFrame) for consistency
- Include in Makefile SOURCES list and vcxproj file filters

## Important Files to Review

When making changes, always check:
1. `main.cpp` - For window instantiation and menu integration
2. `Makefile` - For source file list when adding new files
3. `example_glfw_opengl3.vcxproj` and `.vcxproj.filters` - For MSVC build inclusion
4. Header dependencies in the specific component being modified

## Common Tasks

### Running the Application
- Build in Visual Studio (Debug or Release configuration)
- Executable location: `Debug/` or `Release/` folder
- Application title: "Agentic Strategy Research"

### Debugging
- Check `simple_logger.h` for logging utilities
- ImGui Demo Window and ImPlot Demo available from menu
- Anti-aliasing enabled (4x MSAA)
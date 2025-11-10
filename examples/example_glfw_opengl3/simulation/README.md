# SimulationWindow Refactoring

## Overview
This directory contains the refactored simulation framework, breaking down the monolithic 3000-line SimulationWindow into a clean, modular architecture.

## Architecture

### Core Components
- **SimulationTypes.h** - Common data structures (FoldResult, SimulationRun, etc.)
- **SimulationUtils.h** - Statistical and transformation utilities
- **SimulationEngine** - Core walk-forward validation logic

### Model Layer (Strategy Pattern)
- **ISimulationModel** - Abstract interface for all models
- **XGBoostModel** - XGBoost implementation
- **ModelFactory** - Creates model instances
- **ModelCache** - Handles model caching between folds

### UI Layer
- **SimulationConfigWidget** - Feature/target selection, hyperparameters
- **SimulationResultsWidget** - Results table and profit plot
- **SimulationControlsWidget** - Start/stop buttons, progress bar

### Main Coordinator
- **SimulationWindow** - Thin coordinator that connects components

## Benefits

### 1. Extensibility
Adding a new model type (e.g., LightGBM):
```cpp
class LightGBMModel : public ISimulationModel {
    // Implement interface methods
};

// Register in factory
ModelFactory::RegisterModel("LightGBM", []() { 
    return std::make_unique<LightGBMModel>(); 
});
```

### 2. Maintainability
- Each component has single responsibility
- UI changes don't affect business logic
- Model changes don't affect UI

### 3. Testability
```cpp
// Test model in isolation
auto model = std::make_unique<XGBoostModel>();
auto result = model->Train(X_train, y_train, X_val, y_val, config, n_features);
ASSERT_TRUE(result.success);

// Test UI widget separately
SimulationConfigWidget widget;
widget.SetConfig(&config);
bool changed = widget.Draw();
```

### 4. Reusability
- Widgets can be used in other windows
- Models can be used in different contexts
- Utilities available throughout application

## File Organization

```
simulation/
├── README.md                    # This file
├── SimulationTypes.h            # Core data structures
├── SimulationUtils.h            # Utilities
├── ISimulationModel.h           # Model interface
├── XGBoostConfig.h             # XGBoost configuration
├── XGBoostModel.h/cpp          # XGBoost implementation
├── SimulationEngine.h/cpp       # Simulation logic
├── ui/
│   ├── SimulationConfigWidget.h/cpp
│   ├── SimulationResultsWidget.h/cpp
│   └── SimulationControlsWidget.h/cpp
└── SimulationWindow.h/cpp      # Main coordinator

Future additions:
├── models/
│   ├── LightGBMModel.h/cpp
│   ├── CatBoostModel.h/cpp
│   └── NeuralNetModel.h/cpp
```

## Usage Example

```cpp
// In main.cpp
#include "simulation/SimulationWindow.h"

// Create window
auto simWindow = std::make_unique<simulation::SimulationWindow>();
simWindow->SetTimeSeriesWindow(&timeSeriesWindow);

// In main loop
if (simWindow->IsVisible()) {
    simWindow->Draw();
}
```

## Migration Status

- [x] Design architecture
- [x] Create interfaces and headers
- [ ] Implement XGBoostModel
- [ ] Implement SimulationEngine
- [ ] Implement UI widgets
- [ ] Integrate components
- [ ] Test functionality
- [ ] Remove old code

## Key Improvements

### Before (Monolithic)
- 3000+ lines in one file
- Tightly coupled XGBoost code
- Mixed UI and logic
- Hard to test
- Hard to extend

### After (Modular)
- ~300 lines per component
- Clean interfaces
- Separated concerns
- Easy to test
- Easy to extend

## Performance Considerations

- No performance degradation expected
- Potential for optimization:
  - Parallel model training
  - Lazy UI updates
  - Cached data preprocessing

## Future Enhancements

1. **Multi-Model Ensemble**
   - Train multiple models in parallel
   - Combine predictions

2. **Advanced Visualizations**
   - Feature importance plots
   - Learning curves
   - Prediction distributions

3. **Export/Import**
   - Save simulation configurations
   - Export results to CSV/JSON
   - Share model configurations

4. **Real-time Updates**
   - Live data streaming
   - Online model updates
   - Dynamic threshold adjustment

## Documentation

- **SimulationWindow_Refactoring_Plan.md** - Detailed refactoring plan
- **MIGRATION_ROADMAP.md** - Step-by-step migration guide
- **BUILD_INTEGRATION.md** - Build system updates
- **RefactoredSimulationWindow.h** - New window structure

## Contributing

When adding new models:
1. Implement ISimulationModel interface
2. Create model-specific config struct
3. Register in ModelFactory
4. Add to build system
5. Test with existing UI

When modifying UI:
1. Keep widgets independent
2. Use callbacks for communication
3. Don't access engine directly
4. Maintain responsive design
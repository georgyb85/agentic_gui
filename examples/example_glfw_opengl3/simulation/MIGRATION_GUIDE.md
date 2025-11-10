# Migration Guide: Old SimulationWindow to New Architecture

## Overview

The 3000-line monolithic `SimulationWindow.cpp` has been refactored into a clean, modular architecture that supports multiple model types and provides better separation of concerns.

## Architecture Changes

### Old Architecture
- Single 3000-line file: `SimulationWindow.cpp`
- XGBoost-specific implementation
- Tight coupling between UI and business logic
- Difficult to add new models
- Mixed concerns (UI, data, model training, results)

### New Architecture
```
simulation/
├── SimulationWindowNew.cpp    # 200-line coordinator
├── SimulationEngine.cpp        # Core engine
├── models/
│   ├── XGBoostModel.cpp       # XGBoost implementation
│   └── (future models)
├── ui/
│   ├── UniversalConfigWidget.cpp
│   ├── SimulationResultsWidget.cpp
│   └── SimulationControlsWidget.cpp
└── PerformanceMetrics.cpp     # Universal metrics
```

## Migration Steps

### 1. Update main.cpp

Replace the old SimulationWindow instantiation:

```cpp
// OLD
#include "SimulationWindow.h"
static SimulationWindow simulationWindow;

// NEW
#include "simulation/SimulationWindowNew.h"
static simulation::SimulationWindow simulationWindow;

// In initialization:
simulation::InitializeSimulationModels();
```

### 2. Update Build Files

#### For Makefile:
```makefile
# Include the new simulation components
include simulation/Makefile.include
```

#### For Visual Studio:
Add the files listed in `simulation/Makefile.include` to your .vcxproj

### 3. Feature Comparison

| Feature | Old | New |
|---------|-----|-----|
| Walk-forward simulation | ✓ | ✓ |
| Single fold testing | ✓ | ✓ |
| Model caching | ✓ | ✓ |
| XGBoost support | ✓ | ✓ |
| Feature importance | ✓ | ✓ (model-specific) |
| Copy/paste config | Basic | Intelligent with type safety |
| Multiple models | ✗ | ✓ |
| Parallel processing | ✗ | ✓ (prepared) |
| Clean architecture | ✗ | ✓ |

### 4. Code Changes

#### Model Configuration
```cpp
// OLD - Direct XGBoost config
simulationWindow.max_depth = 6;
simulationWindow.eta = 0.3f;

// NEW - Model-agnostic config
auto config = std::make_unique<XGBoostConfig>();
config->max_depth = 6;
config->eta = 0.3f;
engine->SetModelConfig(std::move(config));
```

#### Results Handling
```cpp
// OLD - Direct member access
for (auto& result : simulationWindow.fold_results) {
    // Process result
}

// NEW - Callback-based
engine->SetFoldCompleteCallback([](const FoldResult& result) {
    // Process result
});
```

## Benefits of New Architecture

1. **Modularity**: Each component has a single responsibility
2. **Extensibility**: Easy to add new models (Linear Regression, Neural Networks)
3. **Testability**: Components can be tested independently
4. **Maintainability**: 200-line files vs 3000-line monolith
5. **Type Safety**: std::any with type checking for configurations
6. **Performance**: Prepared for parallel processing
7. **UI Separation**: Clean separation of UI from business logic

## Adding New Models

To add a new model (e.g., Linear Regression):

1. Create `models/LinearRegressionModel.cpp`
2. Implement `ISimulationModel` interface
3. Register in `InitializeSimulationModels()`
4. Model automatically appears in UI

Example:
```cpp
class LinearRegressionModel : public ISimulationModel {
    // Implement required methods
};

// Register
ModelFactory::RegisterModel("Linear Regression", {
    .create_model = []() { 
        return std::make_unique<LinearRegressionModel>(); 
    },
    .category = "Linear Models"
});
```

## Compatibility Mode

For backward compatibility, the new `SimulationWindow` class maintains the same public interface:
- `Draw()`
- `IsVisible()`/`SetVisible()`
- `SetTimeSeriesWindow()`

## Testing the Migration

1. Build the project with new files
2. Run existing workflows - they should work identically
3. Check that model caching still works (orange for cached, red for failed)
4. Verify walk-forward simulation produces same results
5. Test single fold mode

## Troubleshooting

### Unresolved Symbols
- Ensure all files from `simulation/Makefile.include` are added to build
- Check that `simulation::InitializeSimulationModels()` is called at startup

### UI Not Showing
- Verify `SimulationWindowNew` is being used instead of old `SimulationWindow`
- Check that window visibility is set

### Results Different
- Model caching behavior is preserved
- Same XGBoost parameters should produce identical results
- Check that data preprocessing is consistent

## Future Enhancements

The new architecture is prepared for:
- Linear/Polynomial regression models
- Neural network integration
- Real-time streaming data
- Cloud-based model training
- A/B testing framework
- Portfolio optimization
- Risk management modules

## Contact

For questions about the migration, refer to the architecture documentation in `simulation/ARCHITECTURE.md`
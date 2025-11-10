# SimulationWindow Refactoring Plan

## Current Issues
- SimulationWindow.cpp is ~3000 lines - too large and doing too many things
- Tightly coupled to XGBoost implementation
- Difficult to add new model types
- UI code mixed with business logic
- No clear separation of concerns

## Proposed Architecture

### 1. Model Layer (Strategy Pattern)
```
models/
├── ISimulationModel.h           # Abstract interface for all models
├── ModelConfig.h                 # Base configuration structures
├── XGBoostModel.h/cpp           # XGBoost implementation
├── XGBoostConfig.h              # XGBoost-specific config
├── ModelFactory.h/cpp           # Factory for creating models
└── ModelCache.h/cpp             # Model caching logic
```

**ISimulationModel Interface:**
- `train(train_data, val_data, config) -> TrainResult`
- `predict(test_data) -> PredictionResult`
- `serialize() -> buffer`
- `deserialize(buffer)`
- `getDefaultConfig() -> ModelConfig`
- `getConfigWidget() -> ImGui widget`

### 2. UI Components Layer
```
simulation_ui/
├── SimulationConfigWidget.h/cpp    # Feature/target selection UI
├── SimulationHyperparamsWidget.h/cpp # Model hyperparameters UI
├── SimulationControlsWidget.h/cpp   # Start/stop/progress UI
├── SimulationResultsTable.h/cpp     # Results table with coloring
├── SimulationProfitPlot.h/cpp      # Profit visualization
├── SimulationTestModelWidget.h/cpp  # Test model interface
└── SimulationRunManager.h/cpp      # Manages multiple runs UI
```

### 3. Core Simulation Logic
```
simulation_core/
├── SimulationEngine.h/cpp          # Main simulation loop logic
├── SimulationTypes.h               # FoldResult, SimulationRun, etc.
├── WalkForwardValidator.h/cpp      # Walk-forward validation logic
├── DataPreprocessor.h/cpp          # Data extraction & transformation
└── SimulationMetrics.h/cpp         # Metrics calculation
```

### 4. Utilities
```
simulation_utils/
├── StatisticsUtils.h/cpp           # Statistical calculations
├── DataFrameUtils.h/cpp            # DataFrame operations
└── TransformUtils.h/cpp            # Data transformations (tanh, standardization)
```

## Implementation Steps

### Phase 1: Create Model Abstraction
1. Define ISimulationModel interface
2. Create XGBoostModel implementing the interface
3. Move XGBoost-specific code from SimulationWindow to XGBoostModel
4. Implement ModelFactory for model creation

### Phase 2: Extract Data Structures
1. Move all structs to SimulationTypes.h
2. Create ModelConfig hierarchy
3. Separate UI state from business logic

### Phase 3: Extract UI Components
1. Create widget classes for each UI section
2. Each widget class handles its own rendering
3. SimulationWindow becomes a coordinator

### Phase 4: Extract Core Logic
1. Move simulation thread logic to SimulationEngine
2. Create WalkForwardValidator for fold management
3. Move metrics calculation to dedicated class

### Phase 5: Integration
1. Update SimulationWindow to use new components
2. Ensure backward compatibility
3. Test all functionality

## Example: Adding a New Model Type

After refactoring, adding LightGBM would be:
1. Create `LightGBMModel.h/cpp` implementing `ISimulationModel`
2. Create `LightGBMConfig.h` with specific parameters
3. Register in `ModelFactory`
4. Done! UI and simulation logic automatically work

## Benefits
- **Maintainability**: Each component has single responsibility
- **Extensibility**: Easy to add new models
- **Testability**: Components can be unit tested
- **Reusability**: UI widgets can be reused elsewhere
- **Clarity**: Clear separation of concerns

## File Size Targets
- SimulationWindow.cpp: ~300 lines (coordinator only)
- Each widget: 200-400 lines
- Each model: 400-600 lines  
- SimulationEngine: ~500 lines
- Utils: 100-200 lines each
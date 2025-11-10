# Migration Roadmap: SimulationWindow Refactoring

## Current State
- **SimulationWindow.cpp**: ~3000 lines
- **SimulationWindow.h**: ~400 lines
- All logic tightly coupled in one class

## Target Architecture
```
simulation/
├── core/
│   ├── SimulationEngine.cpp/h
│   ├── SimulationTypes.cpp/h
│   └── SimulationUtils.cpp/h
├── models/
│   ├── ISimulationModel.h
│   ├── ModelFactory.cpp/h
│   ├── ModelCache.cpp/h
│   ├── XGBoostModel.cpp/h
│   └── XGBoostConfig.h
├── ui/
│   ├── SimulationConfigWidget.cpp/h
│   ├── SimulationResultsWidget.cpp/h
│   └── SimulationControlsWidget.cpp/h
└── SimulationWindow.cpp/h (coordinator only)
```

## Migration Steps

### Step 1: Implement Core Components (No Breaking Changes)
1. **Implement SimulationTypes.cpp**
   - Move FoldResult::UpdateCache() implementation
   - Add any helper methods

2. **Implement XGBoostModel.cpp**
   - Extract all XGBoost-specific code from SimulationWindow::testSingleFold()
   - Move checkXGBoostError()
   - Move CacheModel() and LoadCachedModel() logic

3. **Implement SimulationEngine.cpp**
   - Extract RunSimulationThread() logic
   - Move data extraction methods
   - Keep interface compatible with current SimulationWindow

### Step 2: Implement UI Widgets (Still No Breaking Changes)
1. **Implement SimulationConfigWidget.cpp**
   - Move DrawFeatureSelection()
   - Move DrawTargetSelection()
   - Move DrawHyperparameters()
   - Move column list management

2. **Implement SimulationResultsWidget.cpp**
   - Move DrawResultsTable()
   - Move DrawProfitPlot()
   - Move results management logic

3. **Implement SimulationControlsWidget.cpp**
   - Move DrawSimulationControls()
   - Move DrawProgressBar()
   - Add model selection dropdown

### Step 3: Create Adapter Layer
1. **Create SimulationWindowAdapter**
   - Inherits from current SimulationWindow
   - Delegates to new components
   - Maintains backward compatibility

### Step 4: Integration Testing
1. Test all existing functionality works
2. Verify performance is maintained
3. Check memory usage

### Step 5: Switch Over
1. Replace SimulationWindow with new implementation
2. Update main.cpp instantiation
3. Update project files

### Step 6: Cleanup
1. Remove old SimulationWindow implementation
2. Remove adapter layer
3. Update documentation

## Implementation Order by Priority

### Phase 1: Core Abstractions (Week 1)
- [ ] SimulationTypes implementation
- [ ] SimulationUtils implementation  
- [ ] ISimulationModel interface finalization
- [ ] XGBoostModel implementation

### Phase 2: Engine and Cache (Week 1-2)
- [ ] ModelCache implementation
- [ ] SimulationEngine implementation
- [ ] ModelFactory implementation

### Phase 3: UI Widgets (Week 2)
- [ ] SimulationConfigWidget
- [ ] SimulationResultsWidget
- [ ] SimulationControlsWidget

### Phase 4: Integration (Week 3)
- [ ] New SimulationWindow coordinator
- [ ] Testing and debugging
- [ ] Performance optimization

## File Mapping

| Current Method | New Location |
|---|---|
| `testSingleFold()` | `XGBoostModel::Train()` |
| `CacheModel()` | `ModelCache::CacheModel()` |
| `LoadCachedModel()` | `ModelCache::LoadCachedModel()` |
| `RunSimulationThread()` | `SimulationEngine::RunSimulationThread()` |
| `DrawFeatureSelection()` | `SimulationConfigWidget::DrawFeatureSelection()` |
| `DrawTargetSelection()` | `SimulationConfigWidget::DrawTargetSelection()` |
| `DrawHyperparameters()` | `SimulationConfigWidget::DrawModelHyperparameters()` |
| `DrawResultsTable()` | `SimulationResultsWidget::DrawResultsTable()` |
| `DrawProfitPlot()` | `SimulationResultsWidget::DrawProfitPlot()` |
| `calculateMedian()` | `utils::Statistics::CalculateMedian()` |
| `calculateStdDev()` | `utils::Statistics::CalculateStdDev()` |
| `calculateQuantile()` | `utils::Statistics::CalculateQuantile()` |
| `extractColumnData()` | `SimulationEngine::ExtractFeatures/Target()` |

## Testing Strategy

### Unit Tests
- Test each component in isolation
- Mock dependencies
- Verify calculations match original

### Integration Tests  
- Test component interactions
- Verify UI updates correctly
- Check threading behavior

### Regression Tests
- Run same simulations before/after
- Compare results exactly
- Verify performance metrics

## Benefits After Migration

1. **Add New Model (e.g., LightGBM)**
   - Create `LightGBMModel.cpp/h`
   - Register in `ModelFactory`
   - Done! (50-100 lines vs 500+ lines before)

2. **Modify UI Layout**
   - Change only relevant widget file
   - No risk to simulation logic

3. **Unit Testing**
   - Test models independently
   - Test UI widgets separately
   - Test engine logic in isolation

4. **Performance Optimization**
   - Profile individual components
   - Optimize bottlenecks without affecting other code
   - Parallel model training possible

5. **Code Reuse**
   - Use widgets in other windows
   - Share models across different simulations
   - Common utilities available globally
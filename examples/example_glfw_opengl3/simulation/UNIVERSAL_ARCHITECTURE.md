# Universal Simulation Architecture

## Overview
This architecture supports diverse model types (linear regression, polynomial, neural networks, etc.) with a unified interface for walk-forward validation and performance testing.

## Key Design Principles

### 1. Model Diversity Through Abstraction
- **ISimulationModel** interface with capabilities discovery
- Models declare what they support (feature importance, early stopping, etc.)
- UI adapts based on model capabilities

### 2. Flexible Configuration with std::any
```cpp
// Each model defines its own config struct
struct LinearRegressionConfig { ... };
struct NeuralNetworkConfig { ... };

// Interface uses std::any for flexibility
virtual std::any CreateDefaultConfig() const = 0;

// Type-safe access in implementation
auto& config = std::any_cast<LinearRegressionConfig&>(base_config);
```

### 3. Universal Features/Targets, Model-Specific Hyperparameters
```cpp
// Universal across all models
std::vector<std::string> features;
std::string target;
WalkForwardConfig walk_forward;

// Model-specific
std::any hyperparameters;  // LinearRegressionConfig, NeuralNetworkConfig, etc.
```

### 4. Elegant Copy/Paste System
```cpp
struct CopiedConfiguration {
    // Universal parts - always copyable
    std::vector<std::string> features;
    std::string target;
    
    // Model-specific parts - only pasteable to same model type
    std::string model_type;
    std::any hyperparameters;
    
    // Intelligent paste
    bool CanPasteHyperparametersTo(const std::string& target_model) {
        return model_type == target_model;
    }
};
```

## Model Examples

### Linear Regression
```cpp
struct LinearRegressionConfig {
    enum RegularizationType { NONE, RIDGE, LASSO, ELASTIC };
    RegularizationType regularization;
    float alpha;  // Regularization strength
    bool use_polynomial;
    int polynomial_degree;
};
```

### Neural Network
```cpp
struct NeuralNetworkConfig {
    std::vector<int> hidden_layers;  // [64, 32] = 2 layers
    ActivationType activation;
    OptimizerType optimizer;
    float learning_rate;
    float dropout_rate;
};
```

### Future: Random Forest (Example)
```cpp
struct RandomForestConfig {
    int n_trees;
    int max_depth;
    int min_samples_split;
    float max_features;  // Fraction of features per tree
};
```

## Universal Performance Metrics

All models are evaluated with the same comprehensive metrics:

### Regression Metrics
- MSE, RMSE, MAE, MAPE
- R², Adjusted R²

### Trading Metrics
- Directional accuracy
- Hit rate
- Win/loss ratio
- Profit factor
- Sharpe ratio
- Maximum drawdown

### Model Selection
- AIC, BIC for comparing models
- Cross-validation scores

## UI Components

### UniversalConfigWidget
Handles all model types elegantly:
```cpp
class UniversalConfigWidget {
    // Model selection dropdown
    bool DrawModelSelection();
    
    // Universal feature/target selection
    bool DrawFeatureTargetSelection();
    
    // Model-specific hyperparameters
    // Delegates to model's IHyperparameterWidget
    bool DrawHyperparameters();
    
    // Copy/paste with intelligence
    void CopyFeatures();        // Works across all models
    void CopyHyperparameters();  // Model-specific
    bool CanPasteHyperparameters(); // Checks compatibility
};
```

### TestModelWidget
Test single fold with any model:
```cpp
class TestModelWidget {
    // Can load config from:
    // 1. Walk-forward fold result
    // 2. Manual configuration
    // 3. Copied configuration
    
    void SetFromFold(const FoldResult& fold);
    void DrawMetricsTable();  // Shows universal metrics
    void DrawPredictionPlot();
};
```

## Walk-Forward Simulation Flow

```cpp
// 1. User selects model type
UniversalConfigWidget::DrawModelSelection()
    -> ModelFactory::CreateModel("Neural Network")
    -> ModelFactory::CreateWidget("Neural Network")

// 2. Configure features/target (universal)
widget.SetFeatures({"feature1", "feature2"});
widget.SetTarget("returns");

// 3. Configure hyperparameters (model-specific)
NeuralNetworkWidget::Draw(config)
    -> User sets layers, learning rate, etc.

// 4. Run simulation (universal engine)
SimulationEngine::ProcessFold()
    -> model->Train(X_train, y_train, X_val, y_val)
    -> model->Predict(X_test)
    -> PerformanceMetrics::Calculate(predictions, actuals)

// 5. Display results (universal metrics)
SimulationResultsWidget::DrawResultsTable()
    -> Shows metrics applicable to all models
    -> Skips unsupported metrics gracefully
```

## Adding a New Model Type

### 1. Define Configuration
```cpp
struct MyModelConfig {
    // Model-specific parameters
    float my_parameter;
};
```

### 2. Implement Model
```cpp
class MyModel : public ISimulationModel {
    TrainingResult Train(...) override {
        // Implementation
    }
    
    Capabilities GetCapabilities() const override {
        return {
            .supports_feature_importance = false,
            .requires_normalization = true
        };
    }
};
```

### 3. Implement Widget
```cpp
class MyModelWidget : public IHyperparameterWidget {
    bool Draw(std::any& config) override {
        auto& cfg = std::any_cast<MyModelConfig&>(config);
        ImGui::SliderFloat("My Parameter", &cfg.my_parameter, 0, 1);
    }
};
```

### 4. Register
```cpp
ModelFactory::RegisterModel("MyModel", {
    .create_model = []() { return std::make_unique<MyModel>(); },
    .create_widget = []() { return std::make_unique<MyModelWidget>(); },
    .category = "Custom",
    .description = "My custom model"
});
```

## Copy/Paste Intelligence

### Features/Target Copy (Universal)
- Can copy from any model
- Can paste to any model
- Always compatible

### Hyperparameters Copy (Model-Specific)
- Can only paste to same model type
- UI shows compatibility:
  - ✓ Green: Compatible (same model)
  - ⚠ Yellow: Partial (some params match)
  - ✗ Red: Incompatible (different model)

### Example Flow
```cpp
// User has XGBoost simulation
Copy Features -> ["price", "volume"]
Copy Hyperparameters -> XGBoostConfig{...}

// Switch to Neural Network
Paste Features -> ✓ Works
Paste Hyperparameters -> ✗ Disabled (incompatible)

// Switch back to XGBoost
Paste Hyperparameters -> ✓ Works
```

## Performance Comparison

The system supports comparing different models:

```cpp
// Run multiple models on same data
results["Linear"] = LinearModel.Train(data);
results["Neural"] = NeuralModel.Train(data);
results["XGBoost"] = XGBoostModel.Train(data);

// Compare with universal metrics
ModelComparison::Compare(results)
    -> Best by Sharpe: "Neural"
    -> Best by R²: "XGBoost"
    -> Best by Simplicity: "Linear"
```

## Benefits

1. **Truly Universal**: Works with any regression model type
2. **Type-Safe**: Despite flexibility, maintains type safety through std::any
3. **Intelligent UI**: Adapts based on model capabilities
4. **Consistent Metrics**: All models evaluated the same way
5. **Easy Extension**: Add new models without changing framework
6. **Smart Copy/Paste**: Knows what can be shared between models
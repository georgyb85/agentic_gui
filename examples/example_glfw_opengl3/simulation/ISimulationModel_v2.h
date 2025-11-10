#pragma once

#include "SimulationTypes.h"
#include <memory>
#include <vector>
#include <string>
#include <any>
#include <map>
#include <functional>

namespace simulation {

// Forward declarations
class IModelConfigWidget;

// Universal model interface for diverse model types
class ISimulationModel {
public:
    virtual ~ISimulationModel() = default;
    
    // Model identification
    virtual std::string GetModelType() const = 0;
    virtual std::string GetDescription() const = 0;
    virtual std::string GetModelFamily() const = 0; // "tree", "linear", "neural", etc.
    
    // Core training and prediction
    virtual TrainingResult Train(
        const std::vector<float>& X_train,
        const std::vector<float>& y_train,
        const std::vector<float>& X_val,
        const std::vector<float>& y_val,
        const ModelConfigBase& config,
        int num_features
    ) = 0;
    
    virtual PredictionResult Predict(
        const std::vector<float>& X_test,
        int num_samples,
        int num_features
    ) = 0;
    
    // Model persistence
    virtual std::vector<char> Serialize() const = 0;
    virtual bool Deserialize(const std::vector<char>& buffer) = 0;
    
    // Configuration management - using std::any for flexibility
    virtual std::any CreateDefaultConfig() const = 0;
    virtual std::any CloneConfig(const std::any& config) const = 0;
    virtual bool ValidateConfig(const std::any& config) const = 0;
    
    // Check if model is available
    virtual bool IsAvailable() const = 0;
    virtual std::string GetAvailabilityError() const { return ""; }
    
    // Optional capabilities - models return empty/false if not supported
    struct Capabilities {
        bool supports_feature_importance = false;
        bool supports_partial_dependence = false;
        bool supports_prediction_intervals = false;
        bool supports_online_learning = false;
        bool supports_regularization = false;
        bool supports_early_stopping = false;
        bool requires_normalization = false;
        bool requires_feature_scaling = false;
    };
    virtual Capabilities GetCapabilities() const = 0;
    
    // Optional: Feature importance (empty if not supported)
    virtual std::vector<std::pair<std::string, float>> GetFeatureImportance() const {
        return {};
    }
    
    // Optional: Model complexity/size metrics
    virtual std::map<std::string, float> GetModelMetrics() const {
        // Could return things like:
        // - "parameters": number of parameters
        // - "training_time": seconds
        // - "memory_usage": MB
        // - "complexity": some measure
        return {};
    }
};

// Base hyperparameter widget interface
class IHyperparameterWidget {
public:
    virtual ~IHyperparameterWidget() = default;
    
    // Render UI for hyperparameters
    // Returns true if any parameter was modified
    virtual bool Draw(std::any& config) = 0;
    
    // Get compact string representation for display
    virtual std::string GetSummary(const std::any& config) const = 0;
    
    // Import/Export as JSON string for copy/paste
    virtual std::string ExportToJson(const std::any& config) const = 0;
    virtual bool ImportFromJson(const std::string& json, std::any& config) const = 0;
};

// Model configuration widget interface (can be same as hyperparameter widget)
class IModelConfigWidget {
public:
    virtual ~IModelConfigWidget() = default;
    
    // Draw configuration UI
    virtual bool Draw() = 0;
    
    // Get/Set configuration
    virtual std::any GetConfig() const = 0;
    virtual void SetConfig(const std::any& config) = 0;
    
    // Get model type this widget configures
    virtual std::string GetModelType() const = 0;
};

// Factory for creating models and their UI widgets
class ModelFactory {
public:
    struct ModelRegistration {
        std::function<std::unique_ptr<ISimulationModel>()> create_model;
        std::function<std::unique_ptr<IModelConfigWidget>()> create_widget;
        std::string category; // "Regression", "Tree-Based", "Neural Network", etc.
        std::string description;
    };
    
    // Register a model type
    static void RegisterModel(
        const std::string& model_type,
        const ModelRegistration& registration
    );
    
    // Create instances
    static std::unique_ptr<ISimulationModel> CreateModel(const std::string& model_type);
    static std::unique_ptr<IModelConfigWidget> CreateWidget(const std::string& model_type);
    
    // Get available models by category
    static std::map<std::string, std::vector<std::string>> GetModelsByCategory();
    static std::vector<std::string> GetAllModels();
    static bool IsModelAvailable(const std::string& model_type);
    
private:
    static std::map<std::string, ModelRegistration>& GetRegistry();
};

} // namespace simulation
#pragma once

#include "../ISimulationModel_v2.h"
#include <vector>
#include <memory>

namespace simulation {
namespace models {

// Configuration for Linear Regression
struct LinearRegressionConfig {
    // Regularization
    enum RegularizationType {
        NONE,
        RIDGE,    // L2 regularization
        LASSO,    // L1 regularization
        ELASTIC   // Combination of L1 and L2
    };
    RegularizationType regularization = RIDGE;
    float alpha = 0.01f;        // Regularization strength
    float l1_ratio = 0.5f;       // For elastic net (0=Ridge, 1=Lasso)
    
    // Solver options
    bool fit_intercept = true;
    bool normalize = true;       // Normalize features before fitting
    int max_iterations = 1000;   // For iterative solvers
    float tolerance = 1e-4f;     // Convergence tolerance
    
    // Polynomial features
    bool use_polynomial = false;
    int polynomial_degree = 2;
    bool interaction_only = false;
    
    std::string ToString() const {
        std::string reg_str;
        switch (regularization) {
            case NONE: reg_str = "None"; break;
            case RIDGE: reg_str = "Ridge"; break;
            case LASSO: reg_str = "Lasso"; break;
            case ELASTIC: reg_str = "Elastic"; break;
        }
        return "Linear Regression: " + reg_str + " (α=" + std::to_string(alpha) + ")";
    }
};

class LinearRegressionModel : public ISimulationModel {
public:
    LinearRegressionModel();
    ~LinearRegressionModel() override;
    
    // ISimulationModel interface
    std::string GetModelType() const override { return "Linear Regression"; }
    std::string GetDescription() const override { 
        return "Linear/Polynomial regression with optional regularization";
    }
    std::string GetModelFamily() const override { return "linear"; }
    
    TrainingResult Train(
        const std::vector<float>& X_train,
        const std::vector<float>& y_train,
        const std::vector<float>& X_val,
        const std::vector<float>& y_val,
        const ModelConfigBase& config,
        int num_features
    ) override;
    
    PredictionResult Predict(
        const std::vector<float>& X_test,
        int num_samples,
        int num_features
    ) override;
    
    std::vector<char> Serialize() const override;
    bool Deserialize(const std::vector<char>& buffer) override;
    
    std::any CreateDefaultConfig() const override {
        return std::make_any<LinearRegressionConfig>();
    }
    
    std::any CloneConfig(const std::any& config) const override;
    bool ValidateConfig(const std::any& config) const override;
    
    bool IsAvailable() const override { return true; } // Always available
    
    Capabilities GetCapabilities() const override {
        return {
            .supports_feature_importance = true,  // Via coefficients
            .supports_partial_dependence = false,
            .supports_prediction_intervals = true,
            .supports_online_learning = false,
            .supports_regularization = true,
            .supports_early_stopping = false,
            .requires_normalization = false,  // Optional
            .requires_feature_scaling = false  // Recommended but not required
        };
    }
    
    std::vector<std::pair<std::string, float>> GetFeatureImportance() const override;
    
    std::map<std::string, float> GetModelMetrics() const override;
    
private:
    // Model parameters (learned)
    std::vector<float> m_coefficients;
    float m_intercept;
    
    // Feature engineering state
    std::vector<int> m_polynomial_features_map;  // Maps expanded features to original
    
    // Training metrics
    float m_training_r2;
    float m_validation_r2;
    int m_num_features;
    
    // Helper methods
    std::vector<float> ExpandPolynomialFeatures(
        const std::vector<float>& X,
        int num_samples,
        int num_features,
        int degree
    ) const;
    
    void FitNormalEquation(
        const std::vector<float>& X,
        const std::vector<float>& y,
        int num_samples,
        int num_features,
        const LinearRegressionConfig& config
    );
    
    void FitIterative(
        const std::vector<float>& X,
        const std::vector<float>& y,
        int num_samples,
        int num_features,
        const LinearRegressionConfig& config
    );
};

// UI Widget for Linear Regression hyperparameters
class LinearRegressionWidget : public IHyperparameterWidget {
public:
    bool Draw(std::any& config) override;
    std::string GetSummary(const std::any& config) const override;
    std::string ExportToJson(const std::any& config) const override;
    bool ImportFromJson(const std::string& json, std::any& config) const override;
};

} // namespace models
} // namespace simulation
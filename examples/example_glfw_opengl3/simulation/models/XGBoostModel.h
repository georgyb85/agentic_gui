#pragma once

#include "../ISimulationModel_v2.h"
#include "../XGBoostConfig.h"
#include <vector>
#include <memory>
#include <atomic>

// XGBoost C API forward declarations
typedef void* BoosterHandle;
typedef void* DMatrixHandle;

namespace simulation {
namespace models {

class XGBoostModel : public ISimulationModel {
public:
    XGBoostModel();
    ~XGBoostModel() override;
    
    // ISimulationModel interface
    std::string GetModelType() const override { return "XGBoost"; }
    std::string GetModelFamily() const override { return "Gradient Boosting"; }
    std::string GetDescription() const override { 
        return "Gradient Boosting with XGBoost library";
    }
    
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
    
    std::any CreateDefaultConfig() const override;
    std::any CloneConfig(const std::any& config) const override;
    bool ValidateConfig(const std::any& config) const override;
    Capabilities GetCapabilities() const override;
    
    std::vector<std::pair<std::string, float>> GetFeatureImportance() const override;
    
    bool IsAvailable() const override;
    std::string GetAvailabilityError() const override { return m_availability_error; }
    
private:
    // Helper methods
    void CheckXGBoostError(int status, const std::string& context);
    void FreeResources();
    const XGBoostConfig& GetXGBoostConfig(const ModelConfigBase& config) const;
    
    // Serialized model for predictions
    std::vector<char> m_serialized_model;
    
    // Feature names for importance
    std::vector<std::string> m_feature_names;
    
    // Availability check result
    mutable bool m_availability_checked;
    mutable bool m_is_available;
    mutable std::string m_availability_error;
};

} // namespace models
} // namespace simulation
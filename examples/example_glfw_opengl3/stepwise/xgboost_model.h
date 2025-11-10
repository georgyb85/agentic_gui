#pragma once

#include "model_interface.h"
#include "xgboost_config_widget.h"
#include <memory>
#include <vector>

// XGBoost C API forward declarations
typedef void* BoosterHandle;
typedef void* DMatrixHandle;

namespace stepwise {

// XGBoost model implementation for stepwise selection
class XGBoostModel : public IStepwiseModel {
public:
    explicit XGBoostModel(const XGBoostConfig& config);
    ~XGBoostModel();
    
    // IStepwiseModel interface
    void fit(const DataMatrix& X, const std::vector<double>& y,
            const std::vector<int>& feature_indices) override;
    
    std::vector<double> predict(const DataMatrix& X,
                               const std::vector<int>& feature_indices) const override;
    
    double score(const DataMatrix& X, const std::vector<double>& y,
                const std::vector<int>& feature_indices) const override;
    
    std::vector<double> get_coefficients() const override;
    
    std::unique_ptr<IStepwiseModel> clone() const override;
    
    std::string get_model_type() const override { return "XGBoost"; }
    
    bool has_coefficients() const override { return false; }
    
    // Get feature importance scores
    std::vector<float> get_feature_importance() const;
    
private:
    XGBoostConfig m_config;
    bool m_fitted;
    
    // XGBoost model data
    std::vector<int> m_feature_indices;
    std::vector<char> m_serialized_model;  // Serialized XGBoost model
};

} // namespace stepwise
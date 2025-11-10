#pragma once

#include "../ISimulationModel_v2.h"
#include "../XGBoostConfig.h"
#include <memory>

namespace simulation {
namespace models {

// Widget for XGBoost-specific configuration
class XGBoostWidget : public IModelConfigWidget {
public:
    XGBoostWidget();
    ~XGBoostWidget() override = default;
    
    // IModelConfigWidget interface
    bool Draw() override;
    std::any GetConfig() const override;
    void SetConfig(const std::any& config) override;
    std::string GetModelType() const override { return "XGBoost"; }
    
private:
    XGBoostConfig m_config;
    bool m_configChanged;
};

} // namespace models
} // namespace simulation
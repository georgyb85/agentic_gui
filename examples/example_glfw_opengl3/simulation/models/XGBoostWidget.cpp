#include "XGBoostWidget.h"
#include "imgui.h"
#include <sstream>

namespace simulation {
namespace models {

XGBoostWidget::XGBoostWidget()
    : m_configChanged(false) {
    // Initialize with defaults
}

bool XGBoostWidget::Draw() {
    bool changed = false;
    
    if (ImGui::CollapsingHeader("Tree Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt("Max Depth", &m_config.max_depth, 1, 20);
        changed |= ImGui::SliderFloat("Min Child Weight", &m_config.min_child_weight, 0.1f, 100.0f, "%.1f");
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Minimum sum of instance weight needed in a child");
        }
    }
    
    if (ImGui::CollapsingHeader("Learning Parameters")) {
        changed |= ImGui::SliderFloat("Learning Rate", &m_config.learning_rate, 
            0.001f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::InputInt("Num Rounds", &m_config.num_boost_round);
        changed |= ImGui::InputInt("Early Stopping", &m_config.early_stopping_rounds);
        changed |= ImGui::InputInt("Min Rounds", &m_config.min_boost_rounds);
        changed |= ImGui::Checkbox("Force Minimum Training", &m_config.force_minimum_training);
        
        // Loss function selection
        ImGui::Separator();
        const char* loss_functions[] = { "Squared Error", "Quantile 95%", "Quantile 5%" };
        int current_loss = 0;
        if (m_config.objective == "reg:squarederror") {
            current_loss = 0;
        } else if (m_config.objective == "reg:quantileerror" && m_config.quantile_alpha == 0.95f) {
            current_loss = 1;
        } else if (m_config.objective == "reg:quantileerror" && m_config.quantile_alpha == 0.05f) {
            current_loss = 2;
        }
        
        if (ImGui::Combo("Loss Function", &current_loss, loss_functions, 3)) {
            switch (current_loss) {
                case 0:
                    m_config.objective = "reg:squarederror";
                    break;
                case 1:
                    m_config.objective = "reg:quantileerror";
                    m_config.quantile_alpha = 0.95f;
                    break;
                case 2:
                    m_config.objective = "reg:quantileerror";
                    m_config.quantile_alpha = 0.05f;
                    break;
            }
            changed = true;
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Squared Error: Standard MSE loss, balanced for all predictions\n"
                "Quantile 95%%: Focus on predicting high returns (for long trades)\n"
                "Quantile 5%%: Focus on predicting low returns (for short trades)"
            );
        }
    }
    
    if (ImGui::CollapsingHeader("Regularization")) {
        changed |= ImGui::SliderFloat("Subsample", &m_config.subsample, 0.1f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Col Sample", &m_config.colsample_bytree, 0.1f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Lambda (L2)", &m_config.lambda, 0.0f, 10.0f, "%.2f");
    }
    
    if (ImGui::CollapsingHeader("Trading Threshold")) {
        const char* threshold_methods[] = { "95th Percentile", "Optimal ROC (PF-based)" };
        int current_method = static_cast<int>(m_config.threshold_method);
        if (ImGui::Combo("Threshold Method", &current_method, threshold_methods, 2)) {
            m_config.threshold_method = static_cast<ThresholdMethod>(current_method);
            changed = true;
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "95th Percentile: Traditional method using validation set\n"
                "Optimal ROC: Finds threshold that maximizes profit factor on training set"
            );
        }
    }
    
    m_configChanged = changed;
    return changed;
}

std::any XGBoostWidget::GetConfig() const {
    return m_config;
}

void XGBoostWidget::SetConfig(const std::any& config) {
    try {
        m_config = std::any_cast<XGBoostConfig>(config);
    } catch (const std::bad_any_cast&) {
        // Keep current config if cast fails
    }
}

} // namespace models
} // namespace simulation
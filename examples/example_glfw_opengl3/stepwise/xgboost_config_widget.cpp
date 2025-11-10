#include "xgboost_config_widget.h"

namespace stepwise {

bool XGBoostConfigWidget::Draw() {
    bool changed = false;
    
    // Tree Parameters section
    if (ImGui::TreeNodeEx("Tree Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt("Max Depth", &m_config.max_depth, 1, 15);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maximum depth of each tree. Deeper trees can capture more complex patterns but may overfit.");
        }
        
        changed |= ImGui::SliderFloat("Min Child Weight", &m_config.min_child_weight, 0.1f, 100.0f, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Minimum sum of instance weight needed in a child. Higher values prevent overfitting.");
        }
        
        ImGui::TreePop();
    }
    
    // Learning Parameters section
    if (ImGui::TreeNodeEx("Learning Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Learning Rate", &m_config.learning_rate, 
            0.001f, 0.3f, "%.4f", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Step size shrinkage. Lower values prevent overfitting but require more rounds.");
        }
        
        changed |= ImGui::InputInt("Boosting Rounds", &m_config.num_boost_round);
        if (m_config.num_boost_round < 10) m_config.num_boost_round = 10;
        if (m_config.num_boost_round > 5000) m_config.num_boost_round = 5000;
        
        changed |= ImGui::InputInt("Early Stopping", &m_config.early_stopping_rounds);
        if (m_config.early_stopping_rounds < 0) m_config.early_stopping_rounds = 0;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stop training if validation score doesn't improve for this many rounds. 0 = disabled.");
        }
        
        ImGui::TreePop();
    }
    
    // Regularization section
    if (ImGui::TreeNodeEx("Regularization", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Subsample", &m_config.subsample, 0.3f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Fraction of training samples used for each tree. Lower values prevent overfitting.");
        }
        
        changed |= ImGui::SliderFloat("Column Sample", &m_config.colsample_bytree, 0.3f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Fraction of features used for each tree. Lower values prevent overfitting.");
        }
        
        changed |= ImGui::SliderFloat("Lambda (L2)", &m_config.lambda, 0.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("L2 regularization on weights. Higher values prevent overfitting.");
        }
        
        changed |= ImGui::SliderFloat("Alpha (L1)", &m_config.alpha, 0.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("L1 regularization on weights. Can help with feature selection.");
        }
        
        ImGui::TreePop();
    }
    
    // Advanced section
    if (ImGui::TreeNode("Advanced")) {
        // Tree method selection
        const char* tree_methods[] = { "hist", "exact", "approx" };
        int current_method = 0;
        if (m_config.tree_method == "exact") current_method = 1;
        else if (m_config.tree_method == "approx") current_method = 2;
        
        if (ImGui::Combo("Tree Method", &current_method, tree_methods, 3)) {
            m_config.tree_method = tree_methods[current_method];
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Algorithm for tree construction. 'hist' is fast and memory efficient.");
        }
        
        // Device selection
        const char* devices[] = { "cpu", "cuda" };
        int current_device = (m_config.device == "cuda") ? 1 : 0;
        
        if (ImGui::Combo("Device", &current_device, devices, 2)) {
            m_config.device = devices[current_device];
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Computing device. CUDA requires GPU support.");
        }
        
        ImGui::TreePop();
    }
    
    // Display current configuration summary
    ImGui::Separator();
    ImGui::TextWrapped("Config: %s", m_config.ToString().c_str());
    
    // Reset button
    if (ImGui::Button("Reset to Defaults")) {
        ResetToDefaults();
        changed = true;
    }
    
    return changed;
}

} // namespace stepwise
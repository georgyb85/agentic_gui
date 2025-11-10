#include "UniversalConfigWidget.h"
#include "../ISimulationModel_v2.h"
#include "../XGBoostConfig.h"
#include "../../TimeSeriesWindow.h"
#include "../../analytics_dataframe.h"
#include "imgui.h"
#include <algorithm>
#include <iostream>
#include <cctype>
#include <cstring>
#include <sstream>
#include <set>

namespace simulation {
namespace ui {

UniversalConfigWidget::UniversalConfigWidget()
    : m_timeSeriesWindow(nullptr)
    , m_selectedModelType("XGBoost")  // Default to XGBoost
    , m_selectedCategoryIndex(0)
    , m_selectedModelIndex(0)
    , m_useFeatureSchedule(false)
    , m_showAdvancedOptions(false)
    , m_sortFeaturesAlphabetically(true) {
    // Initialize with default XGBoost config
    m_currentConfig = XGBoostConfig();
}

UniversalConfigWidget::~UniversalConfigWidget() = default;

void UniversalConfigWidget::SetDataSource(TimeSeriesWindow* tsWindow) {
    m_timeSeriesWindow = tsWindow;
    // Initialize columns immediately so features are available before Draw()
    if (tsWindow && tsWindow->HasData()) {
        UpdateAvailableColumns();
        // Build initial selected features list from checkboxes
        m_selectedFeatures.clear();
        for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
            if (m_featureCheckboxes[i]) {
                m_selectedFeatures.push_back(m_availableColumns[i]);
            }
        }
    }
}

std::vector<std::string> UniversalConfigWidget::GetFeatures() const {
    // Make sure we have updated columns if data is now available
    if (m_selectedFeatures.empty() && m_timeSeriesWindow && m_timeSeriesWindow->HasData()) {
        // Need to update columns - cast away const for this internal update
        const_cast<UniversalConfigWidget*>(this)->UpdateAvailableColumns();
        // Rebuild selected features from checkboxes
        auto& mutableThis = *const_cast<UniversalConfigWidget*>(this);
        mutableThis.m_selectedFeatures.clear();
        for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
            if (m_featureCheckboxes[i]) {
                mutableThis.m_selectedFeatures.push_back(m_availableColumns[i]);
            }
        }
    }
    return m_selectedFeatures;
}

std::string UniversalConfigWidget::GetTarget() const {
    // Make sure we have updated columns if data is now available
    if (m_selectedTarget.empty() && m_timeSeriesWindow && m_timeSeriesWindow->HasData()) {
        const_cast<UniversalConfigWidget*>(this)->UpdateAvailableColumns();
    }
    return m_selectedTarget;
}

void UniversalConfigWidget::SetAvailableModels(
    const std::map<std::string, std::vector<std::string>>& models_by_category) {
    m_modelsByCategory = models_by_category;
    
    // Select first model if available
    if (!m_modelsByCategory.empty()) {
        auto it = m_modelsByCategory.begin();
        if (!it->second.empty()) {
            m_selectedModelType = it->second[0];
            OnModelTypeChanged(m_selectedModelType);
        }
    }
}

bool UniversalConfigWidget::Draw() {
    bool changed = false;
    
    if (ImGui::BeginTabBar("ConfigTabs")) {
        // Features/Target Tab (Primary)
        if (ImGui::BeginTabItem("Data")) {
            changed |= DrawFeatureTargetSelection();
            ImGui::EndTabItem();
        }
        
        // Hyperparameters Tab
        if (ImGui::BeginTabItem("Hyperparameters")) {
            changed |= DrawHyperparameters();
            ImGui::EndTabItem();
        }
        
        // Walk-Forward Tab
        if (ImGui::BeginTabItem("Walk-Forward")) {
            changed |= DrawWalkForwardSettings();
            ImGui::EndTabItem();
        }
        
        // Copy/Paste Tab
        if (ImGui::BeginTabItem("Copy/Paste")) {
            DrawCopyPasteButtons();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    return changed;
}

bool UniversalConfigWidget::DrawModelSelection() {
    bool changed = false;
    
    ImGui::Text("Select Model Type");
    ImGui::Separator();
    
    // Category selection
    std::vector<std::string> categories;
    for (const auto& [cat, models] : m_modelsByCategory) {
        categories.push_back(cat);
    }
    
    if (!categories.empty()) {
        if (ImGui::BeginCombo("Category", categories[m_selectedCategoryIndex].c_str())) {
            for (size_t i = 0; i < categories.size(); ++i) {
                bool is_selected = (m_selectedCategoryIndex == i);
                if (ImGui::Selectable(categories[i].c_str(), is_selected)) {
                    m_selectedCategoryIndex = i;
                    m_selectedModelIndex = 0;  // Reset model selection
                    changed = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        // Model selection within category
        const auto& models_in_category = m_modelsByCategory.at(categories[m_selectedCategoryIndex]);
        if (!models_in_category.empty()) {
            if (ImGui::BeginCombo("Model", models_in_category[m_selectedModelIndex].c_str())) {
                for (size_t i = 0; i < models_in_category.size(); ++i) {
                    bool is_selected = (m_selectedModelIndex == i);
                    
                    // Check if model is available
                    bool is_available = ModelFactory::IsModelAvailable(models_in_category[i]);
                    
                    if (!is_available) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    }
                    
                    if (ImGui::Selectable(models_in_category[i].c_str(), is_selected)) {
                        if (is_available) {
                            m_selectedModelIndex = i;
                            std::string new_model = models_in_category[i];
                            if (new_model != m_selectedModelType) {
                                m_selectedModelType = new_model;
                                OnModelTypeChanged(new_model);
                                changed = true;
                            }
                        }
                    }
                    
                    if (!is_available) {
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Model not available - library may not be installed");
                        }
                    }
                    
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        
        // Model description
        if (!m_selectedModelType.empty()) {
            auto model = ModelFactory::CreateModel(m_selectedModelType);
            if (model) {
                ImGui::TextWrapped("Description: %s", model->GetDescription().c_str());
                
                // Show capabilities
                auto caps = model->GetCapabilities();
                ImGui::Text("Capabilities:");
                ImGui::Indent();
                
                if (caps.supports_feature_importance) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Feature Importance");
                }
                if (caps.supports_early_stopping) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Early Stopping");
                }
                if (caps.supports_regularization) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Regularization");
                }
                if (caps.requires_normalization) {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "⚠ Requires Normalization");
                }
                if (caps.requires_feature_scaling) {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "⚠ Requires Feature Scaling");
                }
                
                ImGui::Unindent();
            }
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No models available");
    }
    
    return changed;
}

bool UniversalConfigWidget::DrawFeatureTargetSelection() {
    bool changed = false;
    
    UpdateAvailableColumns();
    
    // Target selection FIRST (only show tgt_ prefixed columns)
    ImGui::Text("Target Selection");
    ImGui::Separator();
    
    if (!m_availableTargets.empty()) {
        if (ImGui::BeginCombo("Target", m_selectedTarget.c_str())) {
            for (const auto& target : m_availableTargets) {
                bool is_selected = (target == m_selectedTarget);
                if (ImGui::Selectable(target.c_str(), is_selected)) {
                    m_selectedTarget = target;
                    changed = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No target columns (tgt_*) available");
    }
    
    ImGui::Separator();
    
    // Feature Selection with Paste buttons
    ImGui::Text("Feature Selection");
    
    // Add radio buttons for feature selection mode
    if (ImGui::RadioButton("Manual Selection", !m_useFeatureSchedule)) {
        m_useFeatureSchedule = false;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Use Feature Schedule", m_useFeatureSchedule)) {
        m_useFeatureSchedule = true;
        changed = true;
    }
    
    if (m_useFeatureSchedule) {
        // Feature schedule text area
        ImGui::Text("Feature Schedule (format: startRow-endRow: feature1, feature2, ...)");
        static char scheduleBuffer[8192] = "";
        if (m_featureSchedule.size() < sizeof(scheduleBuffer)) {
            strcpy(scheduleBuffer, m_featureSchedule.c_str());
        }
        if (ImGui::InputTextMultiline("##FeatureSchedule", scheduleBuffer, sizeof(scheduleBuffer), 
                                      ImVec2(-1, 150))) {
            m_featureSchedule = scheduleBuffer;
            changed = true;
        }
        ImGui::TextWrapped("Paste feature schedule from Enhanced Stepwise Selection results");
    } else {
    // Manual feature selection
    
    // Paste buttons at the top
    if (m_copiedConfig.has_features) {
        if (ImGui::Button("Paste Features")) {
            // Clear current selection
            for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
                m_featureCheckboxes[i] = false;
            }
            
            // Set copied features
            for (const auto& feature : m_copiedConfig.features) {
                for (size_t i = 0; i < m_availableColumns.size(); ++i) {
                    if (m_availableColumns[i] == feature) {
                        m_featureCheckboxes[i] = true;
                        break;
                    }
                }
            }
            
            // Set target
            m_selectedTarget = m_copiedConfig.target;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                          "(%zu features available)", m_copiedConfig.features.size());
    }
    
    ImGui::Separator();
    
    // Search filter
    static char search_buffer[256] = "";
    ImGui::InputText("Filter", search_buffer, sizeof(search_buffer));
    m_searchFilter = search_buffer;
    
    // Sort option
    ImGui::SameLine();
    ImGui::Checkbox("Sort A-Z", &m_sortFeaturesAlphabetically);
    
    // Select all/none buttons
    if (ImGui::Button("Select All")) {
        for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
            // Only select visible (filtered) features
            const auto& col = m_availableColumns[i];
            if (m_searchFilter.empty() || 
                col.find(m_searchFilter) != std::string::npos) {
                m_featureCheckboxes[i] = true;
            }
        }
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
            m_featureCheckboxes[i] = false;
        }
        changed = true;
    }
    
    // Feature list
    ImGui::BeginChild("FeatureList", ImVec2(0, 200), true);
    
    std::vector<std::pair<std::string, size_t>> display_features;
    for (size_t i = 0; i < m_availableColumns.size(); ++i) {
        const auto& col = m_availableColumns[i];
        // Apply search filter - case insensitive
        if (!m_searchFilter.empty()) {
            std::string col_lower = col;
            std::string filter_lower = m_searchFilter;
            std::transform(col_lower.begin(), col_lower.end(), col_lower.begin(), ::tolower);
            std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);
            if (col_lower.find(filter_lower) == std::string::npos) {
                continue;
            }
        }
        display_features.push_back({col, i});
    }
    
    // Sort if requested
    if (m_sortFeaturesAlphabetically) {
        std::sort(display_features.begin(), display_features.end());
    }
    
    // Display checkboxes
    for (const auto& [col_name, idx] : display_features) {
        bool checked = m_featureCheckboxes[idx];
        if (ImGui::Checkbox(col_name.c_str(), &checked)) {
            m_featureCheckboxes[idx] = checked;
            changed = true;
        }
    }
    
    ImGui::EndChild();
    
    // Update selected features list
    m_selectedFeatures.clear();
    for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
        if (m_featureCheckboxes[i]) {
            m_selectedFeatures.push_back(m_availableColumns[i]);
        }
    }
    
    ImGui::Text("Selected: %d features", (int)m_selectedFeatures.size());
    } // End of manual selection block
    
    return changed;
}

std::vector<std::string> UniversalConfigWidget::GetFeaturesForRange(int startRow, int endRow) const {
    if (!m_useFeatureSchedule || m_featureSchedule.empty()) {
        return m_selectedFeatures;
    }
    
    // Special case: if asking for a very large range (like 0-100000), return all unique features from schedule
    if (startRow == 0 && endRow >= 100000) {
        std::set<std::string> allFeatures;
        std::istringstream scheduleStream(m_featureSchedule);
        std::string line;
        
        while (std::getline(scheduleStream, line)) {
            if (line.empty()) continue;
            
            size_t colonPos = line.find(':');
            if (colonPos == std::string::npos) continue;
            
            std::string featuresStr = line.substr(colonPos + 1);
            std::istringstream featureStream(featuresStr);
            std::string feature;
            
            while (std::getline(featureStream, feature, ',')) {
                feature.erase(0, feature.find_first_not_of(" \t"));
                feature.erase(feature.find_last_not_of(" \t") + 1);
                if (!feature.empty()) {
                    allFeatures.insert(feature);
                }
            }
        }
        
        return std::vector<std::string>(allFeatures.begin(), allFeatures.end());
    }
    
    // Parse the feature schedule to find the appropriate feature set
    std::istringstream scheduleStream(m_featureSchedule);
    std::string line;
    
    while (std::getline(scheduleStream, line)) {
        // Skip empty lines
        if (line.empty()) continue;
        
        // Parse line format: "startRow-endRow: feature1, feature2, ..."
        size_t dashPos = line.find('-');
        size_t colonPos = line.find(':');
        
        if (dashPos == std::string::npos || colonPos == std::string::npos) {
            continue;  // Invalid format, skip
        }
        
        try {
            int rangeStart = std::stoi(line.substr(0, dashPos));
            int rangeEnd = std::stoi(line.substr(dashPos + 1, colonPos - dashPos - 1));
            
            // Check if the current training range falls within this schedule range
            // A schedule range covers the training range if it fully contains it
            if (rangeStart <= startRow && rangeEnd >= endRow) {
                // Parse features from this line
                std::vector<std::string> features;
                std::string featuresStr = line.substr(colonPos + 1);
                
                std::istringstream featureStream(featuresStr);
                std::string feature;
                
                while (std::getline(featureStream, feature, ',')) {
                    // Trim whitespace
                    feature.erase(0, feature.find_first_not_of(" \t"));
                    feature.erase(feature.find_last_not_of(" \t") + 1);
                    
                    if (!feature.empty()) {
                        features.push_back(feature);
                    }
                }
                
                return features;
            }
        } catch (const std::exception&) {
            // Invalid number format, skip this line
            continue;
        }
    }
    
    // If no matching range found, fall back to selected features
    return m_selectedFeatures;
}

bool UniversalConfigWidget::DrawHyperparameters() {
    bool changed = false;
    
    if (!m_hyperparamWidget) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No model selected");
        return false;
    }
    
    ImGui::Text("Model: %s", m_selectedModelType.c_str());
    
    // Paste hyperparameters button
    if (m_copiedConfig.has_hyperparameters && m_copiedConfig.model_type == m_selectedModelType) {
        ImGui::SameLine();
        if (ImGui::Button("Paste Hyperparameters")) {
            m_currentConfig = m_copiedConfig.hyperparameters;
            changed = true;
        }
    }
    
    ImGui::Separator();
    
    // Delegate to model-specific widget
    if (m_hyperparamWidget) {
        m_hyperparamWidget->SetConfig(m_currentConfig);
        changed = m_hyperparamWidget->Draw();
        if (changed) {
            m_currentConfig = m_hyperparamWidget->GetConfig();
        }
    }
    
    return changed;
}

bool UniversalConfigWidget::DrawWalkForwardSettings() {
    bool changed = false;
    
    ImGui::Text("Walk-Forward Validation Settings");
    ImGui::Separator();
    
    changed |= ImGui::InputInt("Train Size", &m_walkForwardConfig.train_size, 100, 1000);
    changed |= ImGui::InputInt("Test Size", &m_walkForwardConfig.test_size, 10, 100);
    changed |= ImGui::InputInt("Train-Test Gap", &m_walkForwardConfig.train_test_gap, 1, 10);
    changed |= ImGui::InputInt("Fold Step", &m_walkForwardConfig.fold_step, 10, 100);
    
    ImGui::Separator();
    
    changed |= ImGui::InputInt("Start Fold", &m_walkForwardConfig.start_fold, 1, 10);
    changed |= ImGui::InputInt("End Fold", &m_walkForwardConfig.end_fold, 1, 10);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set to -1 for automatic calculation based on available data");
    }
    
    changed |= ImGui::InputInt("Initial Offset", &m_walkForwardConfig.initial_offset, 100, 1000);
    
    // Calculate and display max folds
    int max_folds = CalculateMaxFolds();
    if (max_folds > 0) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                          "Max folds with current settings: %d", max_folds);
    } else {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), 
                          "Insufficient data for current settings");
    }
    
    ImGui::Separator();
    ImGui::Text("Performance Options");
    
    changed |= ImGui::Checkbox("Calculate Training Profit Factor", &m_calculate_training_pf);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Calculates profit factor on training data for each fold.\nDisabling this can significantly improve performance.");
    }
    
    return changed;
}

bool UniversalConfigWidget::DrawCopyPasteButtons() {
    ImGui::Text("Configuration Copy/Paste");
    ImGui::Separator();
    
    // Show what's currently in clipboard
    if (m_copiedConfig.has_features || m_copiedConfig.has_hyperparameters) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Clipboard Contents:");
        if (m_copiedConfig.has_features) {
            ImGui::BulletText("%zu features copied", m_copiedConfig.features.size());
            ImGui::BulletText("Target: %s", m_copiedConfig.target.c_str());
        }
        if (m_copiedConfig.has_hyperparameters) {
            ImGui::BulletText("%s hyperparameters copied", m_copiedConfig.model_type.c_str());
        }
        ImGui::Separator();
    }
    
    // Copy buttons
    ImGui::Text("Copy:");
    if (ImGui::Button("Copy Features", ImVec2(150, 0))) {
        CopyFeatures();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Copy current configuration:");
        ImGui::BulletText("%zu features selected", m_selectedFeatures.size());
        if (!m_selectedFeatures.empty()) {
            ImGui::BulletText("Features: %s%s", 
                m_selectedFeatures[0].c_str(), 
                m_selectedFeatures.size() > 1 ? ", ..." : "");
        }
        ImGui::BulletText("Target: %s", m_selectedTarget.c_str());
        ImGui::BulletText("Walk-forward settings");
        ImGui::EndTooltip();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Copy Hyperparameters", ImVec2(150, 0))) {
        CopyHyperparameters();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Copy %s hyperparameters", m_selectedModelType.c_str());
        if (m_selectedModelType == "XGBoost" && m_currentConfig.has_value()) {
            try {
                auto xgb = std::any_cast<XGBoostConfig>(m_currentConfig);
                ImGui::BulletText("Max depth: %d", xgb.max_depth);
                ImGui::BulletText("Learning rate: %.3f", xgb.learning_rate);
                ImGui::BulletText("Boost rounds: %d", xgb.num_boost_round);
                ImGui::BulletText("Min child weight: %.1f", xgb.min_child_weight);
            } catch (...) {}
        }
        ImGui::EndTooltip();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Copy All", ImVec2(100, 0))) {
        CopyAll();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Copy everything:");
        ImGui::BulletText("Features & Target");
        ImGui::BulletText("Hyperparameters");
        ImGui::BulletText("Walk-forward settings");
        ImGui::EndTooltip();
    }
    
    ImGui::Separator();
    
    // Paste buttons
    ImGui::Text("Paste:");
    
    // Features paste (always available if copied)
    if (!m_copiedConfig.has_features) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        ImGui::BeginDisabled(true);
    }
    if (ImGui::Button("Paste Features", ImVec2(150, 0))) {
        PasteFeatures();
    }
    if (ImGui::IsItemHovered() && m_copiedConfig.has_features) {
        ImGui::BeginTooltip();
        ImGui::Text("Paste copied configuration:");
        ImGui::BulletText("%zu features", m_copiedConfig.features.size());
        if (!m_copiedConfig.features.empty()) {
            ImGui::BulletText("Features: %s%s", 
                m_copiedConfig.features[0].c_str(), 
                m_copiedConfig.features.size() > 1 ? ", ..." : "");
        }
        ImGui::BulletText("Target: %s", m_copiedConfig.target.c_str());
        ImGui::EndTooltip();
    }
    if (!m_copiedConfig.has_features) {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ %d features", 
                          (int)m_copiedConfig.features.size());
    }
    
    // Hyperparameters paste (only if compatible)
    bool can_paste_hyperparams = CanPasteHyperparameters();
    if (!can_paste_hyperparams) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        ImGui::BeginDisabled(true);
    }
    
    if (ImGui::Button("Paste Hyperparameters", ImVec2(150, 0))) {
        PasteHyperparameters();
    }
    if (ImGui::IsItemHovered() && m_copiedConfig.has_hyperparameters) {
        ImGui::BeginTooltip();
        ImGui::Text("Paste %s hyperparameters", m_copiedConfig.model_type.c_str());
        if (m_copiedConfig.model_type != m_selectedModelType) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Model type mismatch!");
            ImGui::Text("Copied: %s, Current: %s", 
                m_copiedConfig.model_type.c_str(), m_selectedModelType.c_str());
        }
        ImGui::EndTooltip();
    }
    
    if (!can_paste_hyperparams) {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        
        if (m_copiedConfig.has_hyperparameters) {
            ImGui::SameLine();
            if (m_copiedConfig.model_type != m_selectedModelType) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "✗ Incompatible (%s → %s)",
                                  m_copiedConfig.model_type.c_str(),
                                  m_selectedModelType.c_str());
            }
        }
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Compatible");
    }
    
    // Show what's copied
    if (m_copiedConfig.has_features || m_copiedConfig.has_hyperparameters) {
        ImGui::Separator();
        ImGui::Text("Clipboard:");
        if (m_copiedConfig.has_features) {
            ImGui::BulletText("Features: %d selected, Target: %s",
                            (int)m_copiedConfig.features.size(),
                            m_copiedConfig.target.c_str());
        }
        if (m_copiedConfig.has_hyperparameters) {
            ImGui::BulletText("Hyperparameters from: %s",
                            m_copiedConfig.model_type.c_str());
        }
    }

    if (!m_clipboardStatusMessage.empty()) {
        ImGui::Separator();
        const ImVec4 color = m_clipboardStatusSuccess
            ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", m_clipboardStatusMessage.c_str());
    }
    
    return false;
}

void UniversalConfigWidget::UpdateAvailableColumns() {
    if (!m_timeSeriesWindow || !m_timeSeriesWindow->HasData()) {
        return;
    }
    
    const auto* df = m_timeSeriesWindow->GetDataFrame();
    if (!df) return;
    
    // Default feature columns to auto-select
    std::vector<std::string> defaultFeatures = {
        "BOL_WIDTH_M", "CMMA_S", "DTR_RSI_M", "PV_FIT_M", 
        "AROON_DIFF_S", "PCO_10_20", "ADX_L"
    };
    
    // Get all column names
    auto table = df->get_cpu_table();
    auto all_columns = table->ColumnNames();
    
    std::vector<std::string> new_features;
    std::vector<std::string> new_targets;
    
    // Separate features and targets based on prefix
    for (const auto& name : all_columns) {
        if (name.size() >= 3) {
            std::string prefix = name.substr(0, 3);
            std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
            if (prefix == "tgt") {
                new_targets.push_back(name);
            } else {
                new_features.push_back(name);
            }
        } else {
            new_features.push_back(name);
        }
    }
    
    // Update if changed
    if (new_features != m_availableColumns || new_targets != m_availableTargets) {
        m_availableColumns = new_features;
        m_availableTargets = new_targets;
        m_featureCheckboxes.resize(m_availableColumns.size(), false);
        
        // Auto-select default features if this is first time
        if (m_selectedFeatures.empty()) {
            for (size_t i = 0; i < m_availableColumns.size(); ++i) {
                const std::string& columnName = m_availableColumns[i];
                bool isDefault = std::find(defaultFeatures.begin(), defaultFeatures.end(), columnName) != defaultFeatures.end();
                m_featureCheckboxes[i] = isDefault;
                if (isDefault) {
                    m_selectedFeatures.push_back(columnName);
                }
            }
        } else {
            // Preserve previous selections
            for (size_t i = 0; i < m_availableColumns.size(); ++i) {
                auto it = std::find(m_selectedFeatures.begin(), 
                                   m_selectedFeatures.end(), 
                                   m_availableColumns[i]);
                m_featureCheckboxes[i] = (it != m_selectedFeatures.end());
            }
        }
        
        // Auto-select first target if none selected
        if (m_selectedTarget.empty() && !m_availableTargets.empty()) {
            m_selectedTarget = m_availableTargets[0];
        }
    }
}

void UniversalConfigWidget::OnModelTypeChanged(const std::string& new_model_type) {
    // Create new model and widget
    auto model = ModelFactory::CreateModel(new_model_type);
    m_hyperparamWidget = ModelFactory::CreateWidget(new_model_type);
    
    if (model) {
        m_currentConfig = model->CreateDefaultConfig();
    }
}

int UniversalConfigWidget::CalculateMaxFolds() const {
    if (!m_timeSeriesWindow || !m_timeSeriesWindow->HasData()) {
        return 0;
    }
    
    const auto* df = m_timeSeriesWindow->GetDataFrame();
    if (!df) return 0;
    
    int64_t num_rows = df->num_rows();
    
    int required_per_fold = m_walkForwardConfig.train_size + 
                           m_walkForwardConfig.train_test_gap + 
                           m_walkForwardConfig.test_size;
    
    int available_rows = num_rows - m_walkForwardConfig.initial_offset;
    if (available_rows <= required_per_fold) {
        return 0;
    }
    
    int max_folds = 1 + (available_rows - required_per_fold) / m_walkForwardConfig.fold_step;
    return m_walkForwardConfig.start_fold + max_folds - 1;
}

void UniversalConfigWidget::CopyFeatures() {
    RunConfigSerializer::Snapshot snapshot;
    snapshot.modelType = m_selectedModelType;
    snapshot.dataset = m_timeSeriesWindow ? m_timeSeriesWindow->GetSuggestedDatasetId() : std::string();
    snapshot.features = m_selectedFeatures;
    snapshot.target = m_selectedTarget;
    snapshot.walkForward = m_walkForwardConfig;
    snapshot.hasWalkForward = true;
    if (m_useFeatureSchedule && !m_featureSchedule.empty()) {
        snapshot.featureSchedule = m_featureSchedule;
        snapshot.hasFeatureSchedule = true;
    }

    const auto text = RunConfigSerializer::Serialize(
        snapshot,
        RunConfigSerializer::SectionMetadata |
        RunConfigSerializer::SectionFeatures |
        RunConfigSerializer::SectionFeatureSchedule |
        RunConfigSerializer::SectionWalkForward);
    ImGui::SetClipboardText(text.c_str());

    m_copiedConfig.features = snapshot.features;
    m_copiedConfig.target = snapshot.target;
    m_copiedConfig.walk_forward = snapshot.walkForward;
    m_copiedConfig.model_type = snapshot.modelType;
    m_copiedConfig.has_features = !snapshot.features.empty();
    SetClipboardStatus("Features copied to clipboard.", true);
}

void UniversalConfigWidget::CopyHyperparameters() {
    if (!m_currentConfig.has_value()) {
        SetClipboardStatus("No hyperparameters available to copy.", false);
        return;
    }

    try {
        RunConfigSerializer::Snapshot snapshot;
        snapshot.modelType = m_selectedModelType;
        snapshot.dataset = m_timeSeriesWindow ? m_timeSeriesWindow->GetSuggestedDatasetId() : std::string();
        snapshot.hyperparameterType = m_selectedModelType;
        if (m_selectedModelType == "XGBoost") {
            snapshot.xgboost = std::any_cast<XGBoostConfig>(m_currentConfig);
            snapshot.hasHyperparameters = true;
        } else {
            snapshot.hasHyperparameters = false;
        }

        const auto text = RunConfigSerializer::Serialize(
            snapshot,
            RunConfigSerializer::SectionMetadata | RunConfigSerializer::SectionHyperparameters);
        ImGui::SetClipboardText(text.c_str());

        m_copiedConfig.model_type = snapshot.modelType;
        m_copiedConfig.hyperparameters = m_currentConfig;
        m_copiedConfig.has_hyperparameters = true;
        SetClipboardStatus("Hyperparameters copied to clipboard.", true);
    } catch (const std::bad_any_cast&) {
        SetClipboardStatus("Failed to copy hyperparameters.", false);
    }
}

void UniversalConfigWidget::CopyAll() {
    RunConfigSerializer::Snapshot snapshot;
    snapshot.modelType = m_selectedModelType;
    snapshot.dataset = m_timeSeriesWindow ? m_timeSeriesWindow->GetSuggestedDatasetId() : std::string();
    snapshot.features = m_selectedFeatures;
    snapshot.target = m_selectedTarget;
    snapshot.walkForward = m_walkForwardConfig;
    snapshot.hasWalkForward = true;
    if (m_useFeatureSchedule && !m_featureSchedule.empty()) {
        snapshot.featureSchedule = m_featureSchedule;
        snapshot.hasFeatureSchedule = true;
    }

    if (m_currentConfig.has_value() && m_selectedModelType == "XGBoost") {
        try {
            snapshot.xgboost = std::any_cast<XGBoostConfig>(m_currentConfig);
            snapshot.hyperparameterType = "XGBoost";
            snapshot.hasHyperparameters = true;
        } catch (const std::bad_any_cast&) {
        }
    }

    const auto text = RunConfigSerializer::Serialize(
        snapshot,
        RunConfigSerializer::SectionMetadata |
        RunConfigSerializer::SectionFeatures |
        RunConfigSerializer::SectionFeatureSchedule |
        RunConfigSerializer::SectionWalkForward |
        RunConfigSerializer::SectionHyperparameters);
    ImGui::SetClipboardText(text.c_str());

    m_copiedConfig.features = snapshot.features;
    m_copiedConfig.target = snapshot.target;
    m_copiedConfig.walk_forward = snapshot.walkForward;
    m_copiedConfig.model_type = snapshot.modelType;
    m_copiedConfig.has_features = !snapshot.features.empty();
    if (snapshot.hasHyperparameters && snapshot.xgboost.has_value()) {
        m_copiedConfig.hyperparameters = snapshot.xgboost.value();
        m_copiedConfig.has_hyperparameters = true;
    } else if (m_currentConfig.has_value()) {
        m_copiedConfig.hyperparameters = m_currentConfig;
        m_copiedConfig.has_hyperparameters = true;
    }
    SetClipboardStatus("Full configuration copied to clipboard.", true);
}

bool UniversalConfigWidget::CanPasteFeatures() const {
    return m_copiedConfig.has_features || ClipboardContainsSerializableConfig();
}

bool UniversalConfigWidget::CanPasteHyperparameters() const {
    const bool cached = m_copiedConfig.has_hyperparameters &&
        m_copiedConfig.model_type == m_selectedModelType;
    return cached || ClipboardContainsSerializableConfig();
}

void UniversalConfigWidget::PasteFeatures() {
    RunConfigSerializer::Snapshot snapshot;
    std::string parseError;
    const bool parsed = TryParseClipboardSnapshot(&snapshot, &parseError);

    if (!parsed) {
        if (m_copiedConfig.has_features) {
            m_selectedFeatures = m_copiedConfig.features;
            m_selectedTarget = m_copiedConfig.target;
            m_walkForwardConfig = m_copiedConfig.walk_forward;
            for (size_t i = 0; i < m_availableColumns.size(); ++i) {
                auto it = std::find(m_selectedFeatures.begin(), m_selectedFeatures.end(), m_availableColumns[i]);
                m_featureCheckboxes[i] = (it != m_selectedFeatures.end());
            }
            SetClipboardStatus("Pasted stored configuration.", true);
        } else {
            SetClipboardStatus(parseError.empty() ? "Clipboard is empty." : parseError, false);
        }
        return;
    }

    if (!snapshot.features.empty()) {
        m_selectedFeatures = snapshot.features;
    }
    if (!snapshot.target.empty()) {
        m_selectedTarget = snapshot.target;
    }
    if (snapshot.hasWalkForward) {
        m_walkForwardConfig = snapshot.walkForward;
    }
    if (snapshot.hasFeatureSchedule) {
        m_featureSchedule = snapshot.featureSchedule;
        m_useFeatureSchedule = true;
    } else if (!snapshot.featureSchedule.empty()) {
        m_featureSchedule = snapshot.featureSchedule;
        m_useFeatureSchedule = true;
    } else {
        m_useFeatureSchedule = !m_featureSchedule.empty();
    }

    for (size_t i = 0; i < m_availableColumns.size(); ++i) {
        auto it = std::find(m_selectedFeatures.begin(), m_selectedFeatures.end(), m_availableColumns[i]);
        m_featureCheckboxes[i] = (it != m_selectedFeatures.end());
    }

    m_copiedConfig.features = m_selectedFeatures;
    m_copiedConfig.target = m_selectedTarget;
    m_copiedConfig.walk_forward = m_walkForwardConfig;
    m_copiedConfig.model_type = snapshot.modelType.empty() ? m_selectedModelType : snapshot.modelType;
    m_copiedConfig.has_features = !m_selectedFeatures.empty();

    SetClipboardStatus("Features pasted from clipboard.", true);
}

void UniversalConfigWidget::PasteHyperparameters() {
    RunConfigSerializer::Snapshot snapshot;
    std::string parseError;
    const bool parsed = TryParseClipboardSnapshot(&snapshot, &parseError);

    if (parsed && snapshot.hasHyperparameters && snapshot.xgboost.has_value()) {
        if (!snapshot.modelType.empty() && snapshot.modelType != m_selectedModelType) {
            SetClipboardStatus("Clipboard hyperparameters belong to a different model.", false);
            return;
        }
        m_currentConfig = snapshot.xgboost.value();
        m_copiedConfig.hyperparameters = snapshot.xgboost.value();
        m_copiedConfig.has_hyperparameters = true;
        m_copiedConfig.model_type = m_selectedModelType;
        SetClipboardStatus("Hyperparameters pasted from clipboard.", true);
        return;
    }

    if (m_copiedConfig.has_hyperparameters && m_copiedConfig.model_type == m_selectedModelType) {
        m_currentConfig = m_copiedConfig.hyperparameters;
        SetClipboardStatus("Hyperparameters pasted from in-app copy.", true);
        return;
    }

    SetClipboardStatus(parseError.empty() ? "Clipboard does not contain compatible hyperparameters." : parseError, false);
}

bool UniversalConfigWidget::TryParseClipboardSnapshot(RunConfigSerializer::Snapshot* snapshot,
                                                      std::string* error) const {
    if (!snapshot) {
        if (error) *error = "Internal error: snapshot is null.";
        return false;
    }
    const char* clipboard = ImGui::GetClipboardText();
    if (!clipboard || clipboard[0] == '\0') {
        if (error) *error = "Clipboard is empty.";
        return false;
    }
    std::string text(clipboard);
    return RunConfigSerializer::Deserialize(text, snapshot, error);
}

bool UniversalConfigWidget::ClipboardContainsSerializableConfig() const {
    const char* clipboard = ImGui::GetClipboardText();
    return clipboard && RunConfigSerializer::LooksLikeSerializedConfig(clipboard);
}

void UniversalConfigWidget::SetClipboardStatus(const std::string& message, bool success) {
    m_clipboardStatusMessage = message;
    m_clipboardStatusSuccess = success;
}
} // namespace ui
} // namespace simulation

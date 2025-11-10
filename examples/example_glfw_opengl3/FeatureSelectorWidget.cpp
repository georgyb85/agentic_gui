#include "FeatureSelectorWidget.h"
#include <algorithm>
#include <cctype>

FeatureSelectorWidget::FeatureSelectorWidget() {
    m_featureFilterBuffer[0] = '\0';
    m_targetFilterBuffer[0] = '\0';
}

void FeatureSelectorWidget::SetAvailableColumns(const std::vector<std::string>& columns) {
    m_availableColumns = columns;
    m_featureCheckboxes.resize(columns.size(), false);
    UpdateAvailableTargets();
}

bool FeatureSelectorWidget::Draw() {
    bool changed = false;
    
    // Draw in two columns
    ImGui::Columns(2, "FeatureTargetColumns", true);
    
    // Left column - Features
    ImGui::Text("Feature Selection");
    ImGui::Separator();
    changed |= DrawFeatureSelection();
    
    ImGui::NextColumn();
    
    // Right column - Target
    ImGui::Text("Target Selection");
    ImGui::Separator();
    changed |= DrawTargetSelection();
    
    ImGui::Columns(1);
    
    return changed;
}

bool FeatureSelectorWidget::DrawFeatureSelection() {
    bool changed = false;
    
    // Search filter
    if (ImGui::InputText("Filter Features", m_featureFilterBuffer, sizeof(m_featureFilterBuffer))) {
        m_featureFilter = m_featureFilterBuffer;
    }
    
    // Control buttons
    if (ImGui::Button("Select All")) {
        std::string prefixLower = ToLower(m_targetPrefix);
        for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
            const auto& col = m_availableColumns[i];
            // Skip columns that start with target prefix (case-insensitive)
            if (m_showOnlyTargetsWithPrefix) {
                std::string colLower = ToLower(col);
                if (colLower.find(prefixLower) == 0) {
                    continue;
                }
            }
            // Only select visible (filtered) features
            if (PassesFilter(col, m_featureFilter)) {
                m_featureCheckboxes[i] = true;
            }
        }
        changed = true;
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        std::fill(m_featureCheckboxes.begin(), m_featureCheckboxes.end(), false);
        changed = true;
    }
    
    ImGui::SameLine();
    ImGui::Checkbox("Sort A-Z", &m_sortAlphabetically);
    
    // Feature list in scrollable region
    ImGui::BeginChild("FeatureList", ImVec2(0, 300), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    // Prepare display list with filtering
    std::vector<std::pair<std::string, size_t>> displayFeatures;
    std::string prefixLower = ToLower(m_targetPrefix);
    for (size_t i = 0; i < m_availableColumns.size(); ++i) {
        const auto& col = m_availableColumns[i];
        
        // Skip columns that start with target prefix if configured (case-insensitive)
        if (m_showOnlyTargetsWithPrefix) {
            std::string colLower = ToLower(col);
            if (colLower.find(prefixLower) == 0) {
                continue;
            }
        }
        
        // Apply filter
        if (!PassesFilter(col, m_featureFilter)) {
            continue;
        }
        
        displayFeatures.push_back({col, i});
    }
    
    // Sort if requested
    if (m_sortAlphabetically) {
        std::sort(displayFeatures.begin(), displayFeatures.end());
    }
    
    // Display checkboxes
    for (const auto& [colName, idx] : displayFeatures) {
        bool checked = m_featureCheckboxes[idx];
        if (ImGui::Checkbox(colName.c_str(), &checked)) {
            m_featureCheckboxes[idx] = checked;
            changed = true;
        }
    }
    
    ImGui::EndChild();
    
    // Count selected
    int selectedCount = 0;
    for (bool selected : m_featureCheckboxes) {
        if (selected) selectedCount++;
    }
    ImGui::Text("Selected: %d features", selectedCount);
    
    return changed;
}

bool FeatureSelectorWidget::DrawTargetSelection() {
    bool changed = false;
    
    // Search filter for targets
    if (ImGui::InputText("Filter Targets", m_targetFilterBuffer, sizeof(m_targetFilterBuffer))) {
        m_targetFilter = m_targetFilterBuffer;
    }
    
    // Target list in scrollable region
    ImGui::BeginChild("TargetList", ImVec2(0, 330), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    // Prepare display list
    std::vector<std::string> displayTargets;
    for (const auto& target : m_availableTargets) {
        if (PassesFilter(target, m_targetFilter)) {
            displayTargets.push_back(target);
        }
    }
    
    // Sort if requested
    if (m_sortAlphabetically) {
        std::sort(displayTargets.begin(), displayTargets.end());
    }
    
    // Display radio buttons for single selection or checkboxes for multiple
    if (m_allowMultipleTargets) {
        // Multiple target selection (for future use)
        for (const auto& target : displayTargets) {
            bool isSelected = std::find(m_selectedTargets.begin(), m_selectedTargets.end(), target) != m_selectedTargets.end();
            if (ImGui::Checkbox((target + "##target").c_str(), &isSelected)) {
                if (isSelected) {
                    m_selectedTargets.push_back(target);
                } else {
                    m_selectedTargets.erase(
                        std::remove(m_selectedTargets.begin(), m_selectedTargets.end(), target),
                        m_selectedTargets.end()
                    );
                }
                changed = true;
            }
        }
    } else {
        // Single target selection
        for (const auto& target : displayTargets) {
            bool isSelected = (target == m_selectedTarget);
            if (ImGui::RadioButton(target.c_str(), isSelected)) {
                m_selectedTarget = target;
                changed = true;
            }
        }
    }
    
    ImGui::EndChild();
    
    if (m_availableTargets.empty()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No target columns (%s*) available", m_targetPrefix.c_str());
    } else {
        ImGui::Text("Target: %s", m_selectedTarget.c_str());
    }
    
    return changed;
}

std::vector<std::string> FeatureSelectorWidget::GetSelectedFeatures() const {
    std::vector<std::string> selected;
    for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
        if (m_featureCheckboxes[i]) {
            selected.push_back(m_availableColumns[i]);
        }
    }
    return selected;
}

std::string FeatureSelectorWidget::GetSelectedTarget() const {
    return m_selectedTarget;
}

void FeatureSelectorWidget::SetSelectedFeatures(const std::vector<std::string>& features) {
    // Clear all first
    std::fill(m_featureCheckboxes.begin(), m_featureCheckboxes.end(), false);
    
    // Set requested features
    for (const auto& feature : features) {
        auto it = std::find(m_availableColumns.begin(), m_availableColumns.end(), feature);
        if (it != m_availableColumns.end()) {
            size_t idx = std::distance(m_availableColumns.begin(), it);
            m_featureCheckboxes[idx] = true;
        }
    }
}

void FeatureSelectorWidget::SetSelectedTarget(const std::string& target) {
    m_selectedTarget = target;
}

void FeatureSelectorWidget::ClearSelection() {
    std::fill(m_featureCheckboxes.begin(), m_featureCheckboxes.end(), false);
    m_selectedTarget.clear();
    m_selectedTargets.clear();
}

void FeatureSelectorWidget::SelectAllFeatures() {
    std::string prefixLower = ToLower(m_targetPrefix);
    for (size_t i = 0; i < m_featureCheckboxes.size(); ++i) {
        const auto& col = m_availableColumns[i];
        // Skip target columns (case-insensitive)
        if (m_showOnlyTargetsWithPrefix) {
            std::string colLower = ToLower(col);
            if (colLower.find(prefixLower) == 0) {
                continue;
            }
        }
        m_featureCheckboxes[i] = true;
    }
}

void FeatureSelectorWidget::SelectFeaturesMatching(const std::string& pattern) {
    std::string patternLower = ToLower(pattern);
    for (size_t i = 0; i < m_availableColumns.size(); ++i) {
        const auto& col = m_availableColumns[i];
        std::string colLower = ToLower(col);
        if (colLower.find(patternLower) != std::string::npos) {
            m_featureCheckboxes[i] = true;
        }
    }
}

void FeatureSelectorWidget::UpdateAvailableTargets() {
    m_availableTargets.clear();
    
    if (m_showOnlyTargetsWithPrefix) {
        // Only show columns that start with the target prefix (case-insensitive)
        std::string prefixLower = ToLower(m_targetPrefix);
        for (const auto& col : m_availableColumns) {
            std::string colLower = ToLower(col);
            if (colLower.find(prefixLower) == 0) {
                m_availableTargets.push_back(col);
            }
        }
    } else {
        // Show all columns as potential targets
        m_availableTargets = m_availableColumns;
    }
}

bool FeatureSelectorWidget::PassesFilter(const std::string& item, const std::string& filter) const {
    if (filter.empty()) {
        return true;
    }
    
    std::string itemLower = ToLower(item);
    std::string filterLower = ToLower(filter);
    
    return itemLower.find(filterLower) != std::string::npos;
}

std::string FeatureSelectorWidget::ToLower(const std::string& str) const {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}
#pragma once

#include <string>
#include <vector>
#include <functional>
#include "imgui.h"

// Reusable widget for selecting features and targets from a list of columns
class FeatureSelectorWidget {
public:
    FeatureSelectorWidget();
    ~FeatureSelectorWidget() = default;
    
    // Set available columns (called when data source changes)
    void SetAvailableColumns(const std::vector<std::string>& columns);
    
    // Draw the widget, returns true if selection changed
    bool Draw();
    
    // Draw only feature selection
    bool DrawFeatureSelection();
    
    // Draw only target selection  
    bool DrawTargetSelection();
    
    // Get selected features
    std::vector<std::string> GetSelectedFeatures() const;
    
    // Get selected target
    std::string GetSelectedTarget() const;
    
    // Set selected features
    void SetSelectedFeatures(const std::vector<std::string>& features);
    
    // Set selected target
    void SetSelectedTarget(const std::string& target);
    
    // Configuration options
    void SetTargetPrefix(const std::string& prefix) { m_targetPrefix = prefix; }
    void SetShowOnlyTargetsWithPrefix(bool show) { m_showOnlyTargetsWithPrefix = show; }
    void SetSortAlphabetically(bool sort) { m_sortAlphabetically = sort; }
    void SetAllowMultipleTargets(bool allow) { m_allowMultipleTargets = allow; }
    
    // Clear all selections
    void ClearSelection();
    
    // Select all features
    void SelectAllFeatures();
    
    // Select features matching a pattern
    void SelectFeaturesMatching(const std::string& pattern);
    
private:
    // Available columns from data source
    std::vector<std::string> m_availableColumns;
    std::vector<std::string> m_availableTargets;
    
    // Selection state
    std::vector<bool> m_featureCheckboxes;
    std::string m_selectedTarget;
    std::vector<std::string> m_selectedTargets; // For multiple target selection
    
    // Filter/search
    std::string m_featureFilter;
    std::string m_targetFilter;
    char m_featureFilterBuffer[256];
    char m_targetFilterBuffer[256];
    
    // Configuration
    std::string m_targetPrefix = "TGT";
    bool m_showOnlyTargetsWithPrefix = true;
    bool m_sortAlphabetically = true;
    bool m_allowMultipleTargets = false;
    
    // Helper functions
    void UpdateAvailableTargets();
    bool PassesFilter(const std::string& item, const std::string& filter) const;
    std::string ToLower(const std::string& str) const;
};
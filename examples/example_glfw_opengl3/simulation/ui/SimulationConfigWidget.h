#pragma once

#include "../SimulationTypes.h"
#include "../ISimulationModel.h"
#include <memory>
#include <vector>
#include <string>

// Forward declarations
class TimeSeriesWindow;

namespace simulation {
namespace ui {

// Widget for configuring simulation parameters
class SimulationConfigWidget {
public:
    SimulationConfigWidget();
    
    // Set data source for column information
    void SetDataSource(TimeSeriesWindow* tsWindow) { m_timeSeriesWindow = tsWindow; }
    
    // Set the current model for model-specific UI
    void SetModel(ISimulationModel* model) { m_model = model; }
    
    // Get/Set configuration
    void SetConfig(ModelConfigBase* config) { m_config = config; }
    ModelConfigBase* GetConfig() { return m_config; }
    
    void SetWalkForwardConfig(WalkForwardConfig* config) { m_walkForwardConfig = config; }
    WalkForwardConfig* GetWalkForwardConfig() { return m_walkForwardConfig; }
    
    // Render the configuration UI
    // Returns true if any configuration was changed
    bool Draw();
    
    // Copy/Paste functionality
    void CopyConfiguration();
    void PasteConfiguration();
    bool HasCopiedConfiguration() const { return m_hasCopiedConfig; }
    
private:
    // Sub-sections
    bool DrawFeatureSelection();
    bool DrawTargetSelection();
    bool DrawModelHyperparameters();
    bool DrawWalkForwardSettings();
    bool DrawTransformationSettings();
    
    // Helper methods
    void UpdateColumnLists();
    int CalculateMaxFolds() const;
    
    // Data source
    TimeSeriesWindow* m_timeSeriesWindow;
    
    // Current model and configuration
    ISimulationModel* m_model;
    ModelConfigBase* m_config;
    WalkForwardConfig* m_walkForwardConfig;
    
    // Available columns
    std::vector<std::string> m_availableFeatureColumns;
    std::vector<std::string> m_availableTargetColumns;
    std::vector<bool> m_selectedFeatures;
    std::vector<std::pair<std::string, bool>> m_sortedFeatures;
    int m_selectedTargetIndex;
    
    // Copy/Paste state
    bool m_hasCopiedConfig;
    std::unique_ptr<ModelConfigBase> m_copiedConfig;
    WalkForwardConfig m_copiedWalkForward;
    
    // UI state
    bool m_needsColumnUpdate;
    int m_lastDataRows;
};

} // namespace ui
} // namespace simulation
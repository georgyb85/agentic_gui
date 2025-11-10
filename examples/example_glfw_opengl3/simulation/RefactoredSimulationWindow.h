#pragma once

#include "imgui.h"
#include "SimulationEngine.h"
#include "ui/SimulationConfigWidget.h"
#include "ui/SimulationResultsWidget.h"
#include "ui/SimulationControlsWidget.h"
#include <memory>

// Forward declarations
class TimeSeriesWindow;

namespace simulation {

// Refactored SimulationWindow - now just a coordinator
class SimulationWindow {
public:
    SimulationWindow();
    ~SimulationWindow();
    
    // Main draw function
    void Draw();
    
    // Window visibility
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }
    
    // Set data source
    void SetTimeSeriesWindow(TimeSeriesWindow* tsWindow);
    
private:
    // Initialize components
    void InitializeComponents();
    void SetupCallbacks();
    
    // Callback handlers
    void OnStartSimulation();
    void OnStopSimulation();
    void OnResetSimulation();
    void OnModelChanged(const std::string& model_type);
    void OnProgressUpdate(int current, int total);
    void OnFoldComplete(const FoldResult& result);
    void OnSimulationComplete(const SimulationRun& run);
    
    // Create model instance
    std::unique_ptr<ISimulationModel> CreateModel(const std::string& model_type);
    
    // Window state
    bool m_isVisible;
    TimeSeriesWindow* m_timeSeriesWindow;
    
    // Core components
    std::unique_ptr<SimulationEngine> m_engine;
    std::unique_ptr<ISimulationModel> m_currentModel;
    std::unique_ptr<ModelConfigBase> m_currentConfig;
    WalkForwardConfig m_walkForwardConfig;
    
    // UI widgets
    std::unique_ptr<ui::SimulationConfigWidget> m_configWidget;
    std::unique_ptr<ui::SimulationResultsWidget> m_resultsWidget;
    std::unique_ptr<ui::SimulationControlsWidget> m_controlsWidget;
    
    // Layout settings
    float m_configPanelHeight;
    static constexpr float CONFIG_PANEL_MIN_HEIGHT = 300.0f;
};

} // namespace simulation

// For backward compatibility - alias in global namespace
using SimulationWindow = simulation::SimulationWindow;
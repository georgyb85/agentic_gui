#pragma once

#include "../SimulationTypes.h"
#include <deque>
#include <memory>
#include <mutex>
#include "imgui.h"

namespace simulation {
namespace ui {

// Widget for displaying simulation results with the classic layout:
// - Always-visible plot at the top
// - Current run status below the plot
// - Individual tabs for each run with fold details
class SimulationResultsWidget_v2 {
public:
    SimulationResultsWidget_v2();
    ~SimulationResultsWidget_v2();
    
    // Main draw function
    void Draw();
    
    // Add a new run and create a tab for it
    void AddRun(SimulationRun&& run);
    
    // Update current running run with new fold
    void AddFoldResult(const FoldResult& result);
    
    // Update run when completed
    void UpdateCurrentRun(const SimulationRun& run);
    
    // Clear all runs
    void ClearRuns();
    
    // Settings
    void SetAutoScroll(bool enable) { m_autoScrollTable = enable; }
    void SetAutoFitPlot(bool enable) { m_autoFitPlot = enable; }
    
    // Set trade mode for display
    void SetTradeMode(TradeMode mode) { m_tradeMode = mode; }
    TradeMode GetTradeMode() const { return m_tradeMode; }
    
    // Get selected fold for test model
    struct SelectedFoldInfo {
        bool valid = false;
        FoldResult fold;
        std::string runName;
        std::string modelType;
        int runIndex = -1;
    };
    SelectedFoldInfo GetSelectedFold() const;
    void ClearSelectedFold();
    
    // Set reference to config widget for copy/paste
    void SetConfigWidget(class UniversalConfigWidget* widget) { m_configWidget = widget; }
    
    // API for external access
    int GetRunCount() const { return (int)m_simulationRuns.size(); }
    const SimulationRun* GetRunByIndex(int index) const;
    int ConsumePendingSaveRequest();
    void SetSaveStatus(const std::string& message, bool success);
    
private:
    // Draw components
    void DrawProfitPlot();
    void DrawCurrentRunStatus();
    void DrawRunTabs();
    void DrawFoldTable(const SimulationRun& run, size_t runIndex);
    void DrawFoldDetailsPopup();
    void DrawRunPerformanceSummary(const SimulationRun& run, size_t runIndex, float totalPF);
    void DrawCopyButtons(const SimulationRun& run);
    
    // Calculate plot ranges
    void CalculatePlotLimits(double& xMin, double& xMax, double& yMin, double& yMax);
    
    // Data
    std::deque<SimulationRun> m_simulationRuns;
    int m_currentRunIndex;
    mutable std::mutex m_resultsMutex;
    int m_pendingSaveRunIndex = -1;
    std::string m_saveStatusMessage;
    bool m_saveStatusSuccess = true;
    
    // UI state
    bool m_autoScrollTable;
    bool m_autoFitPlot;
    int m_selectedRunTab;
    int m_selectedFoldRow;
    bool m_showFoldDetails;
    TradeMode m_tradeMode = TradeMode::LongOnly;  // Default to long-only
    
    // Selected fold for popup
    SelectedFoldInfo m_selectedFoldInfo;
    
    // Plot colors
    static const ImVec4 PLOT_COLORS[];
    static const int NUM_PLOT_COLORS;
    
    // Reference to config widget for copy/paste
    class UniversalConfigWidget* m_configWidget = nullptr;
};

} // namespace ui
} // namespace simulation

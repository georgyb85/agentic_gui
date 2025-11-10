#pragma once

#include "../SimulationTypes.h"
#include <vector>
#include <mutex>
#include "imgui.h"

namespace simulation {
namespace ui {

// Widget for displaying simulation results
class SimulationResultsWidget {
public:
    SimulationResultsWidget();
    
    // Manage simulation runs
    void AddRun(const SimulationRun& run);
    void UpdateCurrentRun(const SimulationRun& run);
    void ClearRuns();
    
    // Add a fold result to the current run
    void AddFoldResult(const FoldResult& result);
    
    // Render the results UI
    void Draw();
    
    // Configuration
    void SetAutoScroll(bool enable) { m_autoScrollTable = enable; }
    void SetAutoFitPlot(bool enable) { m_autoFitPlot = enable; }
    
    // Set trade mode for display
    void SetTradeMode(TradeMode mode) { m_tradeMode = mode; }
    TradeMode GetTradeMode() const { return m_tradeMode; }
    
    // Get selected run for copying configuration
    const SimulationRun* GetSelectedRun() const;
    int GetSelectedRunIndex() const { return m_selectedRunIndex; }
    int GetRunCount() const { return (int)m_simulationRuns.size(); }
    const SimulationRun* GetRunByIndex(int index) const {
        if (index >= 0 && index < (int)m_simulationRuns.size()) {
            return &m_simulationRuns[index];
        }
        return nullptr;
    }
    
private:
    // Sub-components
    void DrawRunTabs();
    void DrawResultsTable(int runIndex);
    void DrawProfitPlot();
    void DrawRunSummary(const SimulationRun& run);
    void DrawFoldDetails(const FoldResult& fold);
    
    // Helper methods
    ImVec4 GetRunColor(int runIndex) const;
    ImVec4 GetFoldColor(const FoldResult& fold) const;
    std::string FormatDuration(const SimulationRun& run) const;
    
    // Results storage
    mutable std::mutex m_resultsMutex;
    std::vector<SimulationRun> m_simulationRuns;
    int m_currentRunIndex;
    int m_selectedRunIndex;
    bool m_resultsUpdated;
    
    // UI state
    bool m_autoScrollTable;
    bool m_autoFitPlot;
    float m_resultsPanelHeight;
    int m_selectedFoldIndex;
    TradeMode m_tradeMode = TradeMode::LongOnly;  // Default to long-only
    
    // Plot settings
    static constexpr int MAX_VISIBLE_RESULTS = 200;
    static constexpr float RESULTS_PANEL_MIN_HEIGHT = 200.0f;
};

} // namespace ui
} // namespace simulation
#pragma once

#include "imgui.h"
#include "analytics_dataframe.h"
#include "bivariate_analysis_exact.h"
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <atomic>

// Forward declarations
class TimeSeriesWindow;

class BivarAnalysisWidget {
public:
    BivarAnalysisWidget();
    ~BivarAnalysisWidget() = default;
    
    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }
    
    void SetDataSource(const TimeSeriesWindow* dataSource);
    void UpdateColumnList();
    
private:
    void DrawColumnSelection();
    void DrawParameterSettings();
    void DrawRunControls();
    void DrawResultsTable();
    void DrawStatusBar();
    
    void RunAnalysis();
    void ClearResults();
    
    // Window state
    bool m_isVisible;
    
    // Data source
    const TimeSeriesWindow* m_dataSource;
    
    // Column selection
    std::vector<std::string> m_availableColumns;
    std::vector<bool> m_selectedPredictors;
    int m_selectedTargetIndex;
    
    // Analysis parameters
    int m_numPredictorBins;
    int m_numTargetBins;
    int m_criterionType;      // 1=MI, 2=uncertainty_reduction
    int m_mcptType;           // 0=none, 1=complete, 2=cyclic
    int m_numPermutations;
    
    // Analysis state
    std::atomic<bool> m_isRunning;
    std::future<std::vector<BivariateResult>> m_analysisFuture;
    std::vector<BivariateResult> m_results;
    std::string m_statusMessage;
    bool m_hasResults;
    
    // UI state
    int m_maxResultsToShow;
    bool m_sortDescending;
    
    // UI layout constants
    static constexpr float COLUMN_SELECTION_HEIGHT = 200.0f;
    static constexpr float PARAMETER_SECTION_HEIGHT = 120.0f;
    static constexpr float STATUS_BAR_HEIGHT = 25.0f;
};
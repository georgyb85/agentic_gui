#pragma once

#include "imgui.h"
#include <vector>
#include <string>
#include <memory>
#include <future>
#include <atomic>
#include "FeatureSelectorWidget.h"

// Forward declarations
class TimeSeriesWindow;
class AnalyticsDataFrame;

class LFSWindow {
public:
    LFSWindow();
    ~LFSWindow();
    
    // Window management
    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }
    
    // Data source
    void SetDataSource(const TimeSeriesWindow* dataSource);
    void UpdateColumnList();
    
private:
    // UI state
    bool m_isVisible;
    const TimeSeriesWindow* m_dataSource;
    
    // Feature selection
    FeatureSelectorWidget m_featureSelector;
    std::vector<std::string> m_availableColumns;
    
    // LFS parameters
    int m_maxKept;
    int m_iterations;
    int m_nRand;
    int m_nBeta;
    int m_maxThreads;
    int m_solverType;  // 0 = Legacy, 1 = HiGHS
    bool m_useCUDA;
    int m_targetBins;   // Number of bins for target discretization
    
    // MCPT parameters
    int m_mcptReps;     // Number of MCPT replications (0 = disabled)
    int m_mcptType;     // 0 = None, 1 = Complete, 2 = Cyclic
    
    // Data range parameters
    int m_startRow;     // Starting row for analysis
    int m_endRow;       // Ending row for analysis (-1 = all rows)
    
    // Analysis state
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_stopRequested;
    std::future<void> m_analysisFuture;
    std::atomic<float> m_progress;
    std::string m_progressText;
    bool m_hasResults;
    
    // Results storage
    std::string m_resultsBuffer;  // Text buffer for results display
    
    // Data storage for LFS
    double* m_database;
    int m_n_cases;
    int m_n_vars;
    
    // Helper functions
    void StartAnalysis();
    void RunLFSAnalysis(const std::vector<std::string>& features, const std::string& target);
    void ProcessResults(int* f_binary, const std::vector<std::string>& selectedFeatures);
    void ProcessMCPTResults(const std::vector<std::string>& selectedFeatures,
                           const std::vector<double>& originalCrits,
                           const std::vector<int>& mcptSolo,
                           const std::vector<int>& mcptBestof);
    bool PrepareData(const std::vector<std::string>& features, const std::string& target);
    void AppendToResults(const std::string& text);
    void ShuffleTargetComplete(double* data, int nCases, int nVars);
    void ShuffleTargetCyclic(double* data, int nCases, int nVars);
    std::vector<int> DiscretizeTarget(const std::vector<double>& targetValues, int nbins);
};
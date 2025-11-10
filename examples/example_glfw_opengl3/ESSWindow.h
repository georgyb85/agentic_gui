#pragma once

#include "imgui.h"
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <atomic>
#include <mutex>
#include <sstream>
#include "stepwise/enhanced_stepwise.h"
#include "stepwise/enhanced_stepwise_v2.h"
#include "stepwise/data_matrix.h"
#include "stepwise/model_interface.h"
#include "stepwise/xgboost_config_widget.h"
#include "stepwise/linear_quadratic_model_wrapper.h"
#include "stepwise/xgboost_model.h"
// Option to use existing simulation XGBoost widget instead:
// #include "simulation/models/XGBoostWidget.h"
#include "FeatureSelectorWidget.h"

// Forward declarations
class TimeSeriesWindow;

// Enhanced Stepwise Selection Window
class ESSWindow {
public:
    ESSWindow();
    ~ESSWindow() = default;
    
    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }
    
    void SetDataSource(const TimeSeriesWindow* dataSource);
    void UpdateColumnList();
    
private:
    // UI Drawing functions
    void DrawColumnSelection();
    void DrawModelSelection();
    void DrawConfigurationSettings();
    void DrawRunControls();
    void DrawResultsOutput();
    void DrawStatusBar();
    
    // Analysis functions
    void RunAnalysis();
    void RunAnalysisOnSubsets();
    void ClearResults();
    
    // Data preparation
    void PrepareDataForAnalysis(DataMatrix& X, std::vector<double>& y, 
                                int startRow = -1, int endRow = -1);
    
    // Window state
    bool m_isVisible;
    
    // Data source
    const TimeSeriesWindow* m_dataSource;
    
    // Feature selector widget
    FeatureSelectorWidget m_featureSelector;
    
    // Column selection (still needed for backward compatibility)
    std::vector<std::string> m_availableColumns;
    
    // Model selection
    stepwise::ModelType m_modelType;  // Selected model type
    stepwise::XGBoostConfigWidget m_xgboostConfigWidget;  // XGBoost configuration widget
    
    // Configuration settings (matching StepwiseConfig)
    int m_nKept;                    // Number of best feature sets per step
    int m_nFolds;                   // Cross-validation folds
    int m_minPredictors;            // Minimum predictors
    int m_maxPredictors;            // Maximum predictors  
    int m_mcptReplications;         // Monte Carlo permutation test replications
    int m_mcptType;                 // 0=COMPLETE, 1=CYCLIC
    bool m_earlyTermination;        // Enable early termination
    
    // Subset configuration
    bool m_useSubsets;              // Whether to split data into subsets
    int m_numSubsets;               // Number of subsets to create
    int m_subsetSize;                // Fixed size for each subset (0 = auto-calculate)
    int m_overlapSize;              // Number of data points to overlap between subsets
    bool m_globalStandardization;   // Whether to standardize globally or per subset
    
    // Analysis state
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_stopRequested;
    std::future<void> m_analysisFuture;
    
    // Results
    struct SubsetResult {
        int subsetIndex;
        int startRow;
        int endRow;
        EnhancedStepwise::StepwiseResults results;  // For backward compatibility
        stepwise::EnhancedStepwiseV2::StepwiseResults results_v2;  // For new model interface
        bool use_v2;  // Flag to indicate which results to use
    };
    std::vector<SubsetResult> m_subsetResults;
    std::stringstream m_resultsText;
    std::string m_resultsString;  // Cached string for display
    std::vector<char> m_resultsBuffer;  // Buffer for ImGui display
    
    // Feature schedule output
    std::stringstream m_featureScheduleText;
    std::string m_featureScheduleString;
    std::vector<char> m_featureScheduleBuffer;
    
    std::mutex m_bufferMutex;  // Mutex for thread-safe buffer access
    bool m_hasResults;
    
    // Status
    std::string m_statusMessage;
    
    // UI state
    bool m_autoScroll;
    
    // UI layout constants
    static constexpr float COLUMN_SELECTION_HEIGHT = 250.0f;
    static constexpr float CONFIG_SECTION_HEIGHT = 200.0f;
    static constexpr float STATUS_BAR_HEIGHT = 25.0f;
};
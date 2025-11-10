#pragma once

#include "SimulationTypes.h"
#include <memory>
#include <vector>
#include <string>
#include <any>

// Forward declarations
class TimeSeriesWindow;
class FeatureSelectorWidget;
namespace simulation {
    class ISimulationModel;
}

namespace simulation {

// Proper TestModelWindow that matches the original implementation
class TestModelWindow {
public:
    TestModelWindow();
    ~TestModelWindow();
    
    // Main draw function - NOT a modal, just regular ImGui content
    void Draw();
    
    // Configuration
    void SetFromFold(const FoldResult& fold, const SimulationRun& run);
    void SetDataSource(TimeSeriesWindow* tsWindow) { m_timeSeriesWindow = tsWindow; }
    void Clear();
    
    // Visibility
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }
    
private:
    // Plotting functions from original code
    void PlotFeatureImportance();
    void PlotPredictionScatter();
    void PlotPredictionHistogram();
    void PlotROCCurve();
    
    // Run the test model
    void RunTestModel();
    
    // Recalculate metrics when threshold changes
    void RecalculateMetricsWithThreshold();
    
    // ROC and profit factor calculations
    void CalculateROCData();
    void CalculateOptimalThreshold();
    void CalculateProfitFactors();
    
    // State
    bool m_isVisible;
    bool m_hasConfiguration;
    bool m_hasResults;
    
    // Configuration from fold - matches original TestModelState
    struct TestConfig {
        // Source tracking
        FoldResult sourceFold;
        std::string sourceRunName;
        std::string sourceModelType;
        
        // Data ranges
        int trainStart = 6000;
        int trainEnd = 16000;
        int testStart = 16000;
        int testEnd = 16200;
        
        // Model configuration
        float originalThreshold = 0.0f;
        std::string modelType;
        std::shared_ptr<ModelConfigBase> modelConfig;
        
        // Transformation parameters from original fold
        float transformMean = 0.0f;
        float transformStd = 1.0f;
        float tanhScalingFactor = 1.0f;
        bool transformParamsPreserved = false;  // True when using params from fold
        
        // Features from the fold (for initialization)
        std::vector<std::string> foldFeatures;
        std::string targetColumn;
    } m_config;
    
    // Results - matches original exactly
    struct TestResults {
        // Core results
        int signalsGenerated = 0;
        float hitRate = 0.0f;
        float accuracyAboveThreshold = 0.0f;
        float totalReturn = 0.0f;
        
        // Data for plotting
        std::vector<float> predictions;
        std::vector<float> actuals;
        std::vector<std::pair<std::string, float>> featureImportance;
        
        // ROC curve data
        std::vector<float> rocFPR;  // False positive rates
        std::vector<float> rocTPR;  // True positive rates
        std::vector<float> rocThresholds;  // Corresponding thresholds
        float rocAUC = 0.0f;  // Area under ROC curve
        float optimalROCThreshold = 0.0f;  // Optimal threshold from ROC analysis
        
        // Profit factors for 95th percentile threshold (long-only)
        float trainProfitFactor = 0.0f;
        float testProfitFactor = 0.0f;
        float trainProfitFactorLongOnly = 0.0f;
        float testProfitFactorLongOnly = 0.0f;
        
        // Profit factors for optimal ROC threshold (long-only)
        float trainProfitFactorOptimal = 0.0f;
        float testProfitFactorOptimal = 0.0f;
        
        // Short-only profit factors (5th percentile threshold)
        float trainProfitFactorShortOnly5th = 0.0f;
        float testProfitFactorShortOnly5th = 0.0f;
        
        // Short-only profit factors (optimal short threshold)
        float trainProfitFactorShortOnlyOptimal = 0.0f;
        float testProfitFactorShortOnlyOptimal = 0.0f;
        
        // Short thresholds
        float shortThreshold5th = 0.0f;  // 5th percentile threshold
        float optimalShortThreshold = 0.0f;  // Optimal threshold for short trades
        
        // R-squared
        float rSquared = 0.0f;
        
        // Train predictions for profit factor calculation
        std::vector<float> trainPredictions;
        std::vector<float> trainActuals;
        
        // Status
        bool success = false;
        std::string errorMessage;
    } m_results;
    
    // Model (would be XGBoost BoosterHandle in full implementation)
    std::unique_ptr<ISimulationModel> m_model;
    
    // Data source
    TimeSeriesWindow* m_timeSeriesWindow;
    
    // Feature selector widget
    std::unique_ptr<FeatureSelectorWidget> m_featureSelector;
};

} // namespace simulation
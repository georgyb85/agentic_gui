#pragma once

#include "../SimulationTypes.h"
#include "../ISimulationModel_v2.h"
#include "../../RunConfigSerializer.h"
#include <memory>
#include <vector>
#include <string>
#include <any>
#include <map>

// Forward declarations
class TimeSeriesWindow;

namespace simulation {
namespace ui {

// Universal configuration widget that works with any model type
class UniversalConfigWidget {
public:
    UniversalConfigWidget();
    ~UniversalConfigWidget();
    
    // Set data source for column information
    void SetDataSource(TimeSeriesWindow* tsWindow);
    
    // Model management
    void SetAvailableModels(const std::map<std::string, std::vector<std::string>>& models_by_category);
    std::string GetSelectedModelType() const { return m_selectedModelType; }
    
    // Configuration management
    void SetConfig(const std::any& config) { m_currentConfig = config; }
    std::any GetConfig() const { return m_currentConfig; }
    
    // Feature/Target management (universal across all models)
    void SetFeatures(const std::vector<std::string>& features) { m_selectedFeatures = features; }
    std::vector<std::string> GetFeatures() const;
    
    void SetTarget(const std::string& target) { m_selectedTarget = target; }
    std::string GetTarget() const;
    
    // Feature schedule for walk-forward with dynamic feature selection
    void SetFeatureSchedule(const std::string& schedule) { m_featureSchedule = schedule; m_useFeatureSchedule = !schedule.empty(); }
    std::string GetFeatureSchedule() const { return m_featureSchedule; }
    bool IsUsingFeatureSchedule() const { return m_useFeatureSchedule; }
    std::vector<std::string> GetFeaturesForRange(int startRow, int endRow) const;
    
    // Walk-forward configuration (universal)
    void SetWalkForwardConfig(const WalkForwardConfig& config) { m_walkForwardConfig = config; }
    WalkForwardConfig GetWalkForwardConfig() const { return m_walkForwardConfig; }
    
    // Performance options
    bool GetCalculateTrainingPF() const { return m_calculate_training_pf; }
    
    
    // Draw the configuration UI
    // Returns true if any configuration was changed
    bool Draw();
    
    // Copy/Paste functionality with elegant handling of different model types
    struct CopiedConfiguration {
        // Universal parts
        std::vector<std::string> features;
        std::string target;
        WalkForwardConfig walk_forward;
        
        // Model-specific parts
        std::string model_type;
        std::any hyperparameters;
        
        bool has_features = false;
        bool has_hyperparameters = false;
    };
    
    void CopyFeatures();
    void CopyHyperparameters();
    void CopyAll();
    
    bool CanPasteFeatures() const;
    bool CanPasteHyperparameters() const;
    
    void PasteFeatures();
    void PasteHyperparameters();
    
    const CopiedConfiguration& GetCopiedConfig() const { return m_copiedConfig; }
    void SetCopiedConfig(const CopiedConfiguration& config) { m_copiedConfig = config; }
    
private:
    // Sub-sections
    bool DrawModelSelection();
    bool DrawFeatureTargetSelection();
    bool DrawHyperparameters();
    bool DrawWalkForwardSettings();
    bool DrawCopyPasteButtons();
    
    // Helper methods
    void UpdateAvailableColumns();
    void OnModelTypeChanged(const std::string& new_model_type);
    int CalculateMaxFolds() const;
    bool TryParseClipboardSnapshot(RunConfigSerializer::Snapshot* snapshot, std::string* error) const;
    bool ClipboardContainsSerializableConfig() const;
    void SetClipboardStatus(const std::string& message, bool success);
    
    // Data source
    TimeSeriesWindow* m_timeSeriesWindow;
    
    // Model selection
    std::map<std::string, std::vector<std::string>> m_modelsByCategory;
    std::string m_selectedModelType;
    int m_selectedCategoryIndex;
    int m_selectedModelIndex;
    
    // Current configuration
    std::any m_currentConfig;
    std::unique_ptr<IModelConfigWidget> m_hyperparamWidget;
    
    // Feature/Target selection (universal)
    std::vector<std::string> m_availableColumns;
    std::vector<std::string> m_availableTargets;  // Columns with tgt_ prefix
    std::vector<std::string> m_selectedFeatures;
    std::string m_selectedTarget;
    std::vector<bool> m_featureCheckboxes;
    
    // Feature schedule support
    std::string m_featureSchedule;
    bool m_useFeatureSchedule;
    
    // Walk-forward configuration (universal)
    WalkForwardConfig m_walkForwardConfig;
    
    // Performance options
    bool m_calculate_training_pf = true;  // Default to on for compatibility
    
    
    // Copy/Paste state
    CopiedConfiguration m_copiedConfig;
    std::string m_clipboardStatusMessage;
    bool m_clipboardStatusSuccess = false;
    
    // UI state
    bool m_showAdvancedOptions;
    bool m_sortFeaturesAlphabetically;
    std::string m_searchFilter;
};

// Specialized widget for Test Model functionality
class TestModelWidget {
public:
    TestModelWidget();
    
    // Configuration source
    enum ConfigSource {
        MANUAL,       // User manually configures
        FROM_FOLD,    // Copy from a simulation fold
        FROM_COPIED   // Use copied configuration
    };
    
    void SetConfigSource(ConfigSource source) { m_configSource = source; }
    ConfigSource GetConfigSource() const { return m_configSource; }
    
    // Set configuration from a fold result
    void SetFromFold(const FoldResult& fold, const std::any& config, const std::string& model_type);
    
    // Manual configuration
    void SetManualConfig(
        int train_start, int train_end,
        int test_start, int test_end,
        const std::any& model_config
    );
    
    // Get test configuration
    struct TestConfig {
        int train_start, train_end;
        int test_start, test_end;
        std::any model_config;
        std::string model_type;
        float threshold;  // Trading threshold
        bool threshold_manually_set;
    };
    TestConfig GetTestConfig() const { return m_testConfig; }
    
    // Draw the test model UI
    bool Draw();
    
    // Results display
    void SetResults(
        const std::vector<float>& predictions,
        const std::vector<float>& actuals,
        const std::map<std::string, float>& metrics
    );
    
private:
    void DrawConfigurationSection();
    void DrawResultsSection();
    void DrawMetricsTable();
    void DrawPredictionPlot();
    void DrawThresholdAnalysis();
    
    ConfigSource m_configSource;
    TestConfig m_testConfig;
    
    // Results
    std::vector<float> m_predictions;
    std::vector<float> m_actuals;
    std::map<std::string, float> m_metrics;
    
    // UI state
    bool m_showPredictionPlot;
    bool m_showThresholdAnalysis;
    float m_plotHeight;
};

} // namespace ui
} // namespace simulation

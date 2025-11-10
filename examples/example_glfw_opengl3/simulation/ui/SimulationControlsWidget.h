#pragma once

#include <functional>
#include <string>
#include <vector>
#include "imgui.h"

namespace simulation {
namespace ui {

// Run modes for simulation
enum class RunMode {
    WalkForward,
    SingleFold
};

// Widget for simulation controls (start/stop, progress, model selection)
class SimulationControlsWidget {
public:
    using StartCallback = std::function<void()>;
    using StopCallback = std::function<void()>;
    using ResetCallback = std::function<void()>;
    using ModelChangeCallback = std::function<void(const std::string& model_type)>;
    using SettingsChangeCallback = std::function<void()>;
    using ClearCacheCallback = std::function<void()>;
    
    SimulationControlsWidget();
    
    // Set callbacks
    void SetStartCallback(StartCallback cb) { m_startCallback = cb; }
    void SetStopCallback(StopCallback cb) { m_stopCallback = cb; }
    void SetResetCallback(ResetCallback cb) { m_resetCallback = cb; }
    void SetModelChangeCallback(ModelChangeCallback cb) { m_modelChangeCallback = cb; }
    void SetSettingsChangeCallback(SettingsChangeCallback cb) { m_settingsChangeCallback = cb; }
    void SetClearCacheCallback(ClearCacheCallback cb) { m_clearCacheCallback = cb; }
    
    // Set available models
    void SetAvailableModels(const std::vector<std::string>& models);
    void SetCurrentModel(const std::string& model);
    
    // Update state
    void SetRunning(bool running);
    void SetProgress(int current, int total);
    void SetStatusMessage(const std::string& message);
    void SetCurrentOperation(const std::string& operation);
    void UpdateElapsedTime(float deltaTime);
    void ResetTimer();
    
    // Get settings
    std::string GetSelectedModel() const;
    RunMode GetRunMode() const;
    int GetSingleFoldNumber() const;
    bool IsModelCachingEnabled() const;
    bool IsParallelProcessingEnabled() const;
    int GetMaxThreads() const;
    
    // Render the controls
    void Draw();
    
private:
    // Drawing methods
    void DrawMainControls();
    void DrawStatusBar();
    void DrawAdvancedOptions();
    
    // Utilities
    std::string FormatTime(float seconds) const;
    float GetMemoryUsageMB() const;
    
    // Callbacks
    StartCallback m_startCallback;
    StopCallback m_stopCallback;
    ResetCallback m_resetCallback;
    ModelChangeCallback m_modelChangeCallback;
    SettingsChangeCallback m_settingsChangeCallback;
    ClearCacheCallback m_clearCacheCallback;
    
    // State
    bool m_isRunning;
    int m_currentProgress;
    int m_totalProgress;
    std::string m_statusMessage;
    std::string m_currentOperation;
    float m_elapsedTime;
    
    // Model selection
    std::vector<std::string> m_availableModels;
    int m_selectedModelIndex;
    
    // Settings
    RunMode m_runMode;
    int m_singleFoldNumber;
    bool m_enableModelCaching;
    bool m_enableParallelProcessing;
    int m_maxThreads;
    bool m_stopOnError;
    bool m_verboseLogging;
    bool m_saveIntermediateResults;
};

} // namespace ui
} // namespace simulation
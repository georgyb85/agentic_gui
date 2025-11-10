#include "SimulationControlsWidget.h"
#include "imgui.h"
#include "../ISimulationModel_v2.h"
#include <sstream>
#include <iomanip>

namespace simulation {
namespace ui {

SimulationControlsWidget::SimulationControlsWidget()
    : m_isRunning(false)
    , m_currentProgress(0)
    , m_totalProgress(0)
    , m_statusMessage("Ready")
    , m_elapsedTime(0.0f)
    , m_selectedModelIndex(0)
    , m_runMode(RunMode::WalkForward)
    , m_singleFoldNumber(0)
    , m_enableModelCaching(true)
    , m_enableParallelProcessing(false)
    , m_maxThreads(4) {
}

void SimulationControlsWidget::Draw() {
    // Main control bar
    DrawMainControls();
    
    // Status and progress
    DrawStatusBar();
    
    // Advanced options (collapsible)
    if (ImGui::TreeNode("Advanced Options")) {
        DrawAdvancedOptions();
        ImGui::TreePop();
    }
}

void SimulationControlsWidget::DrawMainControls() {
    // Model selector
    const char* preview = m_selectedModelIndex < m_availableModels.size() ? 
                          m_availableModels[m_selectedModelIndex].c_str() : 
                          "Select Model...";
    
    ImGui::PushItemWidth(200);
    if (ImGui::BeginCombo("Model", preview)) {
        for (size_t i = 0; i < m_availableModels.size(); ++i) {
            bool isSelected = (m_selectedModelIndex == i);
            if (ImGui::Selectable(m_availableModels[i].c_str(), isSelected)) {
                m_selectedModelIndex = i;
                if (m_modelChangeCallback) {
                    m_modelChangeCallback(m_availableModels[i]);
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    
    // Run mode selector
    ImGui::PushItemWidth(150);
    const char* runModeStr = (m_runMode == RunMode::WalkForward) ? 
                             "Walk-Forward" : "Single Fold";
    if (ImGui::BeginCombo("Mode", runModeStr)) {
        if (ImGui::Selectable("Walk-Forward", m_runMode == RunMode::WalkForward)) {
            m_runMode = RunMode::WalkForward;
        }
        if (ImGui::Selectable("Single Fold", m_runMode == RunMode::SingleFold)) {
            m_runMode = RunMode::SingleFold;
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    
    // Single fold number (only when in SingleFold mode)
    if (m_runMode == RunMode::SingleFold) {
        ImGui::SameLine();
        ImGui::PushItemWidth(80);
        ImGui::InputInt("Fold#", &m_singleFoldNumber);
        m_singleFoldNumber = std::max(0, m_singleFoldNumber);
        ImGui::PopItemWidth();
    }
    
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0)); // Spacing
    ImGui::SameLine();
    
    // Control buttons
    if (!m_isRunning) {
        // Start button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
        if (ImGui::Button("Start Simulation", ImVec2(120, 0))) {
            if (m_startCallback) {
                m_startCallback();
            }
        }
        ImGui::PopStyleColor(2);
        
        ImGui::SameLine();
        
        // Reset button
        if (ImGui::Button("Reset", ImVec2(60, 0))) {
            if (m_resetCallback) {
                m_resetCallback();
            }
        }
    } else {
        // Stop button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Button("Stop Simulation", ImVec2(120, 0))) {
            if (m_stopCallback) {
                m_stopCallback();
            }
        }
        ImGui::PopStyleColor(2);
    }
    
    // Quick settings
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();
    
    ImGui::Checkbox("Model Caching", &m_enableModelCaching);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reuse last successful model when a fold fails to learn");
    }
    
    ImGui::SameLine();
    ImGui::Checkbox("Parallel", &m_enableParallelProcessing);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable parallel processing for faster execution");
    }
}

void SimulationControlsWidget::DrawStatusBar() {
    ImGui::Separator();
    
    // Progress bar
    if (m_isRunning && m_totalProgress > 0) {
        float progress = (float)m_currentProgress / m_totalProgress;
        
        // Custom progress bar with text overlay
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
        
        std::ostringstream progressText;
        progressText << "Fold " << m_currentProgress << " / " << m_totalProgress 
                     << " (" << std::fixed << std::setprecision(1) 
                     << (progress * 100.0f) << "%)";
        
        ImGui::ProgressBar(progress, ImVec2(-1, 0), progressText.str().c_str());
        ImGui::PopStyleColor();
        
        // Time estimate
        if (m_currentProgress > 0) {
            float timePerFold = m_elapsedTime / m_currentProgress;
            float remainingTime = timePerFold * (m_totalProgress - m_currentProgress);
            
            ImGui::Text("Elapsed: %s | Remaining: %s (est.)", 
                       FormatTime(m_elapsedTime).c_str(),
                       FormatTime(remainingTime).c_str());
        }
    }
    
    // Status message
    ImVec4 statusColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (m_statusMessage.find("Error") != std::string::npos || 
        m_statusMessage.find("Failed") != std::string::npos) {
        statusColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red
    } else if (m_statusMessage.find("Warning") != std::string::npos) {
        statusColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); // Orange
    } else if (m_statusMessage.find("Completed") != std::string::npos) {
        statusColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); // Green
    }
    
    ImGui::TextColored(statusColor, "Status: %s", m_statusMessage.c_str());
    
    // Current operation (if running)
    if (m_isRunning && !m_currentOperation.empty()) {
        ImGui::Text("Current: %s", m_currentOperation.c_str());
    }
}

void SimulationControlsWidget::DrawAdvancedOptions() {
    // Performance settings
    ImGui::Text("Performance Settings:");
    ImGui::Indent();
    
    if (ImGui::Checkbox("Enable Parallel Processing", &m_enableParallelProcessing)) {
        if (m_settingsChangeCallback) {
            m_settingsChangeCallback();
        }
    }
    
    if (m_enableParallelProcessing) {
        ImGui::SameLine();
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("Max Threads", &m_maxThreads)) {
            m_maxThreads = std::max(1, std::min(m_maxThreads, 32));
            if (m_settingsChangeCallback) {
                m_settingsChangeCallback();
            }
        }
        ImGui::PopItemWidth();
    }
    
    ImGui::Unindent();
    
    ImGui::Separator();
    
    // Simulation behavior
    ImGui::Text("Simulation Behavior:");
    ImGui::Indent();
    
    if (ImGui::Checkbox("Stop on Error", &m_stopOnError)) {
        if (m_settingsChangeCallback) {
            m_settingsChangeCallback();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stop simulation if an error occurs during training");
    }
    
    if (ImGui::Checkbox("Verbose Logging", &m_verboseLogging)) {
        if (m_settingsChangeCallback) {
            m_settingsChangeCallback();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable detailed logging output");
    }
    
    if (ImGui::Checkbox("Save Intermediate Results", &m_saveIntermediateResults)) {
        if (m_settingsChangeCallback) {
            m_settingsChangeCallback();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Save results after each fold completes");
    }
    
    ImGui::Unindent();
    
    ImGui::Separator();
    
    // Memory management
    ImGui::Text("Memory Management:");
    ImGui::Indent();
    
    if (ImGui::Button("Clear Model Cache")) {
        if (m_clearCacheCallback) {
            m_clearCacheCallback();
        }
        m_statusMessage = "Model cache cleared";
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Garbage Collect")) {
        // Trigger garbage collection if needed
        m_statusMessage = "Memory cleaned up";
    }
    
    ImGui::Text("Memory Usage: %.1f MB", GetMemoryUsageMB());
    
    ImGui::Unindent();
}

void SimulationControlsWidget::SetAvailableModels(const std::vector<std::string>& models) {
    m_availableModels = models;
    if (!models.empty() && m_selectedModelIndex >= models.size()) {
        m_selectedModelIndex = 0;
    }
}

void SimulationControlsWidget::SetRunning(bool running) {
    m_isRunning = running;
    if (!running) {
        m_currentOperation.clear();
    }
}

void SimulationControlsWidget::SetProgress(int current, int total) {
    m_currentProgress = current;
    m_totalProgress = total;
}

void SimulationControlsWidget::SetStatusMessage(const std::string& message) {
    m_statusMessage = message;
}

void SimulationControlsWidget::SetCurrentOperation(const std::string& operation) {
    m_currentOperation = operation;
}

void SimulationControlsWidget::UpdateElapsedTime(float deltaTime) {
    if (m_isRunning) {
        m_elapsedTime += deltaTime;
    }
}

void SimulationControlsWidget::ResetTimer() {
    m_elapsedTime = 0.0f;
}

std::string SimulationControlsWidget::GetSelectedModel() const {
    if (m_selectedModelIndex < m_availableModels.size()) {
        return m_availableModels[m_selectedModelIndex];
    }
    return "";
}

RunMode SimulationControlsWidget::GetRunMode() const {
    return m_runMode;
}

int SimulationControlsWidget::GetSingleFoldNumber() const {
    return m_singleFoldNumber;
}

bool SimulationControlsWidget::IsModelCachingEnabled() const {
    return m_enableModelCaching;
}

bool SimulationControlsWidget::IsParallelProcessingEnabled() const {
    return m_enableParallelProcessing;
}

int SimulationControlsWidget::GetMaxThreads() const {
    return m_maxThreads;
}

std::string SimulationControlsWidget::FormatTime(float seconds) const {
    int mins = (int)(seconds / 60);
    int secs = (int)seconds % 60;
    
    std::ostringstream oss;
    if (mins > 0) {
        oss << mins << "m ";
    }
    oss << secs << "s";
    return oss.str();
}

float SimulationControlsWidget::GetMemoryUsageMB() const {
    // Platform-specific memory usage calculation
    // This is a placeholder - actual implementation would be platform-specific
#ifdef _WIN32
    // Windows implementation
    return 0.0f; // Placeholder
#else
    // Linux/Mac implementation
    return 0.0f; // Placeholder
#endif
}

} // namespace ui
} // namespace simulation
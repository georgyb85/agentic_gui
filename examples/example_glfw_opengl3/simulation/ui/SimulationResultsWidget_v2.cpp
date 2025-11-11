#include "SimulationResultsWidget_v2.h"
#include "UniversalConfigWidget.h"
#include "../XGBoostConfig.h"
#include "../../RunConfigSerializer.h"
#include "imgui.h"
#include "../../implot.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include <float.h>
#include <iostream>

namespace simulation {
namespace ui {

namespace {

RunConfigSerializer::Snapshot SnapshotFromRun(const simulation::SimulationRun& run) {
    RunConfigSerializer::Snapshot snapshot;
    snapshot.modelType = run.model_type;
    snapshot.runName = run.name;
    snapshot.dataset = run.dataset_measurement;
    snapshot.walkForward = run.walk_forward_config;
    snapshot.hasWalkForward = true;
    if (run.using_feature_schedule && !run.feature_schedule.empty()) {
        snapshot.featureSchedule = run.feature_schedule;
        snapshot.hasFeatureSchedule = true;
    }
    if (run.config) {
        snapshot.features = run.config->feature_columns;
        snapshot.target = run.config->target_column;
        if (run.model_type == "XGBoost") {
            if (auto* xgb = dynamic_cast<XGBoostConfig*>(run.config.get())) {
                snapshot.xgboost = *xgb;
                snapshot.hyperparameterType = "XGBoost";
                snapshot.hasHyperparameters = true;
            }
        }
    }
    return snapshot;
}

} // namespace

// Define plot colors
const ImVec4 SimulationResultsWidget_v2::PLOT_COLORS[] = {
    ImVec4(0.2f, 0.8f, 0.2f, 1.0f),  // Green
    ImVec4(0.8f, 0.2f, 0.2f, 1.0f),  // Red  
    ImVec4(0.2f, 0.2f, 0.8f, 1.0f),  // Blue
    ImVec4(0.8f, 0.8f, 0.2f, 1.0f),  // Yellow
    ImVec4(0.8f, 0.2f, 0.8f, 1.0f),  // Magenta
    ImVec4(0.2f, 0.8f, 0.8f, 1.0f),  // Cyan
};
const int SimulationResultsWidget_v2::NUM_PLOT_COLORS = 6;

SimulationResultsWidget_v2::SimulationResultsWidget_v2()
    : m_currentRunIndex(-1)
    , m_autoScrollTable(true)
    , m_autoFitPlot(true)
    , m_selectedRunTab(0)
    , m_selectedFoldRow(-1)
    , m_showFoldDetails(false) {
}

SimulationResultsWidget_v2::~SimulationResultsWidget_v2() = default;

const SimulationRun* SimulationResultsWidget_v2::GetRunByIndex(int index) const {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    if (index >= 0 && index < (int)m_simulationRuns.size()) {
        return &m_simulationRuns[index];
    }
    return nullptr;
}

int SimulationResultsWidget_v2::ConsumePendingSaveRequest() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    int pending = m_pendingSaveRunIndex;
    m_pendingSaveRunIndex = -1;
    return pending;
}

void SimulationResultsWidget_v2::SetSaveStatus(const std::string& message, bool success) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_saveStatusMessage = message;
    m_saveStatusSuccess = success;
}

void SimulationResultsWidget_v2::Draw() {
    // Get available space
    ImVec2 availableRegion = ImGui::GetContentRegionAvail();
    
    // 1. Always-visible plot at the top (40% of space)
    float plotHeight = availableRegion.y * 0.4f;
    if (ImGui::BeginChild("PlotRegion", ImVec2(0, plotHeight), false)) {
        DrawProfitPlot();
    }
    ImGui::EndChild();
    
    ImGui::Separator();
    
    // 2. Current run status (fixed height)
    float statusHeight = 60.0f;
    if (ImGui::BeginChild("StatusRegion", ImVec2(0, statusHeight), true)) {
        DrawCurrentRunStatus();
    }
    ImGui::EndChild();
    
    ImGui::Separator();
    
    // 3. Run tabs with fold details (remaining space)
    if (ImGui::BeginChild("TableRegion", ImVec2(0, 0), false)) {
        DrawRunTabs();
    }
    ImGui::EndChild();
    
    // Fold details popup is now handled by main window
}

void SimulationResultsWidget_v2::DrawProfitPlot() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_simulationRuns.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No simulation results to display");
        return;
    }
    
    // Trade mode selector
    ImGui::Text("Trade Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Long Only", m_tradeMode == TradeMode::LongOnly)) {
        m_tradeMode = TradeMode::LongOnly;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Short Only", m_tradeMode == TradeMode::ShortOnly)) {
        m_tradeMode = TradeMode::ShortOnly;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Dual (Long+Short)", m_tradeMode == TradeMode::Dual)) {
        m_tradeMode = TradeMode::Dual;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 100);
    ImGui::Checkbox("Auto-fit", &m_autoFitPlot);
    
    if (ImPlot::BeginPlot("Cumulative Profit", ImVec2(-1, -1))) {
        ImPlot::SetupAxis(ImAxis_X1, "Fold Number");
        ImPlot::SetupAxis(ImAxis_Y1, "Running Sum");
        
        // Set axis limits if auto-fit is enabled
        if (m_autoFitPlot) {
            double xMin, xMax, yMin, yMax;
            CalculatePlotLimits(xMin, xMax, yMin, yMax);
            // Only set limits if we have valid data
            if (xMin < xMax && yMin < yMax && xMax != -DBL_MAX) {
                ImPlot::SetupAxisLimits(ImAxis_X1, xMin, xMax, ImGuiCond_Always);
                ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImGuiCond_Always);
            }
        }
        
        // Plot each run based on selected trade mode
        for (size_t i = 0; i < m_simulationRuns.size(); ++i) {
            const auto& run = m_simulationRuns[i];
            if (!run.profitPlotX.empty()) {
                // Select data based on trade mode
                const std::vector<double>* plotData = nullptr;
                switch (m_tradeMode) {
                    case TradeMode::LongOnly:
                        plotData = &run.profitPlotY_long;
                        break;
                    case TradeMode::ShortOnly:
                        plotData = &run.profitPlotY_short;
                        break;
                    case TradeMode::Dual:
                        plotData = &run.profitPlotY_dual;
                        break;
                }
                
                if (plotData && !plotData->empty()) {
                    ImVec4 color = PLOT_COLORS[i % NUM_PLOT_COLORS];
                    ImPlot::SetNextLineStyle(color);
                    ImPlot::PlotLine(run.name.c_str(), 
                        run.profitPlotX.data(), 
                        plotData->data(), 
                        run.profitPlotX.size());
                }
            }
        }
        
        ImPlot::EndPlot();
    }
}

void SimulationResultsWidget_v2::DrawCurrentRunStatus() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_simulationRuns.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No simulation runs");
        return;
    }
    
    // Draw table summary of all runs
    if (ImGui::BeginTable("RunsSummary", 14, 
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit)) {
        
        // Headers
        ImGui::TableSetupColumn("Run", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Folds", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Return", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("PF Long", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("PF Short", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("PF Dual", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Sig Long", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Sig Short", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Sig Total", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Hit% Long", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Hit% Short", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Hit% Total", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Runtime", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        
        // Rows for each run
        for (size_t i = 0; i < m_simulationRuns.size(); ++i) {
            const auto& run = m_simulationRuns[i];
            ImGui::TableNextRow();
            
            // Run name with color indicator
            ImGui::TableNextColumn();
            ImVec4 color = PLOT_COLORS[i % NUM_PLOT_COLORS];
            ImGui::TextColored(color, "%s", run.name.c_str());
            
            // Calculate statistics
            float totalReturn = 0.0f;
            int totalLongSignals = 0, totalShortSignals = 0;
            double totalLongHits = 0.0;
            double totalShortHits = 0.0;
            int totalFolds = run.foldResults.size();
            float totalWins = 0.0f, totalLosses = 0.0f;
            float totalShortWins = 0.0f, totalShortLosses = 0.0f;
            
            if (totalFolds > 0) {
                for (const auto& fold : run.foldResults) {
                    totalReturn += fold.signal_sum + fold.short_signal_sum;
                    totalLongSignals += fold.n_signals;
                    totalShortSignals += fold.n_short_signals;
                    totalWins += fold.sum_wins;
                    totalLosses += fold.sum_losses;
                    totalShortWins += fold.sum_short_wins;
                    totalShortLosses += fold.sum_short_losses;
                    if (fold.n_signals > 0) {
                        totalLongHits += static_cast<double>(fold.hit_rate) * static_cast<double>(fold.n_signals);
                    }
                    if (fold.n_short_signals > 0) {
                        totalShortHits += static_cast<double>(fold.short_hit_rate) * static_cast<double>(fold.n_short_signals);
                    }
                }
            }
            
            int totalSignals = totalLongSignals + totalShortSignals;
            const float avgLongHitRate = (totalLongSignals > 0)
                ? static_cast<float>(totalLongHits / static_cast<double>(totalLongSignals))
                : 0.0f;
            const float avgShortHitRate = (totalShortSignals > 0)
                ? static_cast<float>(totalShortHits / static_cast<double>(totalShortSignals))
                : 0.0f;
            const float avgTotalHitRate = (totalSignals > 0)
                ? static_cast<float>((totalLongHits + totalShortHits) / static_cast<double>(totalSignals))
                : 0.0f;
            
            float pfLong = (totalLosses > 0) ? (totalWins / totalLosses) :
                           ((totalWins > 0) ? 999.0f : 0.0f);
            float pfShort = (totalShortLosses > 0) ? (totalShortWins / totalShortLosses) :
                            ((totalShortWins > 0) ? 999.0f : 0.0f);
            float pfDual = ((totalLosses + totalShortLosses) > 0) ? 
                           ((totalWins + totalShortWins) / (totalLosses + totalShortLosses)) : 
                           (totalWins > 0 ? 999.0f : 0.0f);
            
            // Folds
            ImGui::TableNextColumn();
            ImGui::Text("%d", totalFolds);
            
            // Total Return
            ImGui::TableNextColumn();
            ImVec4 returnColor = totalReturn > 0 ? 
                ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : 
                ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
            ImGui::TextColored(returnColor, "%.6f", totalReturn);
            
            // PF Long
            ImGui::TableNextColumn();
            ImVec4 pfLongColor = pfLong > 1.0f ?
                ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
            if (pfLong >= 999.0f) {
                ImGui::TextColored(pfLongColor, "Inf");
            } else if (pfLong > 0) {
                ImGui::TextColored(pfLongColor, "%.2f", pfLong);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // PF Short
            ImGui::TableNextColumn();
            ImVec4 pfShortColor = pfShort > 1.0f ?
                ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
            if (pfShort >= 999.0f) {
                ImGui::TextColored(pfShortColor, "Inf");
            } else if (pfShort > 0) {
                ImGui::TextColored(pfShortColor, "%.2f", pfShort);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // PF Dual
            ImGui::TableNextColumn();
            ImVec4 pfDualColor = pfDual > 1.0f ?
                ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
            if (pfDual >= 999.0f) {
                ImGui::TextColored(pfDualColor, "Inf");
            } else if (pfDual > 0) {
                ImGui::TextColored(pfDualColor, "%.2f", pfDual);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Signals Long
            ImGui::TableNextColumn();
            if (totalLongSignals > 0) {
                ImGui::Text("%d", totalLongSignals);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Signals Short
            ImGui::TableNextColumn();
            if (totalShortSignals > 0) {
                ImGui::Text("%d", totalShortSignals);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Signals Total
            ImGui::TableNextColumn();
            if (totalSignals > 0) {
                ImGui::Text("%d", totalSignals);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Hit Rate Long
            ImGui::TableNextColumn();
            if (totalLongSignals > 0) {
                ImGui::Text("%.1f%%", avgLongHitRate * 100.0f);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Hit Rate Short
            ImGui::TableNextColumn();
            if (totalShortSignals > 0) {
                ImGui::Text("%.1f%%", avgShortHitRate * 100.0f);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Hit Rate Total
            ImGui::TableNextColumn();
            if (totalSignals > 0) {
                ImGui::Text("%.1f%%", avgTotalHitRate * 100.0f);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Runtime
            ImGui::TableNextColumn();
            std::chrono::seconds duration;
            
            // Always use endTime if it's valid and after startTime
            if (run.endTime > run.startTime) {
                duration = std::chrono::duration_cast<std::chrono::seconds>(
                    run.endTime - run.startTime);
            } else {
                // For running simulations, calculate elapsed time
                duration = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now() - run.startTime);
            }
            
            // Show running indicator for incomplete runs
            if (!run.completed && i == (size_t)m_currentRunIndex) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "%llds...", duration.count());
            } else {
                ImGui::Text("%llds", duration.count());
            }
        }
        
        ImGui::EndTable();
    }
}

void SimulationResultsWidget_v2::DrawRunPerformanceSummary(const SimulationRun& run, size_t runIndex, float totalPF) {
    // Display configuration in a collapsible header
    if (ImGui::CollapsingHeader("Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Model type and basic info
        ImGui::Text("Model: %s", run.model_type.c_str());
        
        if (run.config) {
            ImGui::Separator();
            
            // Features section - display schedule if using it, otherwise feature list
            if (run.using_feature_schedule && !run.feature_schedule.empty()) {
                ImGui::Text("Feature Schedule:");
                ImGui::SameLine();
                ImGui::TextDisabled("[Dynamic feature selection per fold]");
                
                // Display the schedule in a scrollable text area
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));  // Orange for schedule
                
                // Split schedule by lines and display
                std::istringstream schedStream(run.feature_schedule);
                std::string schedLine;
                while (std::getline(schedStream, schedLine)) {
                    ImGui::Text("%s", schedLine.c_str());
                }
                
                ImGui::PopStyleColor();
                ImGui::Unindent();
            } else {
                // Regular feature list display
                ImGui::Text("Features (%zu):", run.config->feature_columns.size());
                ImGui::SameLine();
                ImGui::TextDisabled("[Copy Features button will copy these]");
                
                // Display features in a compact wrapped format
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));  // Light blue for features
                for (size_t i = 0; i < run.config->feature_columns.size(); ++i) {
                    ImGui::Text("%s", run.config->feature_columns[i].c_str());
                    if (i < run.config->feature_columns.size() - 1) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|");
                        ImGui::SameLine();
                    }
                }
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
            
            // Target
            ImGui::Text("Target:");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));  // Yellow for target
            ImGui::Text("%s", run.config->target_column.c_str());
            ImGui::PopStyleColor();
            
            ImGui::Separator();
            
            // Hyperparameters (compact display)
            ImGui::Text("Hyperparameters:");
            ImGui::SameLine();
            ImGui::TextDisabled("[Copy Hyperparameters button will copy these]");

            // Try to cast to model-specific configs so we display the exact values used
            if (run.model_type == "XGBoost") {
                if (auto* xgb_config = dynamic_cast<const XGBoostConfig*>(run.config.get())) {
                    auto threshold_to_string = [](ThresholdMethod method) {
                        switch (method) {
                            case ThresholdMethod::Percentile95: return "95th Percentile";
                            case ThresholdMethod::OptimalROC:   return "Optimal ROC";
                            default:                             return "Custom";
                        }
                    };

                    auto objective_to_string = [](const std::string& objective, float alpha) {
                        if (objective == "reg:squarederror") {
                            return std::string("Squared Error (MSE)");
                        }
                        if (objective == "reg:quantileerror") {
                            char buffer[48];
                            std::snprintf(buffer, sizeof(buffer), "Quantile %.0f%%", alpha * 100.0f);
                            return std::string(buffer);
                        }
                        return objective;
                    };

                    ImGui::Indent();
                    ImGui::Text("Learning Rate: %.4f | Max Depth: %d | Boost Rounds: %d",
                                xgb_config->learning_rate,
                                xgb_config->max_depth,
                                xgb_config->num_boost_round);
                    ImGui::Text("Min Child Weight: %.1f | Subsample: %.2f | ColSample: %.2f",
                                xgb_config->min_child_weight,
                                xgb_config->subsample,
                                xgb_config->colsample_bytree);
                    ImGui::Text("Lambda (L2): %.2f | Early Stop: %d | Min Rounds: %d",
                                xgb_config->lambda,
                                xgb_config->early_stopping_rounds,
                                xgb_config->min_boost_rounds);
                    ImGui::Text("Force Minimum Training: %s | Random Seed: %d",
                                xgb_config->force_minimum_training ? "Yes" : "No",
                                xgb_config->random_seed);
                    ImGui::Text("Objective: %s | Threshold: %s",
                                objective_to_string(xgb_config->objective, xgb_config->quantile_alpha).c_str(),
                                threshold_to_string(xgb_config->threshold_method));
                    ImGui::Text("Tree Method: %s | Device: %s",
                                xgb_config->tree_method.c_str(),
                                xgb_config->device.c_str());
                    ImGui::Text("Validation Split: %.2f", xgb_config->val_split_ratio);
                    ImGui::Unindent();
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                       "Unable to read XGBoost configuration for this run.");
                }
            }
            
            // Data transformation settings
            ImGui::Text("Transform: %s%s",
                       run.config->use_standardization ? "Standardize " : "",
                       run.config->use_tanh_transform ? "Tanh" : "");
    }
    
    if (ImGui::Button("Save Run to Stage1")) {
        m_pendingSaveRunIndex = static_cast<int>(runIndex);
        m_saveStatusMessage.clear();
        m_saveStatusSuccess = true;
    }
    if (!m_saveStatusMessage.empty()) {
        ImGui::SameLine();
            const ImVec4 color = m_saveStatusSuccess
                ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                : ImVec4(0.9f, 0.4f, 0.2f, 1.0f);
            ImGui::TextColored(color, "%s", m_saveStatusMessage.c_str());
        }
    }
}

void SimulationResultsWidget_v2::DrawRunTabs() {
    if (ImGui::BeginTabBar("RunTabs")) {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        
        for (size_t i = 0; i < m_simulationRuns.size(); ++i) {
            const auto& run = m_simulationRuns[i];
            bool open = true;
            
            // Use the same color as the profit plot line
            ImVec4 lineColor = PLOT_COLORS[i % NUM_PLOT_COLORS];
            
            // Create tab colors based on the line color
            ImVec4 tabColor = ImVec4(lineColor.x, lineColor.y, lineColor.z, 0.5f);
            ImVec4 tabHovered = ImVec4(lineColor.x, lineColor.y, lineColor.z, 0.7f);
            ImVec4 tabActive = ImVec4(lineColor.x, lineColor.y, lineColor.z, 0.9f);
            
            ImGui::PushStyleColor(ImGuiCol_Tab, tabColor);
            ImGui::PushStyleColor(ImGuiCol_TabHovered, tabHovered);
            ImGui::PushStyleColor(ImGuiCol_TabActive, tabActive);
            
            if (ImGui::BeginTabItem(run.name.c_str(), &open)) {
                m_selectedRunTab = i;
                DrawFoldTable(run, i);
                ImGui::EndTabItem();
            }
            
            // Always pop the 3 style colors we pushed
            ImGui::PopStyleColor(3);
            
            // Handle tab close
            if (!open && i != m_currentRunIndex) {
                // Would remove the run, but for now just ignore
            }
        }
        
        ImGui::EndTabBar();
    }
}

void SimulationResultsWidget_v2::DrawFoldTable(const SimulationRun& run, size_t runIndex) {
    // Calculate total profit factor across all folds
    float totalWins = 0.0f, totalLosses = 0.0f;
    for (const auto& fold : run.foldResults) {
        totalWins += fold.sum_wins;
        totalLosses += fold.sum_losses;
    }
    float totalPF = (totalLosses > 0) ? (totalWins / totalLosses) : 
                    (totalWins > 0 ? 999.0f : 0.0f);
    
    // Display run performance summary
    DrawRunPerformanceSummary(run, runIndex, totalPF);
    
    // Copy configuration buttons
    DrawCopyButtons(run);
    
    ImGui::Separator();
    
    // Table with fold results
    if (ImGui::BeginTable("FoldResults", 17, 
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit)) {
        
        // Headers
        ImGui::TableSetupColumn("Fold", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Iter", ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("S.Long", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("S.Short", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("S.Total", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("H%Long", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("H%Short", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("H%Total", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Sum", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Running", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("PF Train", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("PF Long", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("PF Short", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("PF Dual", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Train", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        
        // Rows
        for (size_t i = 0; i < run.foldResults.size(); ++i) {
            const auto& fold = run.foldResults[i]; 
            ImGui::TableNextRow();
            
            // Highlight row on hover
            bool isRowHovered = false;
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            rowMin.x = ImGui::GetWindowPos().x;
            float rowHeight = ImGui::GetTextLineHeightWithSpacing();
            ImVec2 rowMax = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), 
                                   rowMin.y + rowHeight);
            if (ImGui::IsMouseHoveringRect(rowMin, rowMax)) {
                isRowHovered = true;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(rowMin, rowMax, 
                    ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 0.3f)));
            }
            
            // Fold number
            ImGui::TableNextColumn();
            ImGui::Text("%d", fold.fold_number);
            
            // Iterations
            ImGui::TableNextColumn();
            ImGui::Text("%d", fold.best_iteration);
            
            // Signals Long
            ImGui::TableNextColumn();
            if (fold.n_signals > 0) {
                ImGui::Text("%d", fold.n_signals);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Signals Short
            ImGui::TableNextColumn();
            if (fold.n_short_signals > 0) {
                ImGui::Text("%d", fold.n_short_signals);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Signals Total
            ImGui::TableNextColumn();
            int totalSig = fold.n_signals + fold.n_short_signals;
            if (totalSig > 0) {
                ImGui::Text("%d", totalSig);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Hit rate Long
            ImGui::TableNextColumn();
            if (fold.n_signals > 0) {
                ImGui::Text("%.1f%%", fold.hit_rate * 100.0f);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Hit rate Short
            ImGui::TableNextColumn();
            if (fold.n_short_signals > 0) {
                ImGui::Text("%.1f%%", fold.short_hit_rate * 100.0f);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Hit rate Total
            ImGui::TableNextColumn();
            if (totalSig > 0) {
                float totalHitRate = ((fold.hit_rate * fold.n_signals + fold.short_hit_rate * fold.n_short_signals) / totalSig) * 100.0f;
                ImGui::Text("%.1f%%", totalHitRate);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Sum (combined long + short)
            ImGui::TableNextColumn();
            float totalSum = fold.signal_sum + fold.short_signal_sum;
            if (totalSum != 0.0f) {
                ImVec4 color = totalSum > 0 ? 
                    ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : 
                    ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
                ImGui::TextColored(color, "%.6f", totalSum);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Running sum
            ImGui::TableNextColumn();
            ImGui::Text("%.6f", fold.running_sum);
            
            // PF Train
            ImGui::TableNextColumn();
            if (fold.profit_factor_train > 0) {
                ImVec4 color = fold.profit_factor_train > 1.0f ?
                    ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                    ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
                ImGui::TextColored(color, "%.2f", fold.profit_factor_train);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // PF Long
            ImGui::TableNextColumn();
            if (fold.profit_factor_test >= 999.0f) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Inf");
            } else if (fold.profit_factor_test > 0) {
                ImVec4 color = fold.profit_factor_test > 1.0f ?
                    ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                    ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
                ImGui::TextColored(color, "%.2f", fold.profit_factor_test);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // PF Short
            ImGui::TableNextColumn();
            if (fold.profit_factor_short_test >= 999.0f) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Inf");
            } else if (fold.profit_factor_short_test > 0) {
                ImVec4 color = fold.profit_factor_short_test > 1.0f ?
                    ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                    ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
                ImGui::TextColored(color, "%.2f", fold.profit_factor_short_test);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // PF Dual (Long + Short combined)
            ImGui::TableNextColumn();
            float pfDual = ((fold.sum_losses + fold.sum_short_losses) > 0) ?
                          ((fold.sum_wins + fold.sum_short_wins) / (fold.sum_losses + fold.sum_short_losses)) :
                          ((fold.sum_wins + fold.sum_short_wins) > 0 ? 999.0f : 0.0f);
            if (pfDual >= 999.0f) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Inf");
            } else if (pfDual > 0) {
                ImVec4 color = pfDual > 1.0f ?
                    ImVec4(0.2f, 0.8f, 0.2f, 1.0f) :
                    ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
                ImGui::TextColored(color, "%.2f", pfDual);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }
            
            // Train range - color coded based on model status
            ImGui::TableNextColumn();
            ImVec4 rangeColor;
            if (fold.model_learned_nothing && !fold.used_cached_model) {
                // Red: Model failed and no cached model used (fold skipped)
                rangeColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
            } else if (fold.used_cached_model) {
                // Orange: Model failed but cached model was used
                rangeColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
            } else {
                // White: Model trained OK
                rangeColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            ImGui::TextColored(rangeColor, "[%d, %d]", fold.train_start, fold.train_end - 1);
            
            // Test range - same color coding as train
            ImGui::TableNextColumn();
            ImGui::TextColored(rangeColor, "[%d, %d]", fold.test_start, fold.test_end - 1);
            
            // Action button
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("Examine")) {
                m_selectedFoldInfo.valid = true;
                m_selectedFoldInfo.fold = fold;
                // Don't copy the whole run, just store essential info
                m_selectedFoldInfo.runName = run.name;
                m_selectedFoldInfo.modelType = run.model_type;
                m_selectedFoldInfo.runIndex = static_cast<int>(runIndex);
                // Don't show popup here - let main window handle it
            }
            ImGui::PopID();
        }
        
        // Auto-scroll to bottom if enabled
        if (m_autoScrollTable && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        
        ImGui::EndTable();
    }
}

void SimulationResultsWidget_v2::DrawFoldDetailsPopup() {
    // This is now deprecated - fold examination is handled through TestModelWindow
    // Function kept for compatibility but does nothing
}

void SimulationResultsWidget_v2::CalculatePlotLimits(
    double& xMin, double& xMax, double& yMin, double& yMax) {
    
    xMin = DBL_MAX;
    xMax = -DBL_MAX;
    yMin = DBL_MAX;
    yMax = -DBL_MAX;
    
    for (const auto& run : m_simulationRuns) {
        if (run.profitPlotX.empty()) continue;
        
        // Select data based on trade mode
        const std::vector<double>* plotData = nullptr;
        switch (m_tradeMode) {
            case TradeMode::LongOnly:
                plotData = &run.profitPlotY_long;
                break;
            case TradeMode::ShortOnly:
                plotData = &run.profitPlotY_short;
                break;
            case TradeMode::Dual:
                plotData = &run.profitPlotY_dual;
                break;
        }
        
        if (plotData && !plotData->empty()) {
            for (size_t i = 0; i < run.profitPlotX.size(); ++i) {
                xMin = std::min(xMin, run.profitPlotX[i]);
                xMax = std::max(xMax, run.profitPlotX[i]);
                yMin = std::min(yMin, (*plotData)[i]);
                yMax = std::max(yMax, (*plotData)[i]);
            }
        }
    }
    
    // Check if we found valid data
    if (xMin == DBL_MAX || xMax == -DBL_MAX) {
        xMin = 0.0;
        xMax = 10.0;
        yMin = -0.1;
        yMax = 0.1;
        return;
    }
    
    // Add padding
    double xPadding = std::max(1.0, (xMax - xMin) * 0.05);
    double yPadding = std::max(0.01, (yMax - yMin) * 0.1);
    
    xMin -= xPadding;
    xMax += xPadding;
    yMin -= yPadding;
    yMax += yPadding;
    
    // Handle edge cases
    if (std::abs(xMax - xMin) < 0.001) {
        xMin -= 1.0;
        xMax += 1.0;
    }
    if (std::abs(yMax - yMin) < 0.0001) {
        yMin -= 0.1;
        yMax += 0.1;
    }
}

void SimulationResultsWidget_v2::AddRun(SimulationRun&& run) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_simulationRuns.push_back(std::move(run));
    m_currentRunIndex = m_simulationRuns.size() - 1;
}

void SimulationResultsWidget_v2::AddFoldResult(const FoldResult& result) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_currentRunIndex >= 0 && m_currentRunIndex < (int)m_simulationRuns.size()) {
        auto& run = m_simulationRuns[m_currentRunIndex];
        run.foldResults.push_back(result);
        run.profitPlotX.push_back((double)result.fold_number);
        run.profitPlotY_long.push_back((double)result.running_sum);
        run.profitPlotY_short.push_back((double)result.running_sum_short);
        run.profitPlotY_dual.push_back((double)result.running_sum_dual);
    }
}

void SimulationResultsWidget_v2::UpdateCurrentRun(const SimulationRun& run) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_currentRunIndex >= 0 && m_currentRunIndex < (int)m_simulationRuns.size()) {
        // Can't copy SimulationRun due to unique_ptr member
        // Update the completed flag, endTime, and profit plots
        m_simulationRuns[m_currentRunIndex].completed = run.completed;
        m_simulationRuns[m_currentRunIndex].endTime = run.endTime;
        
        // Update all profit plot arrays - ensure they're all consistent
        if (run.profitPlotX.size() == run.profitPlotY_long.size() &&
            run.profitPlotX.size() == run.profitPlotY_short.size() &&
            run.profitPlotX.size() == run.profitPlotY_dual.size()) {
            m_simulationRuns[m_currentRunIndex].profitPlotX = run.profitPlotX;
            m_simulationRuns[m_currentRunIndex].profitPlotY_long = run.profitPlotY_long;
            m_simulationRuns[m_currentRunIndex].profitPlotY_short = run.profitPlotY_short;
            m_simulationRuns[m_currentRunIndex].profitPlotY_dual = run.profitPlotY_dual;
        } else {
            std::cerr << "Warning: Profit plot vectors have inconsistent sizes in UpdateCurrentRun" << std::endl;
        }
        
        // Also update fold results if they're different
        m_simulationRuns[m_currentRunIndex].foldResults = run.foldResults;
        
        // CRITICAL: Update predictions and actuals for trade simulation
        m_simulationRuns[m_currentRunIndex].all_test_predictions = run.all_test_predictions;
        m_simulationRuns[m_currentRunIndex].all_test_actuals = run.all_test_actuals;
        m_simulationRuns[m_currentRunIndex].all_test_timestamps = run.all_test_timestamps;
        m_simulationRuns[m_currentRunIndex].fold_prediction_offsets = run.fold_prediction_offsets;
        m_simulationRuns[m_currentRunIndex].walk_forward_config = run.walk_forward_config;
    }
}

void SimulationResultsWidget_v2::ClearRuns() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_simulationRuns.clear();
    m_currentRunIndex = -1;
    m_selectedFoldInfo.valid = false;
}

SimulationResultsWidget_v2::SelectedFoldInfo SimulationResultsWidget_v2::GetSelectedFold() const {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_selectedFoldInfo;
}

void SimulationResultsWidget_v2::ClearSelectedFold() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_selectedFoldInfo.valid = false;
}

void SimulationResultsWidget_v2::DrawCopyButtons(const SimulationRun& run) {
    ImGui::Text("Copy Configuration:");
    ImGui::SameLine();
    
    bool features_copied = false;
    
    if (ImGui::Button("Copy Features")) {
        if (run.config) {
            auto snapshot = SnapshotFromRun(run);
            std::string clipboardText = RunConfigSerializer::Serialize(
                snapshot,
                RunConfigSerializer::SectionMetadata |
                RunConfigSerializer::SectionFeatures |
                RunConfigSerializer::SectionFeatureSchedule |
                RunConfigSerializer::SectionWalkForward);
            ImGui::SetClipboardText(clipboardText.c_str());
            features_copied = true;

            if (m_configWidget) {
                UniversalConfigWidget::CopiedConfiguration copied;
                copied.features = snapshot.features;
                copied.target = snapshot.target;
                copied.walk_forward = snapshot.walkForward;
                copied.has_features = !snapshot.features.empty();
                copied.model_type = snapshot.modelType;
                m_configWidget->SetCopiedConfig(copied);
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Copy from this run:");
        if (run.config) {
            ImGui::BulletText("%zu features", run.config->feature_columns.size());
            if (!run.config->feature_columns.empty()) {
                ImGui::BulletText("First feature: %s%s", 
                    run.config->feature_columns[0].c_str(), 
                    run.config->feature_columns.size() > 1 ? ", ..." : "");
            }
            ImGui::BulletText("Target: %s", 
                run.config->target_column.empty() ? "(empty)" : run.config->target_column.c_str());
            ImGui::BulletText("Walk-forward settings");
        } else {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Configuration not stored");
        }
        ImGui::EndTooltip();
    }
    
    if (features_copied) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), 
                          "Copied %zu features!", run.config->feature_columns.size());
    }
    
    ImGui::SameLine();
    
    bool params_copied = false;
    
    if (ImGui::Button("Copy Hyperparameters")) {
        if (run.config && run.model_type == "XGBoost") {
            auto snapshot = SnapshotFromRun(run);
            if (snapshot.hasHyperparameters && snapshot.xgboost.has_value()) {
                std::string clipboardText = RunConfigSerializer::Serialize(
                    snapshot,
                    RunConfigSerializer::SectionMetadata | RunConfigSerializer::SectionHyperparameters);
                ImGui::SetClipboardText(clipboardText.c_str());
                params_copied = true;

                if (m_configWidget) {
                    auto copied = m_configWidget->GetCopiedConfig();
                    copied.hyperparameters = snapshot.xgboost.value();
                    copied.has_hyperparameters = true;
                    copied.model_type = "XGBoost";
                    m_configWidget->SetCopiedConfig(copied);
                }
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Copy %s hyperparameters:", run.model_type.c_str());
        if (run.config && run.model_type == "XGBoost") {
            auto* xgb_config = dynamic_cast<XGBoostConfig*>(run.config.get());
            if (xgb_config) {
                ImGui::BulletText("Max depth: %d", xgb_config->max_depth);
                ImGui::BulletText("Learning rate: %.3f", xgb_config->learning_rate);
                ImGui::BulletText("Boost rounds: %d", xgb_config->num_boost_round);
                ImGui::BulletText("Min child weight: %.1f", xgb_config->min_child_weight);
            }
        } else if (!run.config) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Configuration not stored");
        }
        ImGui::EndTooltip();
    }
    
    if (params_copied) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Copied %s params!", run.model_type.c_str());
    }

    ImGui::SameLine();

    bool all_copied = false;

    if (ImGui::Button("Copy All")) {
        if (run.config) {
            auto snapshot = SnapshotFromRun(run);
            std::string clipboardText = RunConfigSerializer::Serialize(
                snapshot,
                RunConfigSerializer::SectionMetadata |
                RunConfigSerializer::SectionFeatures |
                RunConfigSerializer::SectionFeatureSchedule |
                RunConfigSerializer::SectionWalkForward |
                RunConfigSerializer::SectionHyperparameters);

            ImGui::SetClipboardText(clipboardText.c_str());
            all_copied = true;

            if (m_configWidget) {
                UniversalConfigWidget::CopiedConfiguration copied;
                copied.features = snapshot.features;
                copied.target = snapshot.target;
                copied.walk_forward = snapshot.walkForward;
                copied.has_features = !snapshot.features.empty();
                copied.model_type = snapshot.modelType;
                if (snapshot.hasHyperparameters && snapshot.xgboost.has_value()) {
                    copied.hyperparameters = snapshot.xgboost.value();
                    copied.has_hyperparameters = true;
                }
                m_configWidget->SetCopiedConfig(copied);
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Copy complete configuration:");
        if (run.config) {
            ImGui::BulletText("%zu features + target", run.config->feature_columns.size());
            ImGui::BulletText("%s hyperparameters", run.model_type.c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Configuration not stored");
        }
        ImGui::EndTooltip();
    }

    if (all_copied) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Copied all config!");
    }
}

} // namespace ui
} // namespace simulation

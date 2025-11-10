#include "SimulationResultsWidget.h"
#include "imgui.h"
#include "../../implot.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <float.h>
#include <iostream>

namespace simulation {
namespace ui {

SimulationResultsWidget::SimulationResultsWidget()
    : m_currentRunIndex(-1)
    , m_selectedRunIndex(-1)
    , m_resultsUpdated(false)
    , m_autoScrollTable(true)
    , m_autoFitPlot(true)
    , m_resultsPanelHeight(400.0f)
    , m_selectedFoldIndex(-1) {
}

void SimulationResultsWidget::AddRun(const SimulationRun& run) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    // Can't copy SimulationRun due to unique_ptr, need to create new one
    SimulationRun newRun;
    newRun.name = run.name;
    newRun.config_description = run.config_description;
    newRun.model_type = run.model_type;
    newRun.walk_forward_config = run.walk_forward_config;
    newRun.using_feature_schedule = run.using_feature_schedule;
    newRun.feature_schedule = run.feature_schedule;
    newRun.foldResults = run.foldResults;
    newRun.profitPlotX = run.profitPlotX;
    newRun.profitPlotY_long = run.profitPlotY_long;
    newRun.profitPlotY_short = run.profitPlotY_short;
    newRun.profitPlotY_dual = run.profitPlotY_dual;
    newRun.completed = run.completed;
    newRun.startTime = run.startTime;
    m_simulationRuns.push_back(std::move(newRun));
    m_currentRunIndex = m_simulationRuns.size() - 1;
    m_selectedRunIndex = m_currentRunIndex;
    m_resultsUpdated = true;
}

void SimulationResultsWidget::UpdateCurrentRun(const SimulationRun& run) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    if (m_currentRunIndex >= 0 && m_currentRunIndex < (int)m_simulationRuns.size()) {
        // Can't assign directly due to unique_ptr member, need to copy manually
        m_simulationRuns[m_currentRunIndex].name = run.name;
        m_simulationRuns[m_currentRunIndex].config_description = run.config_description;
        m_simulationRuns[m_currentRunIndex].model_type = run.model_type;
        m_simulationRuns[m_currentRunIndex].walk_forward_config = run.walk_forward_config;
        m_simulationRuns[m_currentRunIndex].foldResults = run.foldResults;
        m_simulationRuns[m_currentRunIndex].all_test_predictions = run.all_test_predictions;
        m_simulationRuns[m_currentRunIndex].all_test_actuals = run.all_test_actuals;
        m_simulationRuns[m_currentRunIndex].all_test_timestamps = run.all_test_timestamps;
        m_simulationRuns[m_currentRunIndex].fold_prediction_offsets = run.fold_prediction_offsets;
        
        // Copy all profit plot vectors together - they should all have the same size
        // Only copy if they're all properly initialized
        if (run.profitPlotX.size() == run.profitPlotY_long.size() &&
            run.profitPlotX.size() == run.profitPlotY_short.size() &&
            run.profitPlotX.size() == run.profitPlotY_dual.size()) {
            m_simulationRuns[m_currentRunIndex].profitPlotX = run.profitPlotX;
            m_simulationRuns[m_currentRunIndex].profitPlotY_long = run.profitPlotY_long;
            m_simulationRuns[m_currentRunIndex].profitPlotY_short = run.profitPlotY_short;
            m_simulationRuns[m_currentRunIndex].profitPlotY_dual = run.profitPlotY_dual;
        } else {
            // Vectors have inconsistent sizes - likely a bug or early termination
            std::cerr << "Warning: Profit plot vectors have inconsistent sizes: X=" 
                      << run.profitPlotX.size() << ", Y_long=" << run.profitPlotY_long.size()
                      << ", Y_short=" << run.profitPlotY_short.size() 
                      << ", Y_dual=" << run.profitPlotY_dual.size() << std::endl;
        }
        
        m_simulationRuns[m_currentRunIndex].completed = run.completed;
        m_simulationRuns[m_currentRunIndex].startTime = run.startTime;
        m_resultsUpdated = true;
    }
}

void SimulationResultsWidget::AddFoldResult(const FoldResult& result) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    if (m_currentRunIndex >= 0 && m_currentRunIndex < (int)m_simulationRuns.size()) {
        m_simulationRuns[m_currentRunIndex].foldResults.push_back(result);
        m_simulationRuns[m_currentRunIndex].profitPlotX.push_back(result.fold_number);
        m_simulationRuns[m_currentRunIndex].profitPlotY_long.push_back(result.running_sum);
        m_simulationRuns[m_currentRunIndex].profitPlotY_short.push_back(result.running_sum_short);
        m_simulationRuns[m_currentRunIndex].profitPlotY_dual.push_back(result.running_sum_dual);
        m_resultsUpdated = true;
    }
}

void SimulationResultsWidget::ClearRuns() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_simulationRuns.clear();
    m_currentRunIndex = -1;
    m_selectedRunIndex = -1;
    m_resultsUpdated = false;
}

void SimulationResultsWidget::Draw() {
    if (ImGui::BeginTabBar("ResultsTabs")) {
        // Results Table Tab
        if (ImGui::BeginTabItem("Results Table")) {
            DrawResultsTable(m_selectedRunIndex);
            ImGui::EndTabItem();
        }
        
        // Profit Plot Tab
        if (ImGui::BeginTabItem("Profit Plot")) {
            DrawProfitPlot();
            ImGui::EndTabItem();
        }
        
        // Run Comparison Tab
        if (ImGui::BeginTabItem("Runs")) {
            DrawRunTabs();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
}

void SimulationResultsWidget::DrawRunTabs() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_simulationRuns.empty()) {
        ImGui::Text("No simulation runs available");
        return;
    }
    
    // Run selector
    if (ImGui::BeginCombo("Select Run", 
                          m_selectedRunIndex >= 0 ? 
                          m_simulationRuns[m_selectedRunIndex].name.c_str() : 
                          "Select...")) {
        for (size_t i = 0; i < m_simulationRuns.size(); ++i) {
            const auto& run = m_simulationRuns[i];
            bool is_selected = (m_selectedRunIndex == (int)i);
            
            std::string label = run.name + " (" + run.model_type + ")";
            if (run.completed) {
                label += " ✓";
            } else {
                label += " ⟳";
            }
            
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                m_selectedRunIndex = i;
            }
            
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    // Display selected run summary
    if (m_selectedRunIndex >= 0 && m_selectedRunIndex < (int)m_simulationRuns.size()) {
        DrawRunSummary(m_simulationRuns[m_selectedRunIndex]);
    }
}

void SimulationResultsWidget::DrawResultsTable(int runIndex) {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (runIndex < 0 || runIndex >= (int)m_simulationRuns.size()) {
        ImGui::Text("No run selected");
        return;
    }
    
    const auto& run = m_simulationRuns[runIndex];
    
    if (run.foldResults.empty()) {
        ImGui::Text("No results yet...");
        return;
    }
    
    // Summary statistics
    float total_signals = 0;
    float total_hits = 0;
    float final_sum = run.foldResults.back().running_sum;
    
    for (const auto& fold : run.foldResults) {
        total_signals += fold.n_signals;
        total_hits += fold.n_signals * fold.hit_rate;
    }
    
    float overall_hit_rate = total_signals > 0 ? total_hits / total_signals : 0;
    
    ImGui::Text("Model: %s | Folds: %d | Signals: %.0f | Hit Rate: %.1f%% | Final Sum: %.6f",
                run.model_type.c_str(),
                (int)run.foldResults.size(),
                total_signals,
                overall_hit_rate * 100,
                final_sum);
    
    ImGui::Separator();
    
    // Results table
    if (ImGui::BeginTable("FoldResults", 9, 
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | 
                          ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
                          ImGuiTableFlags_Resizable)) {
        
        ImGui::TableSetupColumn("Fold", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Iter", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Signals", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Hit%", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Avg Return", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Sum", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Running", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        
        // Determine visible range
        int startIdx = std::max(0, (int)run.foldResults.size() - MAX_VISIBLE_RESULTS);
        
        for (int i = startIdx; i < (int)run.foldResults.size(); ++i) {
            const auto& result = run.foldResults[i];
            result.UpdateCache();
            
            ImGui::TableNextRow();
            
            // Determine row color
            ImVec4 textColor = GetFoldColor(result);
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            
            // Fold number
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(result.fold_str.c_str(), 
                                 m_selectedFoldIndex == i,
                                 ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedFoldIndex = i;
            }
            
            // Status
            ImGui::TableSetColumnIndex(1);
            if (result.model_learned_nothing && !result.used_cached_model) {
                ImGui::Text("Failed");
            } else if (result.used_cached_model) {
                ImGui::Text("Cached");
            } else {
                ImGui::Text("OK");
            }
            
            // Iterations
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", result.best_iteration);
            
            // Signals
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", result.signals_str.c_str());
            
            // Signal rate
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s", result.rate_str.c_str());
            
            // Hit rate
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%s", result.hit_str.c_str());
            
            // Average return
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%s", result.return_str.c_str());
            
            // Signal sum
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.6f", result.signal_sum);
            
            // Running sum
            ImGui::TableSetColumnIndex(8);
            ImGui::Text("%s", result.sum_str.c_str());
            
            ImGui::PopStyleColor();
        }
        
        // Auto-scroll to bottom
        if (m_autoScrollTable && m_resultsUpdated) {
            ImGui::SetScrollHereY(1.0f);
            m_resultsUpdated = false;
        }
        
        ImGui::EndTable();
    }
    
    // Selected fold details
    if (m_selectedFoldIndex >= 0 && m_selectedFoldIndex < (int)run.foldResults.size()) {
        ImGui::Separator();
        DrawFoldDetails(run.foldResults[m_selectedFoldIndex]);
    }
}

void SimulationResultsWidget::DrawProfitPlot() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_simulationRuns.empty()) {
        ImGui::Text("No data to plot");
        return;
    }
    
    // Add trade mode selector
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
    
    if (ImPlot::BeginPlot("Cumulative Profit", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Fold", "Cumulative Return");
        
        if (m_autoFitPlot) {
            // Calculate actual data ranges
            double minX = DBL_MAX, maxX = -DBL_MAX;
            double minY = DBL_MAX, maxY = -DBL_MAX;
            bool hasData = false;
            
            for (const auto& run : m_simulationRuns) {
                if (!run.profitPlotX.empty()) {
                    hasData = true;
                    for (double x : run.profitPlotX) {
                        minX = std::min(minX, x);
                        maxX = std::max(maxX, x);
                    }
                    // Check bounds based on current trade mode
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
                    
                    if (plotData) {
                        for (double y : *plotData) {
                            minY = std::min(minY, y);
                            maxY = std::max(maxY, y);
                        }
                    }
                }
            }
            
            if (hasData) {
                // Add some padding
                double xRange = maxX - minX;
                double yRange = maxY - minY;
                if (xRange < 1) xRange = 1;
                if (std::abs(yRange) < 0.001) yRange = 0.1;
                
                ImPlot::SetupAxesLimits(minX - xRange * 0.05, maxX + xRange * 0.05,
                                       minY - std::abs(yRange) * 0.1, maxY + std::abs(yRange) * 0.1,
                                       ImPlotCond_Always);
            } else {
                ImPlot::SetupAxesLimits(0, 100, -0.1, 0.1, ImPlotCond_Always);
            }
        }
        
        // Plot each run
        for (size_t runIdx = 0; runIdx < m_simulationRuns.size(); ++runIdx) {
            const auto& run = m_simulationRuns[runIdx];
            
            if (!run.profitPlotX.empty()) {
                ImVec4 color = GetRunColor(runIdx);
                ImPlot::PushStyleColor(ImPlotCol_Line, color);
                
                // Plot based on current trade mode
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
                    std::string label = run.name + " (" + run.model_type + ")";
                    ImPlot::PlotLine(label.c_str(),
                                   run.profitPlotX.data(),
                                   plotData->data(),
                                   run.profitPlotX.size());
                }
                
                ImPlot::PopStyleColor();
            }
        }
        
        // Highlight selected run
        if (m_selectedRunIndex >= 0 && m_selectedRunIndex < (int)m_simulationRuns.size()) {
            const auto& run = m_simulationRuns[m_selectedRunIndex];
            if (!run.profitPlotX.empty()) {
                ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 3.0f);
                ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 1, 0, 1));
                
                // Plot selected based on current trade mode
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
                    ImPlot::PlotLine("##selected",
                                   run.profitPlotX.data(),
                                   plotData->data(),
                                   run.profitPlotX.size());
                }
                
                ImPlot::PopStyleColor();
                ImPlot::PopStyleVar();
            }
        }
        
        ImPlot::EndPlot();
    }
}

void SimulationResultsWidget::DrawRunSummary(const SimulationRun& run) {
    ImGui::Text("Run: %s", run.name.c_str());
    ImGui::Text("Model: %s", run.model_type.c_str());
    ImGui::Text("Status: %s", run.completed ? "Completed" : "Running");
    
    // Display features or feature schedule
    ImGui::Separator();
    if (run.using_feature_schedule && !run.feature_schedule.empty()) {
        ImGui::Text("Feature Schedule:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Schedule")) {
            ImGui::SetClipboardText(run.feature_schedule.c_str());
        }
        
        // Display the schedule in a scrollable text box
        ImGui::BeginChild("FeatureScheduleDisplay", ImVec2(0, 100), true);
        ImGui::TextWrapped("%s", run.feature_schedule.c_str());
        ImGui::EndChild();
    } else if (run.config) {
        // Display regular features for manual selection
        ImGui::Text("Features (%zu):", run.config->feature_columns.size());
        std::string features_str;
        for (size_t i = 0; i < run.config->feature_columns.size(); ++i) {
            if (i > 0) features_str += ", ";
            features_str += run.config->feature_columns[i];
        }
        ImGui::TextWrapped("%s", features_str.c_str());
    } else {
        ImGui::Text("%s", run.config_description.c_str());
    }
    
    if (!run.foldResults.empty()) {
        ImGui::Separator();
        ImGui::Text("Total Folds: %d", (int)run.foldResults.size());
        
        // Calculate statistics
        float avg_signals = 0, avg_hit_rate = 0, total_return = 0;
        int failed_folds = 0, cached_folds = 0;
        
        for (const auto& fold : run.foldResults) {
            avg_signals += fold.n_signals;
            avg_hit_rate += fold.hit_rate;
            total_return = fold.running_sum;  // Last one is total
            
            if (fold.model_learned_nothing && !fold.used_cached_model) {
                failed_folds++;
            } else if (fold.used_cached_model) {
                cached_folds++;
            }
        }
        
        avg_signals /= run.foldResults.size();
        avg_hit_rate /= run.foldResults.size();
        
        ImGui::Text("Avg Signals/Fold: %.1f", avg_signals);
        ImGui::Text("Avg Hit Rate: %.1f%%", avg_hit_rate * 100);
        ImGui::Text("Total Return: %.6f", total_return);
        
        if (failed_folds > 0) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), 
                             "Failed Folds: %d", failed_folds);
        }
        if (cached_folds > 0) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), 
                             "Cached Folds: %d", cached_folds);
        }
    }
    
    // Show duration
    ImGui::Text("Duration: %s", FormatDuration(run).c_str());
}

void SimulationResultsWidget::DrawFoldDetails(const FoldResult& fold) {
    ImGui::Text("Fold %d Details:", fold.fold_number);
    ImGui::Columns(2);
    
    ImGui::Text("Train: [%d, %d] (%d samples)", 
                fold.train_start, fold.train_end - 1, fold.n_train_samples);
    ImGui::Text("Val: %d samples", fold.n_val_samples);
    ImGui::Text("Test: [%d, %d] (%d samples)", 
                fold.test_start, fold.test_end - 1, fold.n_test_samples);
    
    ImGui::NextColumn();
    
    ImGui::Text("Best Iteration: %d", fold.best_iteration);
    ImGui::Text("Best Score: %.6f", fold.best_score);
    ImGui::Text("Threshold: %.6f", fold.prediction_threshold_original);
    
    ImGui::Columns(1);
    
    if (fold.model_learned_nothing && !fold.used_cached_model) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), 
                         "Model failed to learn - no trading");
    } else if (fold.used_cached_model) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), 
                         "Used cached model for predictions");
    }
}

ImVec4 SimulationResultsWidget::GetRunColor(int runIndex) const {
    const ImVec4 colors[] = {
        ImVec4(0.2f, 0.8f, 0.2f, 1.0f),  // Green
        ImVec4(0.2f, 0.2f, 0.8f, 1.0f),  // Blue
        ImVec4(0.8f, 0.2f, 0.8f, 1.0f),  // Magenta
        ImVec4(0.8f, 0.8f, 0.2f, 1.0f),  // Yellow
        ImVec4(0.2f, 0.8f, 0.8f, 1.0f),  // Cyan
        ImVec4(0.8f, 0.5f, 0.2f, 1.0f),  // Orange
    };
    
    return colors[runIndex % (sizeof(colors) / sizeof(colors[0]))];
}

ImVec4 SimulationResultsWidget::GetFoldColor(const FoldResult& fold) const {
    if (fold.model_learned_nothing && !fold.used_cached_model) {
        return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red for failed
    } else if (fold.used_cached_model) {
        return ImVec4(1.0f, 0.8f, 0.3f, 1.0f);  // Orange for cached
    } else {
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);  // Normal
    }
}

std::string SimulationResultsWidget::FormatDuration(const SimulationRun& run) const {
    if (!run.completed) {
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            now - run.startTime);
        
        int seconds = duration.count();
        int minutes = seconds / 60;
        seconds %= 60;
        
        std::ostringstream oss;
        oss << minutes << "m " << seconds << "s (running)";
        return oss.str();
    }
    
    // For completed runs, calculate from fold count
    // Approximate 0.5 seconds per fold
    int total_seconds = run.foldResults.size() / 2;
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    
    std::ostringstream oss;
    oss << minutes << "m " << seconds << "s";
    return oss.str();
}

const SimulationRun* SimulationResultsWidget::GetSelectedRun() const {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    
    if (m_selectedRunIndex >= 0 && m_selectedRunIndex < (int)m_simulationRuns.size()) {
        return &m_simulationRuns[m_selectedRunIndex];
    }
    return nullptr;
}

} // namespace ui
} // namespace simulation

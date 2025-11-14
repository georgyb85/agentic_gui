#include "BivarAnalysisWidget.h"
#include "TimeSeriesWindow.h"
#include "bivariate_analysis_exact.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <thread>

BivarAnalysisWidget::BivarAnalysisWidget()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_selectedTargetIndex(-1)
    , m_numPredictorBins(3)
    , m_numTargetBins(3)
    , m_criterionType(1)
    , m_mcptType(1)
    , m_numPermutations(1000)
    , m_isRunning(false)
    , m_hasResults(false)
    , m_maxResultsToShow(20)
    , m_sortDescending(true)
{
    m_statusMessage = "Ready";
}

void BivarAnalysisWidget::Draw() {
    if (!m_isVisible) {
        return;
    }
    
    // Check if analysis is complete
    if (m_isRunning && m_analysisFuture.valid() &&
        m_analysisFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        
        try {
            m_results = m_analysisFuture.get();
            m_hasResults = true;
            m_statusMessage = "Analysis complete. Found " + std::to_string(m_results.size()) + " predictor pairs.";
        } catch (const std::exception& e) {
            m_statusMessage = "Analysis failed: " + std::string(e.what());
        }
        m_isRunning = false;
    }
    
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Bivariate Analysis", &m_isVisible)) {
        
        // Column selection section
        ImGui::BeginChild("ColumnSelection", ImVec2(0, COLUMN_SELECTION_HEIGHT), true);
        ImGui::Text("Column Selection");
        ImGui::Separator();
        DrawColumnSelection();
        ImGui::EndChild();
        
        ImGui::Separator();
        
        // Parameter settings section
        ImGui::BeginChild("Parameters", ImVec2(0, PARAMETER_SECTION_HEIGHT), true);
        ImGui::Text("Analysis Parameters");
        ImGui::Separator();
        DrawParameterSettings();
        ImGui::EndChild();
        
        ImGui::Separator();
        
        // Run controls
        DrawRunControls();
        
        ImGui::Separator();
        
        // Results table
        if (m_hasResults) {
            float remainingHeight = ImGui::GetContentRegionAvail().y - STATUS_BAR_HEIGHT - 10.0f;
            ImGui::BeginChild("Results", ImVec2(0, remainingHeight), true);
            DrawResultsTable();
            ImGui::EndChild();
        } else {
            ImGui::Text("No results to display. Configure parameters and run analysis.");
        }
        
        DrawStatusBar();
    }
    ImGui::End();
}

void BivarAnalysisWidget::DrawColumnSelection() {
    if (!m_dataSource || !m_dataSource->HasData()) {
        ImGui::Text("No data available. Load data in Time Series Window first.");
        return;
    }
    
    ImGui::Columns(2, "ColumnSelectionColumns", true);
    
    // Predictor selection
    ImGui::Text("Select Predictors (minimum 2):");
    ImGui::Separator();
    
    for (size_t i = 0; i < m_availableColumns.size(); i++) {
        if (m_availableColumns[i] == "Date" || m_availableColumns[i] == "Time" || 
            m_availableColumns[i] == "timestamp_unix" || m_availableColumns[i] == "timestamp") {
            continue; // Skip time columns
        }
        
        bool selected = (i < m_selectedPredictors.size()) ? m_selectedPredictors[i] : false;
        if (ImGui::Checkbox((m_availableColumns[i] + "##pred").c_str(), &selected)) {
            if (i >= m_selectedPredictors.size()) {
                m_selectedPredictors.resize(i + 1, false);
            }
            m_selectedPredictors[i] = selected;
        }
    }
    
    ImGui::NextColumn();
    
    // Target selection
    ImGui::Text("Select Target:");
    ImGui::Separator();
    
    for (size_t i = 0; i < m_availableColumns.size(); i++) {
        if (m_availableColumns[i] == "Date" || m_availableColumns[i] == "Time" || 
            m_availableColumns[i] == "timestamp_unix" || m_availableColumns[i] == "timestamp") {
            continue; // Skip time columns
        }
        
        bool selected = (static_cast<int>(i) == m_selectedTargetIndex);
        if (ImGui::RadioButton((m_availableColumns[i] + "##target").c_str(), selected)) {
            m_selectedTargetIndex = static_cast<int>(i);
        }
    }
    
    ImGui::Columns(1);
}

void BivarAnalysisWidget::DrawParameterSettings() {
    ImGui::Columns(2, "ParameterColumns", true);
    
    // Left column - Binning parameters
    ImGui::Text("Binning:");
    ImGui::SliderInt("Predictor Bins", &m_numPredictorBins, 2, 10);
    ImGui::SliderInt("Target Bins", &m_numTargetBins, 2, 10);
    
    ImGui::NextColumn();
    
    // Right column - Analysis parameters
    ImGui::Text("Analysis:");
    
    const char* criterionItems[] = { "Mutual Information", "Uncertainty Reduction" };
    int criterionIndex = m_criterionType - 1;
    if (ImGui::Combo("Criterion", &criterionIndex, criterionItems, 2)) {
        m_criterionType = criterionIndex + 1;
    }
    
    const char* mcptItems[] = { "None", "Complete", "Cyclic" };
    if (ImGui::Combo("MCPT Type", &m_mcptType, mcptItems, 3)) {
        // m_mcptType is already correct (0, 1, 2)
    }
    
    if (m_mcptType > 0) {
        ImGui::SliderInt("Permutations", &m_numPermutations, 100, 10000);
    }
    
    ImGui::Columns(1);
}

void BivarAnalysisWidget::DrawRunControls() {
    bool canRun = !m_isRunning && m_dataSource && m_dataSource->HasData() && 
                  m_selectedTargetIndex >= 0;
    
    // Check if at least 2 predictors are selected
    int selectedPredCount = 0;
    for (bool selected : m_selectedPredictors) {
        if (selected) selectedPredCount++;
    }
    canRun = canRun && (selectedPredCount >= 2);
    
    if (!canRun) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Run Analysis", ImVec2(120, 30))) {
        RunAnalysis();
    }
    
    if (!canRun) {
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Clear Results", ImVec2(120, 30))) {
        ClearResults();
    }
    
    ImGui::SameLine();
    ImGui::Text("Max Results:");
    ImGui::SameLine();
    ImGui::SliderInt("##maxresults", &m_maxResultsToShow, 10, 100);
}

void BivarAnalysisWidget::DrawResultsTable() {
    if (m_results.empty()) return;
    
    // Sort results by criterion if needed
    if (m_sortDescending) {
        std::sort(m_results.begin(), m_results.end(),
                 [](const BivariateResult& a, const BivariateResult& b) {
                     return a.criterion > b.criterion;
                 });
    }
    
    const int numResults = std::min(m_maxResultsToShow, static_cast<int>(m_results.size()));
    
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_Sortable;
    
    int numColumns = (m_mcptType > 0) ? 6 : 4;
    
    if (ImGui::BeginTable("ResultsTable", numColumns, flags)) {
        ImGui::TableSetupColumn("Predictor 1");
        ImGui::TableSetupColumn("Predictor 2");
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn((m_criterionType == 1) ? "MI" : "UR");
        
        if (m_mcptType > 0) {
            ImGui::TableSetupColumn("p-solo");
            ImGui::TableSetupColumn("p-bestof");
        }
        
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        
        for (int i = 0; i < numResults; i++) {
            const auto& result = m_results[i];
            
            ImGui::TableNextRow();
            
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", result.pred1_name.c_str());
            
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", result.pred2_name.c_str());
            
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", m_availableColumns[m_selectedTargetIndex].c_str());
            
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.4f", result.criterion);
            
            if (m_mcptType > 0) {
                ImGui::TableSetColumnIndex(4);
                if (result.p_value_solo >= 0.0) {
                    ImGui::Text("%.4f", result.p_value_solo);
                } else {
                    ImGui::Text("N/A");
                }
                
                ImGui::TableSetColumnIndex(5);
                if (result.p_value_bestof >= 0.0) {
                    ImGui::Text("%.4f", result.p_value_bestof);
                } else {
                    ImGui::Text("N/A");
                }
            }
        }
        
        ImGui::EndTable();
    }
}

void BivarAnalysisWidget::DrawStatusBar() {
    ImGui::Separator();
    
    if (m_isRunning) {
        ImGui::Text("Status: Running analysis...");
    } else {
        ImGui::Text("Status: %s", m_statusMessage.c_str());
    }
}

void BivarAnalysisWidget::RunAnalysis() {
    if (m_isRunning || !m_dataSource || !m_dataSource->HasData()) {
        return;
    }
    
    // Start timing from the moment user clicks Run Analysis
    auto analysis_start_time = std::chrono::high_resolution_clock::now();
    std::cout << "[BivarAnalysis] ========= ANALYSIS STARTED =========" << std::endl;
    std::cout << "[BivarAnalysis] User clicked 'Run Analysis' button" << std::endl;
    
    // Collect selected predictor names
    std::vector<std::string> predictorNames;
    for (size_t i = 0; i < m_selectedPredictors.size() && i < m_availableColumns.size(); i++) {
        if (m_selectedPredictors[i]) {
            predictorNames.push_back(m_availableColumns[i]);
        }
    }
    
    if (predictorNames.size() < 2 || m_selectedTargetIndex < 0) {
        m_statusMessage = "Error: Need at least 2 predictors and 1 target selected.";
        return;
    }
    
    std::string targetName = m_availableColumns[m_selectedTargetIndex];
    
    std::cout << "[BivarAnalysis] Configuration:" << std::endl;
    std::cout << "  - Hardware threads: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << "  - Predictors: " << predictorNames.size() << std::endl;
    std::cout << "  - Target: " << targetName << std::endl;
    std::cout << "  - Predictor bins: " << m_numPredictorBins << std::endl;
    std::cout << "  - Target bins: " << m_numTargetBins << std::endl;
    std::cout << "  - Criterion: " << (m_criterionType == 1 ? "MI" : "UR") << std::endl;
    std::cout << "  - MCPT type: " << m_mcptType << std::endl;
    std::cout << "  - Permutations: " << m_numPermutations << std::endl;
    
    m_isRunning = true;
    m_statusMessage = "Running analysis...";
    
    // Launch analysis in background thread
    m_analysisFuture = std::async(std::launch::async, [=, &analysis_start_time]() -> std::vector<BivariateResult> {
        auto thread_start = std::chrono::high_resolution_clock::now();
        auto launch_overhead = std::chrono::duration_cast<std::chrono::milliseconds>(thread_start - analysis_start_time);
        std::cout << "[BivarAnalysis] Background thread started (launch overhead: " << launch_overhead.count() << " ms)" << std::endl;
        
        const chronosflow::AnalyticsDataFrame* df = m_dataSource->GetDataFrame();
        std::cout << "[BivarAnalysis] DataFrame accessed, rows: " << df->num_rows() 
                  << ", cols: " << df->num_columns() << std::endl;
        
        auto results = screen_bivariate(*df, predictorNames, targetName,
                                       m_numPredictorBins, m_numTargetBins,
                                       m_criterionType, m_mcptType, m_numPermutations);
        
        auto thread_end = std::chrono::high_resolution_clock::now();
        auto thread_duration = std::chrono::duration_cast<std::chrono::milliseconds>(thread_end - thread_start);
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(thread_end - analysis_start_time);
        
        std::cout << "[BivarAnalysis] ========= ANALYSIS COMPLETED =========" << std::endl;
        std::cout << "[BivarAnalysis] Analysis thread time: " << thread_duration.count() << " ms ("
                  << std::fixed << std::setprecision(2) << thread_duration.count() / 1000.0 << " seconds)" << std::endl;
        std::cout << "[BivarAnalysis] Total time from button click: " << total_duration.count() << " ms ("
                  << std::fixed << std::setprecision(2) << total_duration.count() / 1000.0 << " seconds)" << std::endl;
        std::cout << "[BivarAnalysis] Results found: " << results.size() << " predictor pairs" << std::endl;
        std::cout << "[BivarAnalysis] =====================================" << std::endl;
        
        return results;
    });
}

void BivarAnalysisWidget::ClearResults() {
    m_results.clear();
    m_hasResults = false;
    m_statusMessage = "Results cleared.";
}

void BivarAnalysisWidget::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void BivarAnalysisWidget::UpdateColumnList() {
    m_availableColumns.clear();
    m_selectedPredictors.clear();
    m_selectedTargetIndex = -1;
    
    if (m_dataSource && m_dataSource->HasData()) {
        const chronosflow::AnalyticsDataFrame* df = m_dataSource->GetDataFrame();
        if (df) {
            m_availableColumns = df->column_names();
            m_selectedPredictors.resize(m_availableColumns.size(), false);
        }
    }
}

#include "ESSWindow.h"
#include "TimeSeriesWindow.h"
#include "analytics_dataframe.h"
#include "simple_logger.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>

ESSWindow::ESSWindow()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_modelType(stepwise::ModelType::LINEAR_QUADRATIC)
    , m_nKept(5)
    , m_nFolds(4)
    , m_minPredictors(1)
    , m_maxPredictors(20)
    , m_mcptReplications(100)
    , m_mcptType(0)  // COMPLETE
    , m_earlyTermination(true)
    , m_useSubsets(false)
    , m_numSubsets(5)
    , m_subsetSize(30000)  // Default 30000 data points per subset
    , m_overlapSize(5000)  // Default 5000 data points overlap
    , m_globalStandardization(false)  // Default to per-subset like console version
    , m_isRunning(false)
    , m_stopRequested(false)
    , m_hasResults(false)
    , m_autoScroll(true)
{
    m_statusMessage = "Ready";
    
    // Configure feature selector
    m_featureSelector.SetTargetPrefix("TGT");
    m_featureSelector.SetShowOnlyTargetsWithPrefix(true);
    m_featureSelector.SetSortAlphabetically(true);
}

void ESSWindow::Draw() {
    if (!m_isVisible) {
        return;
    }
    
    // Check if analysis is complete
    if (m_isRunning && m_analysisFuture.valid() &&
        m_analysisFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        
        try {
            m_analysisFuture.get();
            m_hasResults = true;
            // Buffer already updated in real-time via logger callback
            m_statusMessage = "Analysis complete.";
        } catch (const std::exception& e) {
            m_statusMessage = "Analysis failed: " + std::string(e.what());
            
            // Add error to buffer
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_resultsText << "\nERROR: " << e.what() << "\n";
            m_resultsString = m_resultsText.str();
            m_resultsBuffer.resize(m_resultsString.size() + 1024);
            std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
            m_resultsBuffer[m_resultsString.size()] = '\0';
        }
        m_isRunning = false;
    }
    
    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Enhanced Stepwise Selection", &m_isVisible)) {
        
        // Column selection section
        ImGui::BeginChild("ColumnSelection", ImVec2(0, COLUMN_SELECTION_HEIGHT), true);
        ImGui::Text("Feature Selection");
        ImGui::Separator();
        DrawColumnSelection();
        ImGui::EndChild();
        
        ImGui::Separator();
        
        // Model selection section
        DrawModelSelection();
        
        ImGui::Separator();
        
        // Configuration settings section
        ImGui::BeginChild("Configuration", ImVec2(0, CONFIG_SECTION_HEIGHT), true);
        ImGui::Text("Algorithm Configuration");
        ImGui::Separator();
        DrawConfigurationSettings();
        ImGui::EndChild();
        
        ImGui::Separator();
        
        // Run controls
        DrawRunControls();
        
        ImGui::Separator();
        
        // Results output
        float remainingHeight = ImGui::GetContentRegionAvail().y - STATUS_BAR_HEIGHT - 10.0f;
        ImGui::BeginChild("Results", ImVec2(0, remainingHeight), true);
        DrawResultsOutput();
        ImGui::EndChild();
        
        DrawStatusBar();
    }
    ImGui::End();
}

void ESSWindow::DrawColumnSelection() {
    if (!m_dataSource || !m_dataSource->HasData()) {
        ImGui::Text("No data available. Load data in Time Series Window first.");
        return;
    }
    
    // Use the feature selector widget
    m_featureSelector.Draw();
}

void ESSWindow::DrawModelSelection() {
    // Use a fixed height for the model selection section
    float height = (m_modelType == stepwise::ModelType::XGBOOST) ? 400.0f : 120.0f;
    ImGui::BeginChild("ModelSelection", ImVec2(0, height), true);
    ImGui::Text("Model Selection");
    ImGui::Separator();
    
    // Model type selection
    const char* modelTypes[] = { "Linear-Quadratic", "XGBoost" };
    int currentModel = static_cast<int>(m_modelType);
    
    if (ImGui::Combo("Model Type", &currentModel, modelTypes, IM_ARRAYSIZE(modelTypes))) {
        m_modelType = static_cast<stepwise::ModelType>(currentModel);
    }
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Linear-Quadratic: Fast traditional model with interpretable coefficients\n"
                         "XGBoost: Gradient boosting model that can capture non-linear patterns");
    }
    
    // Show XGBoost configuration if selected
    if (m_modelType == stepwise::ModelType::XGBOOST) {
        ImGui::Separator();
        ImGui::Text("XGBoost Parameters:");
        m_xgboostConfigWidget.Draw();
    } else {
        ImGui::Text("Linear-Quadratic model uses automatic configuration.");
    }
    
    ImGui::EndChild();
}

void ESSWindow::DrawConfigurationSettings() {
    ImGui::Columns(3, "ConfigColumns", true);
    
    // First column - Basic settings
    ImGui::Text("Basic Settings:");
    ImGui::SliderInt("N Kept", &m_nKept, 1, 20, "%d");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Number of best feature sets retained per step");
    }
    
    ImGui::SliderInt("CV Folds", &m_nFolds, 2, 10, "%d");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Number of cross-validation folds");
    }
    
    ImGui::SliderInt("Min Predictors", &m_minPredictors, 1, 10, "%d");
    ImGui::SliderInt("Max Predictors", &m_maxPredictors, 1, 50, "%d");
    
    ImGui::NextColumn();
    
    // Second column - MCPT settings
    ImGui::Text("Monte Carlo Permutation Test:");
    ImGui::SliderInt("MCPT Replications", &m_mcptReplications, 0, 1000, "%d");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Number of Monte Carlo permutation test replications\n0 = disabled");
    }
    
    const char* mcptTypes[] = { "Complete", "Cyclic" };
    ImGui::Combo("MCPT Type", &m_mcptType, mcptTypes, IM_ARRAYSIZE(mcptTypes));
    
    ImGui::Checkbox("Early Termination", &m_earlyTermination);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stop if performance degrades");
    }
    
    ImGui::NextColumn();
    
    // Third column - Subset settings
    ImGui::Text("Data Subset Analysis:");
    ImGui::Checkbox("Use Subsets", &m_useSubsets);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Split data into subsets and run analysis on each");
    }
    
    if (m_useSubsets) {
        ImGui::InputInt("Subset Size", &m_subsetSize, 1000, 5000);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Number of data points per subset");
        }
        
        ImGui::InputInt("Overlap Size", &m_overlapSize, 100, 1000);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Number of data points to overlap between consecutive subsets");
        }
        
        // Validate overlap size
        if (m_overlapSize >= m_subsetSize) {
            m_overlapSize = m_subsetSize - 1000;
        }
        if (m_overlapSize < 0) {
            m_overlapSize = 0;
        }
        
        ImGui::Checkbox("Global Standardization", &m_globalStandardization);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If checked: standardize all data together then split\nIf unchecked: standardize each subset independently");
        }
    }
    
    ImGui::Columns(1);
}

void ESSWindow::DrawRunControls() {
    // Get selected features and target from widget
    auto selectedFeatures = m_featureSelector.GetSelectedFeatures();
    std::string selectedTarget = m_featureSelector.GetSelectedTarget();
    
    // Count selected predictors
    int numSelectedPredictors = static_cast<int>(selectedFeatures.size());
    
    // Validation
    bool canRun = !m_isRunning && 
                  m_dataSource && m_dataSource->HasData() &&
                  numSelectedPredictors >= 2 &&
                  !selectedTarget.empty();
    
    if (!canRun) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Run Analysis", ImVec2(120, 0))) {
        m_stopRequested = false;
        if (m_useSubsets) {
            RunAnalysisOnSubsets();
        } else {
            RunAnalysis();
        }
    }
    
    if (!canRun) {
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    // Stop button (only enabled when running)
    if (!m_isRunning) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Stop", ImVec2(120, 0))) {
        m_stopRequested = true;
        m_statusMessage = "Stopping analysis (waiting for current iteration)...";
    }
    
    if (!m_isRunning) {
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Clear Results", ImVec2(120, 0))) {
        ClearResults();
    }
    
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    
    // Display validation messages
    if (selectedTarget.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Please select a target variable");
    } else if (numSelectedPredictors < 2) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Please select at least 2 predictor variables");
    } else {
        ImGui::SameLine();
        ImGui::Text("Selected: %d predictors, target: %s", 
                   numSelectedPredictors,
                   selectedTarget.c_str());
    }
}

void ESSWindow::DrawResultsOutput() {
    // Add tabs for results and feature schedule
    if (ImGui::BeginTabBar("ResultsTabs")) {
        
        // Results tab
        if (ImGui::BeginTabItem("Results")) {
            // Always show the buffer if it has content (even during analysis)
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            if (m_resultsBuffer.size() > 1) {  // > 1 because we always have null terminator
                // Display results in a scrollable text area (using safe buffer)
                ImGui::InputTextMultiline("##Results", 
                                          m_resultsBuffer.data(), 
                                          m_resultsBuffer.size(),
                                          ImVec2(-1, -1),
                                          ImGuiInputTextFlags_ReadOnly);
                
                if (m_autoScroll && m_isRunning) {
                    // Auto-scroll to bottom while running
                    ImGui::SetScrollHereY(1.0f);
                }
            } else if (m_isRunning) {
                ImGui::Text("Analysis starting...");
                
                // Simple progress indicator
                static float progress = 0.0f;
                progress += ImGui::GetIO().DeltaTime * 0.5f;
                if (progress > 1.0f) progress -= 1.0f;
                ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
            } else {
                ImGui::Text("No results to display. Configure parameters and run analysis.");
            }
            ImGui::EndTabItem();
        }
        
        // Feature Schedule tab
        if (ImGui::BeginTabItem("Feature Schedule")) {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            if (m_featureScheduleBuffer.size() > 1) {
                // Display feature schedule in a scrollable text area
                ImGui::InputTextMultiline("##FeatureSchedule", 
                                          m_featureScheduleBuffer.data(), 
                                          m_featureScheduleBuffer.size(),
                                          ImVec2(-1, -1),
                                          ImGuiInputTextFlags_ReadOnly);
            } else {
                ImGui::TextWrapped("Feature schedule will appear here after running subset analysis...");
                ImGui::TextWrapped("Format: startRow-endRow: feature1, feature2, ...");
            }
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
}

void ESSWindow::DrawStatusBar() {
    ImGui::Separator();
    ImGui::Text("Status: %s", m_statusMessage.c_str());
}

void ESSWindow::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void ESSWindow::UpdateColumnList() {
    if (!m_dataSource || !m_dataSource->HasData()) {
        m_availableColumns.clear();
        m_featureSelector.SetAvailableColumns(m_availableColumns);
        return;
    }
    
    const auto* df = m_dataSource->GetDataFrame();
    if (!df) return;
    
    m_availableColumns = df->column_names();
    
    // Update feature selector with available columns
    m_featureSelector.SetAvailableColumns(m_availableColumns);
}

void ESSWindow::PrepareDataForAnalysis(DataMatrix& X, std::vector<double>& y, 
                                       int startRow, int endRow) {
    const auto* df = m_dataSource->GetDataFrame();
    if (!df) {
        throw std::runtime_error("No data frame available");
    }
    
    // Get selected features from widget
    std::vector<std::string> predictorNames = m_featureSelector.GetSelectedFeatures();
    std::string targetName = m_featureSelector.GetSelectedTarget();
    
    if (predictorNames.empty()) {
        throw std::runtime_error("No predictors selected");
    }
    
    if (targetName.empty()) {
        throw std::runtime_error("No target selected");
    }
    
    // Get predictor indices
    std::vector<int> predictorIndices;
    for (const auto& predName : predictorNames) {
        auto it = std::find(m_availableColumns.begin(), m_availableColumns.end(), predName);
        if (it != m_availableColumns.end()) {
            predictorIndices.push_back(static_cast<int>(std::distance(m_availableColumns.begin(), it)));
        }
    }
    
    // Determine row range
    int numRows = df->num_rows();
    if (startRow < 0 || startRow >= numRows) startRow = 0;
    if (endRow < 0 || endRow > numRows) endRow = numRows;
    int rowsToUse = endRow - startRow;
    
    
    // Resize data matrix
    X.resize(rowsToUse, predictorIndices.size());
    X.set_column_names(predictorNames);
    y.resize(rowsToUse);
    
    // Copy predictor data efficiently using column views
    for (size_t col = 0; col < predictorIndices.size(); col++) {
        auto column_result = df->get_column_view<double>(m_availableColumns[predictorIndices[col]]);
        if (!column_result.ok()) {
            throw std::runtime_error("Failed to get column: " + m_availableColumns[predictorIndices[col]]);
        }
        auto column_view = std::move(column_result).ValueOrDie();  // Use move semantics
        const double* column_data = column_view.data();
        double* destCol = X.get_column(col);
        
        // Copy data for the specified row range and check for NaN/Inf
        int nanCount = 0;
        int infCount = 0;
        for (int row = 0; row < rowsToUse; row++) {
            double value = column_data[startRow + row];
            if (std::isnan(value)) {
                nanCount++;
                value = 0.0;  // Replace NaN with 0
            } else if (std::isinf(value)) {
                infCount++;
                value = 0.0;  // Replace Inf with 0
            }
            destCol[row] = value;
        }
        
        if (nanCount > 0 || infCount > 0) {
            // Log warning but continue
            std::string warning = "WARNING: Column " + m_availableColumns[predictorIndices[col]] + 
                                 " has " + std::to_string(nanCount) + " NaN and " + 
                                 std::to_string(infCount) + " Inf values (replaced with 0) - THIS AFFECTS R-SQUARED!\n";
            SimpleLogger::Log(warning);
        }
    }
    
    // Copy target data
    auto target_result = df->get_column_view<double>(targetName);
    if (!target_result.ok()) {
        throw std::runtime_error("Failed to get target column: " + targetName);
    }
    auto target_view = std::move(target_result).ValueOrDie();  // Use move semantics
    const double* target_data = target_view.data();
    
    // Check target for NaN/Inf
    int targetNanCount = 0;
    int targetInfCount = 0;
    for (int row = 0; row < rowsToUse; row++) {
        double value = target_data[startRow + row];
        if (std::isnan(value)) {
            targetNanCount++;
            value = 0.0;  // Replace NaN with 0
        } else if (std::isinf(value)) {
            targetInfCount++;
            value = 0.0;  // Replace Inf with 0
        }
        y[row] = value;
    }
    
    if (targetNanCount > 0 || targetInfCount > 0) {
        std::string warning = "Warning: Target " + targetName + 
                             " has " + std::to_string(targetNanCount) + " NaN and " + 
                             std::to_string(targetInfCount) + " Inf values (replaced with 0)\n";
        SimpleLogger::Log(warning);
    }
    
    // Check for constant columns BEFORE standardization and remove them
    std::vector<int> validPredictorIndices;
    std::vector<std::string> validPredictorNames;
    
    for (size_t col = 0; col < predictorIndices.size(); col++) {
        const double* colData = X.get_column(col);
        
        // Calculate variance
        double mean = 0.0;
        for (int row = 0; row < rowsToUse; row++) {
            mean += colData[row];
        }
        mean /= rowsToUse;
        
        double variance = 0.0;
        for (int row = 0; row < rowsToUse; row++) {
            double diff = colData[row] - mean;
            variance += diff * diff;
        }
        variance /= rowsToUse;
        
        // Only keep columns with sufficient variance
        if (variance > 1e-10) {
            validPredictorIndices.push_back(col);
            validPredictorNames.push_back(predictorNames[col]);
        } else {
            std::string warning = "REMOVING constant column: " + predictorNames[col] + 
                                 " (variance=" + std::to_string(variance) + ") - CHANGES FEATURE SET!\n";
            SimpleLogger::Log(warning);
        }
    }
    
    // If we removed columns, rebuild the matrix with only valid columns
    if (validPredictorIndices.size() < predictorIndices.size()) {
        DataMatrix newX(rowsToUse, validPredictorIndices.size());
        newX.set_column_names(validPredictorNames);
        
        for (size_t newCol = 0; newCol < validPredictorIndices.size(); newCol++) {
            const double* srcCol = X.get_column(validPredictorIndices[newCol]);
            double* destCol = newX.get_column(newCol);
            for (int row = 0; row < rowsToUse; row++) {
                destCol[row] = srcCol[row];
            }
        }
        
        X = std::move(newX);
        predictorNames = validPredictorNames;
    }
    
    if (X.cols() == 0) {
        throw std::runtime_error("All predictor columns were constant and removed. Cannot proceed with analysis.");
    }
    
    // Check if we have enough rows for the number of predictors
    if (rowsToUse < static_cast<int>(X.cols())) {
        throw std::runtime_error("Insufficient data: " + std::to_string(rowsToUse) + " rows for " + 
                                std::to_string(X.cols()) + " predictors. Need at least as many rows as predictors.");
    }
    
    // Now standardize the remaining columns
    for (size_t col = 0; col < X.cols(); col++) {
        X.standardize_column(col);
    }
    
    // Standardize target to unit variance (required for R-square calculation)
    double target_mean = 0.0;
    for (double val : y) {
        target_mean += val;
    }
    target_mean /= y.size();
    
    double target_var = 0.0;
    for (double val : y) {
        double diff = val - target_mean;
        target_var += diff * diff;
    }
    target_var /= y.size();  // Use population variance (n divisor, not n-1)
    
    if (target_var < 1e-10) {
        throw std::runtime_error("Target column " + targetName + " has zero variance. Cannot perform regression.");
    }
    
    double target_std = std::sqrt(target_var);
    
    
    for (double& val : y) {
        val = (val - target_mean) / target_std;
    }
    
    
    // Final validation: check for NaN/Inf after all transformations
    for (size_t col = 0; col < X.cols(); col++) {
        const double* colData = X.get_column(col);
        for (int row = 0; row < rowsToUse; row++) {
            if (std::isnan(colData[row]) || std::isinf(colData[row])) {
                throw std::runtime_error("NaN or Inf detected in predictor " + X.get_column_name(col) + " after standardization");
            }
        }
    }
    
    for (const double& val : y) {
        if (std::isnan(val) || std::isinf(val)) {
            throw std::runtime_error("NaN or Inf detected in target after standardization");
        }
    }
}

void ESSWindow::RunAnalysis() {
    if (m_isRunning) return;
    
    m_isRunning = true;
    m_hasResults = false;
    
    // Clear results with mutex lock
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_resultsText.str("");
        m_resultsText.clear();
        m_resultsBuffer.clear();
        m_resultsBuffer.push_back('\0');
    }
    
    m_subsetResults.clear();
    m_statusMessage = "Running analysis...";
    
    // Launch analysis in background thread
    m_analysisFuture = std::async(std::launch::async, [this]() {
        try {
            // Set up logger callback to capture output and update buffer in real-time
            SimpleLogger::SetCallback([this](const std::string& msg) {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << msg << "\n";
                
                // Update the buffer immediately for real-time display
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            });
            
            // Check which model type to use
            if (m_modelType == stepwise::ModelType::XGBOOST) {
                // Use new V2 interface with XGBoost
                stepwise::EnhancedStepwiseV2::StepwiseConfig config;
                config.n_kept = m_nKept;
                config.n_folds = m_nFolds;
                config.min_predictors = m_minPredictors;
                config.max_predictors = m_maxPredictors;
                config.mcpt_replications = m_mcptReplications;
                config.mcpt_type = (m_mcptType == 0) ? 
                    stepwise::EnhancedStepwiseSelectorV2::SelectionConfig::COMPLETE : 
                    stepwise::EnhancedStepwiseSelectorV2::SelectionConfig::CYCLIC;
                config.early_termination = m_earlyTermination;
                config.cancel_callback = [this]() { return m_stopRequested.load(); };
                
                // Create XGBoost model
                auto xgb_config = m_xgboostConfigWidget.GetConfig();
                auto model = std::make_unique<stepwise::XGBoostModel>(xgb_config);
                
                // Create algorithm instance with XGBoost
                stepwise::EnhancedStepwiseV2 stepwise(std::move(model), config);
                
                // Prepare data
                DataMatrix X;
                std::vector<double> y;
                PrepareDataForAnalysis(X, y);
                
                // Get predictor names and target
                std::vector<std::string> predictorNames = X.get_column_names();
                std::string targetName = m_featureSelector.GetSelectedTarget();
                
                // Log analysis start
                {
                    std::lock_guard<std::mutex> lock(m_bufferMutex);
                    m_resultsText << "=== ENHANCED STEPWISE SELECTION (XGBoost) ===\n";
                    m_resultsText << "Model Type: XGBoost\n";
                    m_resultsText << "XGBoost Config: " << xgb_config.ToString() << "\n";
                    m_resultsText << "Target: " << targetName << "\n";
                    m_resultsText << "Data dimensions: " << X.rows() << " rows x " << X.cols() << " columns\n";
                    m_resultsText << "Predictors: " << predictorNames.size() << "\n";
                    for (const auto& name : predictorNames) {
                        m_resultsText << "  - " << name << "\n";
                    }
                    m_resultsText << "Configuration:\n";
                    m_resultsText << "  - N Kept: " << config.n_kept << "\n";
                    m_resultsText << "  - CV Folds: " << config.n_folds << "\n";
                    m_resultsText << "  - Min Predictors: " << config.min_predictors << "\n";
                    m_resultsText << "  - Max Predictors: " << config.max_predictors << "\n";
                    m_resultsText << "  - MCPT Replications: " << config.mcpt_replications << "\n";
                    m_resultsText << "  - Early Termination: " << (config.early_termination ? "Yes" : "No") << "\n";
                    m_resultsText << "\nStarting analysis...\n";
                    m_resultsText << "========================================\n\n";
                    
                    // Update buffer immediately
                    m_resultsString = m_resultsText.str();
                    m_resultsBuffer.resize(m_resultsString.size() + 1024);
                    std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                    m_resultsBuffer[m_resultsString.size()] = '\0';
                }
                
                // Run stepwise selection
                auto results = stepwise.run_on_data(X, y, predictorNames, targetName);
                
                // Store results
                SubsetResult subsetResult;
                subsetResult.subsetIndex = 0;
                subsetResult.startRow = 0;
                subsetResult.endRow = X.rows();
                subsetResult.results_v2 = results;
                subsetResult.use_v2 = true;
                m_subsetResults.push_back(subsetResult);
                
                // Format results
                {
                    std::lock_guard<std::mutex> lock(m_bufferMutex);
                    m_resultsText << "\nRESULTS:\n";
                    m_resultsText << "--------\n";
                    m_resultsText << "Selected Features (" << results.selected_feature_names.size() << "):\n";
                    for (size_t i = 0; i < results.selected_feature_names.size(); i++) {
                        m_resultsText << "  " << (i+1) << ". " << results.selected_feature_names[i];
                        if (i < results.model_p_values.size()) {
                            m_resultsText << " (p=" << std::fixed << std::setprecision(4) 
                                         << results.model_p_values[i] << ")";
                        }
                        m_resultsText << "\n";
                    }
                    
                    m_resultsText << "\nFinal R-squared: " << std::fixed << std::setprecision(4) 
                                 << results.final_r_square << "\n";
                    
                    if (results.terminated_early) {
                        m_resultsText << "Early termination: " << results.termination_reason << "\n";
                    }
                    
                    // For XGBoost, show feature importances instead of coefficients
                    if (!results.feature_importances.empty()) {
                        m_resultsText << "\nFeature Importances:\n";
                        for (size_t i = 0; i < results.selected_feature_names.size() && i < results.feature_importances.size(); i++) {
                            m_resultsText << "  " << results.selected_feature_names[i] << ": " 
                                         << std::fixed << std::setprecision(2) 
                                         << results.feature_importances[i] << "\n";
                        }
                    }
                    
                    m_resultsText << "\nTotal elapsed time: " << std::fixed << std::setprecision(2) 
                                 << results.total_elapsed_ms / 1000.0 << " seconds\n";
                    
                    // Update buffer
                    m_resultsString = m_resultsText.str();
                    m_resultsBuffer.resize(m_resultsString.size() + 1024);
                    std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                    m_resultsBuffer[m_resultsString.size()] = '\0';
                }
                
            } else {
                // Use original linear-quadratic model
                EnhancedStepwise::StepwiseConfig config;
                config.n_kept = m_nKept;
                config.n_folds = m_nFolds;
                config.min_predictors = m_minPredictors;
                config.max_predictors = m_maxPredictors;
                config.mcpt_replications = m_mcptReplications;
                config.mcpt_type = (m_mcptType == 0) ? 
                    EnhancedStepwise::StepwiseConfig::COMPLETE : 
                    EnhancedStepwise::StepwiseConfig::CYCLIC;
                config.early_termination = m_earlyTermination;
                
                // Set cancellation callback
                config.cancel_callback = [this]() { return m_stopRequested.load(); };
                
                // Create algorithm instance
                EnhancedStepwise stepwise(config);
            
            // Prepare data
            DataMatrix X;
            std::vector<double> y;
            PrepareDataForAnalysis(X, y);
            
            // Get predictor names and target
            std::vector<std::string> predictorNames = X.get_column_names();
            std::string targetName = m_featureSelector.GetSelectedTarget();
            
            // Log analysis start (write directly to buffer for immediate display)
            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << "=== ENHANCED STEPWISE SELECTION ===\n";
                m_resultsText << "Target: " << targetName << "\n";
                m_resultsText << "Data dimensions: " << X.rows() << " rows x " << X.cols() << " columns\n";
                m_resultsText << "Predictors: " << predictorNames.size() << "\n";
                for (const auto& name : predictorNames) {
                    m_resultsText << "  - " << name << "\n";
                }
                m_resultsText << "Configuration:\n";
                m_resultsText << "  - N Kept: " << config.n_kept << "\n";
                m_resultsText << "  - CV Folds: " << config.n_folds << "\n";
                m_resultsText << "  - Min Predictors: " << config.min_predictors << "\n";
                m_resultsText << "  - Max Predictors: " << config.max_predictors << "\n";
                m_resultsText << "  - MCPT Replications: " << config.mcpt_replications << "\n";
                m_resultsText << "  - Early Termination: " << (config.early_termination ? "Yes" : "No") << "\n";
                m_resultsText << "\nStarting analysis...\n";
                m_resultsText << "========================================\n\n";
                
                // Update buffer immediately
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            }
            
                // Run stepwise selection
                auto results = stepwise.run_on_data(X, y, predictorNames, targetName);
                
                // Store results
                SubsetResult subsetResult;
                subsetResult.subsetIndex = 0;
                subsetResult.startRow = 0;
                subsetResult.endRow = X.rows();
                subsetResult.results = results;
                subsetResult.use_v2 = false;
                m_subsetResults.push_back(subsetResult);
            
            // Format results (write directly to buffer)
            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << "\nRESULTS:\n";
                m_resultsText << "--------\n";
                m_resultsText << "Selected Features (" << results.selected_feature_names.size() << "):\n";
                for (size_t i = 0; i < results.selected_feature_names.size(); i++) {
                    m_resultsText << "  " << (i+1) << ". " << results.selected_feature_names[i];
                    if (i < results.model_p_values.size()) {
                        m_resultsText << " (p=" << std::fixed << std::setprecision(4) 
                                     << results.model_p_values[i] << ")";
                    }
                    m_resultsText << "\n";
                }
                
                m_resultsText << "\nFinal R-squared: " << std::fixed << std::setprecision(4) 
                             << results.final_r_square << "\n";
                
                if (results.terminated_early) {
                    m_resultsText << "Early termination: " << results.termination_reason << "\n";
                }
                
                // Print coefficients if available
                if (!results.final_coefficients.empty()) {
                    m_resultsText << "\nRegression Coefficients:\n";
                    m_resultsText << "  Intercept: " << std::fixed << std::setprecision(6) 
                                 << results.final_coefficients[0] << "\n";
                    for (size_t i = 0; i < results.selected_feature_names.size(); i++) {
                        if (i + 1 < results.final_coefficients.size()) {
                            m_resultsText << "  " << results.selected_feature_names[i] << ": " 
                                         << std::fixed << std::setprecision(6) 
                                         << results.final_coefficients[i + 1] << "\n";
                        }
                    }
                }
                
                // Step-by-step details
                if (!results.step_r_squares.empty()) {
                    m_resultsText << "\nStep-by-step R-squared:\n";
                    for (size_t i = 0; i < results.step_r_squares.size(); i++) {
                        m_resultsText << "  Step " << (i+1) << ": " 
                                     << std::fixed << std::setprecision(4) 
                                     << results.step_r_squares[i];
                        if (i < results.change_p_values.size()) {
                            m_resultsText << " (change p=" << results.change_p_values[i] << ")";
                        }
                        m_resultsText << "\n";
                    }
                }
                
                m_resultsText << "\nTotal cases: " << results.total_cases_loaded << "\n";
                m_resultsText << "Total steps: " << results.total_steps << "\n";
                m_resultsText << "Total time: " << std::fixed << std::setprecision(1) 
                             << results.total_elapsed_ms << " ms\n";
                             
                // Update buffer immediately
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            }
            } // End of else (linear-quadratic model)
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_resultsText << "\nERROR: " << e.what() << "\n";
            
            // Update buffer immediately
            m_resultsString = m_resultsText.str();
            m_resultsBuffer.resize(m_resultsString.size() + 1024);
            std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
            m_resultsBuffer[m_resultsString.size()] = '\0';
        }
        
        // Clear logger callback
        SimpleLogger::ClearCallback();
    });
}

void ESSWindow::RunAnalysisOnSubsets() {
    if (m_isRunning) return;
    
    m_isRunning = true;
    m_hasResults = false;
    
    // Clear results with mutex lock
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_resultsText.str("");
        m_resultsText.clear();
        m_resultsBuffer.clear();
        m_resultsBuffer.push_back('\0');
        
        // Clear feature schedule
        m_featureScheduleText.str("");
        m_featureScheduleText.clear();
        m_featureScheduleBuffer.clear();
        m_featureScheduleBuffer.push_back('\0');
    }
    
    m_subsetResults.clear();
    m_statusMessage = "Running subset analysis...";
    
    // Launch analysis in background thread
    m_analysisFuture = std::async(std::launch::async, [this]() {
        try {
            // Set up logger callback to capture output and update buffer in real-time
            SimpleLogger::SetCallback([this](const std::string& msg) {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << msg << "\n";
                
                // Update the buffer immediately for real-time display
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            });
            
            const auto* df = m_dataSource->GetDataFrame();
            if (!df) {
                throw std::runtime_error("No data frame available");
            }
            
            int totalRows = df->num_rows();
            
            // Calculate subset positions with overlap
            std::vector<std::pair<int, int>> subsetRanges;
            
            if (m_overlapSize > 0 && m_subsetSize > m_overlapSize) {
                // Use overlapping subsets with fixed size
                int stride = m_subsetSize - m_overlapSize;
                int currentStart = 0;
                
                while (currentStart < totalRows) {
                    int endRow = std::min(currentStart + m_subsetSize, totalRows);
                    subsetRanges.push_back({currentStart, endRow});
                    
                    // Break if we've covered all data
                    if (endRow >= totalRows) break;
                    
                    currentStart += stride;
                }
            } else {
                // Fallback to non-overlapping subsets
                int rowsPerSubset = totalRows / m_numSubsets;
                for (int i = 0; i < m_numSubsets; i++) {
                    int startRow = i * rowsPerSubset;
                    int endRow = (i == m_numSubsets - 1) ? totalRows : (i + 1) * rowsPerSubset;
                    subsetRanges.push_back({startRow, endRow});
                }
            }
            
            // Get predictor names and target
            std::vector<std::string> predictorNames = m_featureSelector.GetSelectedFeatures();
            std::string targetName = m_featureSelector.GetSelectedTarget();
            
            // Prepare configuration
            EnhancedStepwise::StepwiseConfig config;
            config.n_kept = m_nKept;
            config.n_folds = m_nFolds;
            config.min_predictors = m_minPredictors;
            config.max_predictors = m_maxPredictors;
            config.mcpt_replications = m_mcptReplications;
            config.mcpt_type = (m_mcptType == 0) ? 
                EnhancedStepwise::StepwiseConfig::COMPLETE : 
                EnhancedStepwise::StepwiseConfig::CYCLIC;
            config.early_termination = m_earlyTermination;
            
            // Set cancellation callback
            config.cancel_callback = [this]() { return m_stopRequested.load(); };
            
            // Log subset analysis start (write directly to buffer)
            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << "=== SUBSET ANALYSIS ===\n";
                m_resultsText << "Total rows: " << totalRows << "\n";
                m_resultsText << "Number of subsets: " << subsetRanges.size() << "\n";
                if (m_overlapSize > 0 && m_subsetSize > m_overlapSize) {
                    m_resultsText << "Subset size: " << m_subsetSize << "\n";
                    m_resultsText << "Overlap size: " << m_overlapSize << "\n";
                } else {
                    m_resultsText << "Rows per subset: ~" << (totalRows / m_numSubsets) << "\n";
                }
                m_resultsText << "Target: " << targetName << "\n";
                m_resultsText << "Predictors: " << predictorNames.size() << "\n\n";
                
                // Update buffer immediately
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            }
            
            // Run analysis on each subset
            for (size_t subset = 0; subset < subsetRanges.size(); subset++) {
                // Check if stop was requested
                if (m_stopRequested) {
                    std::lock_guard<std::mutex> lock(m_bufferMutex);
                    m_resultsText << "\n=== ANALYSIS STOPPED BY USER ===\n";
                    m_resultsString = m_resultsText.str();
                    m_resultsBuffer.resize(m_resultsString.size() + 1024);
                    std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                    m_resultsBuffer[m_resultsString.size()] = '\0';
                    break;
                }
                int startRow = subsetRanges[subset].first;
                int endRow = subsetRanges[subset].second;
                
                // Write subset header to buffer
                {
                    std::lock_guard<std::mutex> lock(m_bufferMutex);
                    m_resultsText << "SUBSET " << (subset + 1) << " (rows " << startRow << "-" << endRow << "):\n";
                    m_resultsText << "----------------------------------------\n";
                    
                    // Update buffer immediately
                    m_resultsString = m_resultsText.str();
                    m_resultsBuffer.resize(m_resultsString.size() + 1024);
                    std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                    m_resultsBuffer[m_resultsString.size()] = '\0';
                }
                
                try {
                    // Create algorithm instance for this subset
                    EnhancedStepwise stepwise(config);
                    
                    // Prepare data for this subset
                    DataMatrix X;
                    std::vector<double> y;
                    PrepareDataForAnalysis(X, y, startRow, endRow);
                    
                    // Get the actual predictor names from the prepared matrix
                    std::vector<std::string> matrixPredictorNames = X.get_column_names();
                    
                    // Run stepwise selection
                    auto results = stepwise.run_on_data(X, y, matrixPredictorNames, targetName);
                    
                    // Store results
                    SubsetResult subsetResult;
                    subsetResult.subsetIndex = subset;
                    subsetResult.startRow = startRow;
                    subsetResult.endRow = endRow;
                    subsetResult.results = results;
                    m_subsetResults.push_back(subsetResult);
                    
                    // Add to feature schedule
                    {
                        std::lock_guard<std::mutex> lock(m_bufferMutex);
                        m_featureScheduleText << startRow << "-" << endRow << ": ";
                        for (size_t i = 0; i < results.selected_feature_names.size(); i++) {
                            if (i > 0) m_featureScheduleText << ", ";
                            m_featureScheduleText << results.selected_feature_names[i];
                        }
                        m_featureScheduleText << "\n";
                        
                        // Update feature schedule buffer
                        m_featureScheduleString = m_featureScheduleText.str();
                        m_featureScheduleBuffer.resize(m_featureScheduleString.size() + 1024);
                        std::copy(m_featureScheduleString.begin(), m_featureScheduleString.end(), m_featureScheduleBuffer.begin());
                        m_featureScheduleBuffer[m_featureScheduleString.size()] = '\0';
                    }
                    
                    // Format results for this subset (write to buffer)
                    {
                        std::lock_guard<std::mutex> lock(m_bufferMutex);
                        m_resultsText << "Selected Features (" << results.selected_feature_names.size() << "): ";
                        for (size_t i = 0; i < results.selected_feature_names.size(); i++) {
                            if (i > 0) m_resultsText << ", ";
                            m_resultsText << results.selected_feature_names[i];
                        }
                        m_resultsText << "\n";
                        m_resultsText << "R-squared: " << std::fixed << std::setprecision(4) 
                                     << results.final_r_square << "\n";
                        
                        if (results.terminated_early) {
                            m_resultsText << "Early termination: " << results.termination_reason << "\n";
                        }
                        
                        m_resultsText << "\n";
                        
                        // Update buffer immediately
                        m_resultsString = m_resultsText.str();
                        m_resultsBuffer.resize(m_resultsString.size() + 1024);
                        std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                        m_resultsBuffer[m_resultsString.size()] = '\0';
                    }
                    
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(m_bufferMutex);
                    m_resultsText << "ERROR in subset " << (subset + 1) << ": " << e.what() << "\n\n";
                    
                    // Update buffer immediately
                    m_resultsString = m_resultsText.str();
                    m_resultsBuffer.resize(m_resultsString.size() + 1024);
                    std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                    m_resultsBuffer[m_resultsString.size()] = '\0';
                }
            }
            
            // Summary of features across subsets (write to buffer)
            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << "\n=== FEATURE FREQUENCY ACROSS SUBSETS ===\n";
                std::map<std::string, int> featureFrequency;
                for (const auto& subsetResult : m_subsetResults) {
                    for (const auto& feature : subsetResult.results.selected_feature_names) {
                        featureFrequency[feature]++;
                    }
                }
                
                // Sort by frequency
                std::vector<std::pair<std::string, int>> sortedFeatures(
                    featureFrequency.begin(), featureFrequency.end());
                std::sort(sortedFeatures.begin(), sortedFeatures.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
                
                for (const auto& [feature, count] : sortedFeatures) {
                    m_resultsText << feature << ": " << count << "/" << m_numSubsets 
                                 << " (" << std::fixed << std::setprecision(1) 
                                 << (100.0 * count / m_numSubsets) << "%)\n";
                }
                
                // Update buffer immediately
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            }
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_resultsText << "\nERROR: " << e.what() << "\n";
            
            // Update buffer immediately
            m_resultsString = m_resultsText.str();
            m_resultsBuffer.resize(m_resultsString.size() + 1024);
            std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
            m_resultsBuffer[m_resultsString.size()] = '\0';
        }
        
        // Clear logger callback
        SimpleLogger::ClearCallback();
    });
}

void ESSWindow::ClearResults() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_hasResults = false;
    m_resultsText.str("");
    m_resultsText.clear();
    m_resultsString.clear();
    m_resultsBuffer.clear();
    m_resultsBuffer.push_back('\0');  // Ensure buffer has null terminator
    m_subsetResults.clear();
    m_statusMessage = "Results cleared";
}
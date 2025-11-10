#include "ESSWindow.h"
#include "TimeSeriesWindow.h"
#include <algorithm>
#include <future>
#include <chrono>
#include <iomanip>
#include "simple_logger.h"

ESSWindow::ESSWindow()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_nKept(5)
    , m_nFolds(4)
    , m_minPredictors(1)
    , m_maxPredictors(7)
    , m_mcptReplications(100)
    , m_mcptType(0)  // COMPLETE
    , m_earlyTermination(true)
    , m_useSubsets(false)
    , m_numSubsets(5)
    , m_globalStandardization(false)  // Default to per-subset like console version
    , m_isRunning(false)
    , m_stopRequested(false)
    , m_hasResults(false)
    , m_autoScroll(true)
{
    m_availableColumns.clear();
    
    // Initialize feature selector
    m_featureSelector.SetTargetPrefix("TGT");
    m_featureSelector.SetShowOnlyTargetsWithPrefix(true);
}

void ESSWindow::Draw() {
    if (!m_isVisible) return;
    
    ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Enhanced Stepwise Selection", &m_isVisible)) {
        // Check if async operation has completed
        if (m_isRunning && m_analysisFuture.valid() &&
            m_analysisFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            m_analysisFuture.get();
            m_isRunning = false;
        }
        
        // Main layout
        DrawColumnSelection();
        ImGui::Separator();
        
        DrawConfigurationSettings();
        ImGui::Separator();
        
        DrawRunControls();
        ImGui::Separator();
        
        // Results area
        float availableHeight = ImGui::GetContentRegionAvail().y - STATUS_BAR_HEIGHT;
        ImGui::BeginChild("ResultsArea", ImVec2(0, availableHeight), true);
        DrawResultsOutput();
        ImGui::EndChild();
        
        DrawStatusBar();
    }
    ImGui::End();
}

void ESSWindow::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void ESSWindow::UpdateColumnList() {
    if (!m_dataSource || !m_dataSource->HasData()) {
        m_availableColumns.clear();
        return;
    }
    
    const auto* df = m_dataSource->GetDataFrame();
    if (!df) {
        m_availableColumns.clear();
        return;
    }
    
    // Get all column names
    m_availableColumns = df->column_names();
    
    // Update feature selector
    m_featureSelector.SetAvailableFeatures(m_availableColumns);
}

void ESSWindow::DrawColumnSelection() {
    ImGui::Text("Feature Selection:");
    
    // Check data availability
    if (!m_dataSource || !m_dataSource->HasData()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No data loaded. Please load data in Time Series Window first.");
        return;
    }
    
    if (m_availableColumns.empty()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No columns available.");
        return;
    }
    
    // Use the feature selector widget
    m_featureSelector.Draw();
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
    ImGui::SliderInt("Max Predictors", &m_maxPredictors, 1, 20, "%d");
    
    // Ensure min <= max
    if (m_minPredictors > m_maxPredictors) {
        m_minPredictors = m_maxPredictors;
    }
    
    ImGui::NextColumn();
    
    // Second column - MCPT settings
    ImGui::Text("Monte Carlo Permutation Test:");
    ImGui::SliderInt("Replications", &m_mcptReplications, 1, 1000, "%d");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Number of permutation test replications (higher = more accurate p-values)");
    }
    
    ImGui::RadioButton("Complete", &m_mcptType, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Cyclic", &m_mcptType, 1);
    
    ImGui::Checkbox("Early Termination", &m_earlyTermination);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Stop when adding features no longer improves performance");
    }
    
    ImGui::NextColumn();
    
    // Third column - Subset settings
    ImGui::Text("Data Subset Analysis:");
    ImGui::Checkbox("Use Subsets", &m_useSubsets);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Split data into subsets and run analysis on each");
    }
    
    if (m_useSubsets) {
        ImGui::SliderInt("Number of Subsets", &m_numSubsets, 2, 20, "%d");
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
    // Always show the buffer if it has content (even during analysis)
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (m_resultsBuffer.size() > 1) {  // > 1 because we always have null terminator
        // Display results in a scrollable text area (using safe buffer)
        ImGui::InputTextMultiline("##Results", 
                                  m_resultsBuffer.data(), 
                                  m_resultsBuffer.size(),
                                  ImVec2(-1, -1),
                                  ImGuiInputTextFlags_ReadOnly);
        
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    } else if (!m_hasResults) {
        ImGui::TextWrapped("Results will appear here after running the analysis...");
    }
}

void ESSWindow::DrawStatusBar() {
    if (m_isRunning) {
        ImGui::Text("Status: %s", m_statusMessage.c_str());
    } else if (m_hasResults) {
        ImGui::Text("Status: Analysis complete");
    } else {
        ImGui::Text("Status: Ready");
    }
}

void ESSWindow::PrepareDataForAnalysis(DataMatrix& X, std::vector<double>& y, 
                                       int startRow, int endRow) {
    if (!m_dataSource || !m_dataSource->HasData()) {
        throw std::runtime_error("No data source available");
    }
    
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
                throw std::runtime_error("NaN or Inf detected in predictor after standardization");
            }
        }
    }
    
    for (double val : y) {
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
                // Check for stop request
                if (m_stopRequested) {
                    throw std::runtime_error("Analysis stopped by user");
                }
                
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << msg << "\n";
                
                // Update the buffer immediately for real-time display
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            });
            
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
                             
                // Final buffer update
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            }
            
            SimpleLogger::SetCallback(nullptr);
            m_statusMessage = "Analysis complete";
            m_hasResults = true;
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_resultsText << "\nERROR: " << e.what() << "\n";
            m_resultsString = m_resultsText.str();
            m_resultsBuffer.resize(m_resultsString.size() + 1);
            std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
            m_resultsBuffer[m_resultsString.size()] = '\0';
            
            SimpleLogger::SetCallback(nullptr);
            m_statusMessage = "Analysis failed";
            m_hasResults = false;
        }
        
        m_isRunning = false;
        m_stopRequested = false;
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
    }
    
    m_subsetResults.clear();
    m_statusMessage = "Running subset analysis...";
    
    // Launch analysis in background thread
    m_analysisFuture = std::async(std::launch::async, [this]() {
        try {
            // Set up logger callback to capture output and update buffer in real-time
            SimpleLogger::SetCallback([this](const std::string& msg) {
                // Check for stop request
                if (m_stopRequested) {
                    throw std::runtime_error("Analysis stopped by user");
                }
                
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
            int rowsPerSubset = totalRows / m_numSubsets;
            
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
            
            // Log subset analysis start (write directly to buffer)
            {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << "=== SUBSET ANALYSIS ===\n";
                m_resultsText << "Total rows: " << totalRows << "\n";
                m_resultsText << "Number of subsets: " << m_numSubsets << "\n";
                m_resultsText << "Rows per subset: ~" << rowsPerSubset << "\n";
                m_resultsText << "Target: " << targetName << "\n";
                m_resultsText << "Predictors: " << predictorNames.size() << "\n\n";
                
                // Update buffer immediately
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            }
            
            // Run analysis on each subset
            for (int subset = 0; subset < m_numSubsets; subset++) {
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
                int startRow = subset * rowsPerSubset;
                int endRow = (subset == m_numSubsets - 1) ? totalRows : (subset + 1) * rowsPerSubset;
                
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
                    m_resultsText << "Subset " << (subset + 1) << " failed: " << e.what() << "\n\n";
                    m_resultsString = m_resultsText.str();
                    m_resultsBuffer.resize(m_resultsString.size() + 1024);
                    std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                    m_resultsBuffer[m_resultsString.size()] = '\0';
                }
            }
            
            // Summary statistics
            if (!m_subsetResults.empty() && !m_stopRequested) {
                std::lock_guard<std::mutex> lock(m_bufferMutex);
                m_resultsText << "\n=== SUMMARY ===\n";
                m_resultsText << "Completed " << m_subsetResults.size() << " subsets\n";
                
                // Calculate average R-squared
                double totalRSquare = 0.0;
                for (const auto& result : m_subsetResults) {
                    totalRSquare += result.results.final_r_square;
                }
                double avgRSquare = totalRSquare / m_subsetResults.size();
                
                m_resultsText << "Average R-squared: " << std::fixed << std::setprecision(4) 
                             << avgRSquare << "\n";
                
                m_resultsString = m_resultsText.str();
                m_resultsBuffer.resize(m_resultsString.size() + 1024);
                std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
                m_resultsBuffer[m_resultsString.size()] = '\0';
            }
            
            SimpleLogger::SetCallback(nullptr);
            m_statusMessage = "Subset analysis complete";
            m_hasResults = true;
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_resultsText << "\nERROR: " << e.what() << "\n";
            m_resultsString = m_resultsText.str();
            m_resultsBuffer.resize(m_resultsString.size() + 1);
            std::copy(m_resultsString.begin(), m_resultsString.end(), m_resultsBuffer.begin());
            m_resultsBuffer[m_resultsString.size()] = '\0';
            
            SimpleLogger::SetCallback(nullptr);
            m_statusMessage = "Subset analysis failed";
            m_hasResults = false;
        }
        
        m_isRunning = false;
        m_stopRequested = false;
    });
}

void ESSWindow::ClearResults() {
    m_hasResults = false;
    m_resultsText.str("");
    m_resultsText.clear();
    m_resultsString.clear();
    m_resultsBuffer.clear();
    m_resultsBuffer.push_back('\0');  // Ensure buffer has null terminator
    m_subsetResults.clear();
    m_statusMessage = "Results cleared";
}
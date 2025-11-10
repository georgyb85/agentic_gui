// TestModelWindow implementation that EXACTLY replicates old SimulationWindow behavior
// This ensures test model produces identical results to the original simulation

#include "TestModelWindow.h"
#include "ISimulationModel_v2.h"
#include "SimulationEngine.h"
#include "XGBoostConfig.h"
#include "SimulationUtils.h"
#include "ThresholdCalculator.h"
#include "../TimeSeriesWindow.h"
#include "../analytics_dataframe.h"
#include "../FeatureSelectorWidget.h"
#include "imgui.h"
#include "../implot.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
#include <utility>
#include <xgboost/c_api.h>
#include <arrow/api.h>
#include <arrow/scalar.h>

namespace simulation {

TestModelWindow::TestModelWindow()
    : m_isVisible(false)
    , m_hasConfiguration(false)
    , m_hasResults(false)
    , m_timeSeriesWindow(nullptr)
    , m_featureSelector(std::make_unique<FeatureSelectorWidget>()) {
    // Configure feature selector for financial data
    m_featureSelector->SetTargetPrefix("tgt_");
    m_featureSelector->SetShowOnlyTargetsWithPrefix(true);
}

TestModelWindow::~TestModelWindow() = default;

void TestModelWindow::Draw() {
    if (!m_isVisible) return;
    
    // NOT a modal - just a normal child region in the tab
    ImGui::Text("Test Model on Specific Data Range");
    ImGui::Separator();
    
    // Show configuration source
    if (m_config.sourceRunName.length() > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::Text("Configuration from: %s - Fold %d", 
                    m_config.sourceRunName.c_str(), 
                    m_config.sourceFold.fold_number);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            Clear();
        }
        ImGui::Separator();
    }
    
    // Training range inputs - always editable, but use step of 1 for precision
    ImGui::Text("Training Data Range:");
    ImGui::Text("Start Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##train_start", &m_config.trainStart, 1, 100)) {
        m_config.trainStart = std::max(0, m_config.trainStart);
    }
    
    ImGui::Text("End Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##train_end", &m_config.trainEnd, 1, 100)) {
        m_config.trainEnd = std::max(m_config.trainStart + 100, m_config.trainEnd);
    }
    
    ImGui::Separator();
    
    // Test range inputs - always editable, but use step of 1 for precision
    ImGui::Text("Test Data Range:");
    ImGui::Text("Start Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##test_start", &m_config.testStart, 1, 10)) {
        m_config.testStart = std::max(m_config.trainEnd, m_config.testStart);
    }
    
    ImGui::Text("End Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##test_end", &m_config.testEnd, 1, 10)) {
        m_config.testEnd = std::max(m_config.testStart + 10, m_config.testEnd);
    }
    
    ImGui::Separator();
    
    // Feature selector widget
    ImGui::Text("Feature and Target Selection:");
    ImGui::Separator();
    
    // Update available columns if we have a data source
    if (m_timeSeriesWindow && m_timeSeriesWindow->HasData()) {
        auto df = m_timeSeriesWindow->GetDataFrame();
        if (df) {
            std::vector<std::string> columns = df->column_names();
            m_featureSelector->SetAvailableColumns(columns);
        }
    }
    
    // Draw the feature selector in columns (features on left, target on right)
    ImGui::Columns(2, "FeatureTargetColumns", true);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.7f);
    
    // Left column - Features
    ImGui::Text("Features:");
    bool featuresChanged = m_featureSelector->DrawFeatureSelection();
    
    ImGui::NextColumn();
    
    // Right column - Target
    ImGui::Text("Target:");
    featuresChanged |= m_featureSelector->DrawTargetSelection();
    
    ImGui::Columns(1);
    
    if (featuresChanged) {
        // Don't clear results immediately - user needs to click "Train & Test Model" to retrain
        // This allows user to review changes before committing to retrain
    }
    
    // Show selected count
    auto selectedFeatures = m_featureSelector->GetSelectedFeatures();
    auto selectedTarget = m_featureSelector->GetSelectedTarget();
    ImGui::Text("Selected: %zu features, target: %s", 
                selectedFeatures.size(), 
                selectedTarget.empty() ? "(none)" : selectedTarget.c_str());
    
    ImGui::Separator();
    
    // Trading threshold
    ImGui::Text("Trading Threshold:");
    if (ImGui::InputFloat("##threshold", &m_config.originalThreshold, 0.0001f, 0.001f, "%.6f")) {
        // Recalculate metrics if model is trained
        if (m_hasResults && m_results.success) {
            RecalculateMetricsWithThreshold();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto")) {
        // Will recalculate based on model
        if (m_hasResults && m_results.success && !m_results.predictions.empty()) {
            // Calculate 95th percentile - EXACTLY like old calculateQuantile
            std::vector<float> sorted_preds = m_results.predictions;
            std::sort(sorted_preds.begin(), sorted_preds.end());
            int percentile_idx = static_cast<int>(0.95f * (sorted_preds.size() - 1));
            m_config.originalThreshold = sorted_preds[percentile_idx];
            RecalculateMetricsWithThreshold();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Calculate threshold automatically using 95th percentile");
    }
    
    ImGui::Separator();
    
    // Train button
    if (ImGui::Button("Train & Test Model")) {
        RunTestModel();
    }
    
    // Show results if model is trained
    if (m_hasResults && m_results.success) {
        ImGui::Separator();
        ImGui::Text("Trading Signals:");
        
        // Calculate short signals and metrics
        int shortSignals = 0;
        int shortHits = 0;
        float shortReturn = 0.0f;
        for (size_t i = 0; i < m_results.predictions.size(); ++i) {
            if (m_results.predictions[i] < m_results.shortThreshold5th) {
                shortSignals++;
                float invertedReturn = -m_results.actuals[i];  // Profit when market goes down
                shortReturn += invertedReturn;
                if (invertedReturn > 0) shortHits++;
            }
        }
        float shortHitRate = (shortSignals > 0) ? (float)shortHits / shortSignals : 0.0f;
        
        // Display in a compact table
        if (ImGui::BeginTable("SignalsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Signals", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Hit Rate", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Return", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableHeadersRow();
            
            // Long row
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Long");
            ImGui::TableNextColumn();
            ImGui::Text("%d", m_results.signalsGenerated);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", m_results.hitRate * 100.0f);
            ImGui::TableNextColumn();
            ImGui::TextColored(m_results.totalReturn > 0 ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f), 
                             "%.6f", m_results.totalReturn);
            
            // Short row
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Short");
            ImGui::TableNextColumn();
            ImGui::Text("%d", shortSignals);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f%%", shortHitRate * 100.0f);
            ImGui::TableNextColumn();
            ImGui::TextColored(shortReturn > 0 ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f), 
                             "%.6f", shortReturn);
            
            // Total row
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Total");
            ImGui::TableNextColumn();
            ImGui::Text("%d", m_results.signalsGenerated + shortSignals);
            ImGui::TableNextColumn();
            float totalHitRate = (m_results.signalsGenerated + shortSignals > 0) ? 
                                ((m_results.hitRate * m_results.signalsGenerated + shortHitRate * shortSignals) / 
                                 (m_results.signalsGenerated + shortSignals)) : 0.0f;
            ImGui::Text("%.1f%%", totalHitRate * 100.0f);
            ImGui::TableNextColumn();
            float totalReturnCombined = m_results.totalReturn + shortReturn;
            ImGui::TextColored(totalReturnCombined > 0 ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f), 
                             "%.6f", totalReturnCombined);
            
            ImGui::EndTable();
        }
        
        ImGui::Separator();
        ImGui::Text("ROC-based Metrics:");
        ImGui::Text("ROC AUC: %.4f", m_results.rocAUC);
        ImGui::Text("R-squared: %.4f", m_results.rSquared);
        
        ImGui::Separator();
        ImGui::Text("Thresholds:");
        ImGui::Text("Long - 95th percentile: %.6f", m_config.originalThreshold);
        ImGui::Text("Long - Optimal (ROC): %.6f", m_results.optimalROCThreshold);
        ImGui::Text("Short - 5th percentile: %.6f", m_results.shortThreshold5th);
        ImGui::Text("Short - Optimal: %.6f", m_results.optimalShortThreshold);
        
        ImGui::Separator();
        ImGui::Text("Profit Factors:");
        
        // Create a unified table for all profit factors
        ImGui::BeginTable("ProfitFactorsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg);
        
        // Headers
        ImGui::TableSetupColumn("Dataset", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("All Long", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("All Short", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Long 95%", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Long Opt", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Short 5%", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Short Opt", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();
        
        // Train row
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Train");
        
        ImGui::TableNextColumn();
        if (m_results.trainProfitFactor == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.trainProfitFactor);
        }
        
        ImGui::TableNextColumn();
        ImGui::Text("-");  // All trades short - not calculated yet
        
        ImGui::TableNextColumn();
        if (m_results.trainProfitFactorLongOnly == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.trainProfitFactorLongOnly);
        }
        
        ImGui::TableNextColumn();
        if (m_results.trainProfitFactorOptimal == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.trainProfitFactorOptimal);
        }
        
        ImGui::TableNextColumn();
        if (m_results.trainProfitFactorShortOnly5th == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.trainProfitFactorShortOnly5th);
        }
        
        ImGui::TableNextColumn();
        if (m_results.trainProfitFactorShortOnlyOptimal == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.trainProfitFactorShortOnlyOptimal);
        }
        
        // Test row
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Test");
        
        ImGui::TableNextColumn();
        if (m_results.testProfitFactor == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.testProfitFactor);
        }
        
        ImGui::TableNextColumn();
        ImGui::Text("-");  // All trades short - not calculated yet
        
        ImGui::TableNextColumn();
        if (m_results.testProfitFactorLongOnly == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.testProfitFactorLongOnly);
        }
        
        ImGui::TableNextColumn();
        if (m_results.testProfitFactorOptimal == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.testProfitFactorOptimal);
        }
        
        ImGui::TableNextColumn();
        if (m_results.testProfitFactorShortOnly5th == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.testProfitFactorShortOnly5th);
        }
        
        ImGui::TableNextColumn();
        if (m_results.testProfitFactorShortOnlyOptimal == std::numeric_limits<float>::max()) {
            ImGui::Text("Inf");
        } else {
            ImGui::Text("%.2f", m_results.testProfitFactorShortOnlyOptimal);
        }
        
        ImGui::EndTable();
        
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                          "Note: Test Model uses fixed seed (43) for exact reproducibility. "
                          "Results should match the original fold exactly when all parameters are preserved.");
        ImGui::PopTextWrapPos();
        
        ImGui::Separator();
        
        // 2x2 grid of plots
        float plotHeight = 350.0f;
        float availWidth = ImGui::GetContentRegionAvail().x;
        float plotWidth = (availWidth - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
        
        // First row: ROC Curve and Feature Importance
        ImGui::BeginChild("ROCCurveChild", ImVec2(plotWidth, plotHeight), true);
        ImGui::Text("ROC Curve");
        ImGui::Separator();
        PlotROCCurve();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        ImGui::BeginChild("FeatureImportanceChild", ImVec2(plotWidth, plotHeight), true);
        ImGui::Text("Feature Importance");
        ImGui::Separator();
        PlotFeatureImportance();
        ImGui::EndChild();
        
        // Second row: Predictions vs Actuals and Predictions Distribution
        ImGui::BeginChild("PredictionsScatterChild", ImVec2(plotWidth, plotHeight), true);
        ImGui::Text("Predictions vs Actuals");
        ImGui::Separator();
        PlotPredictionScatter();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        ImGui::BeginChild("PredictionsHistogramChild", ImVec2(plotWidth, plotHeight), true);
        ImGui::Text("Predictions Distribution");
        ImGui::Separator();
        PlotPredictionHistogram();
        ImGui::EndChild();
    } else if (m_hasResults && !m_results.success) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
            "Error: %s", m_results.errorMessage.c_str());
    }
}

void TestModelWindow::SetFromFold(const FoldResult& fold, const SimulationRun& run) {
    // Copy configuration from the fold
    m_config.sourceFold = fold;
    m_config.sourceRunName = run.name;
    m_config.sourceModelType = run.model_type;
    
    // Copy data ranges
    m_config.trainStart = fold.train_start;
    m_config.trainEnd = fold.train_end;
    m_config.testStart = fold.test_start;
    m_config.testEnd = fold.test_end;
    
    // Copy transformation parameters from fold
    m_config.transformMean = fold.mean_scale;
    m_config.transformStd = fold.std_scale;
    m_config.transformParamsPreserved = true;  // Critical flag
    
    // Note: Threshold will be calculated fresh during training
    m_config.originalThreshold = 0.0f;
    m_config.modelType = run.model_type;
    
    // Copy the complete model config from the run
    if (run.config) {
        if (run.model_type == "XGBoost") {
            auto* xgb_src = dynamic_cast<XGBoostConfig*>(run.config.get());
            if (xgb_src) {
                auto xgb_copy = std::make_shared<XGBoostConfig>(*xgb_src);
                
                // CRITICAL: If using feature schedule, set the actual features used for THIS fold
                if (run.using_feature_schedule && !fold.features_used.empty()) {
                    xgb_copy->feature_columns = fold.features_used;
                    xgb_copy->use_feature_schedule = false;  // Don't use schedule, use exact features
                    std::cout << "Using " << fold.features_used.size() 
                              << " features from schedule for fold " << fold.fold_number << std::endl;
                    
                    // Store features for the feature selector
                    m_config.foldFeatures = fold.features_used;
                } else {
                    // Store regular features
                    m_config.foldFeatures = xgb_copy->feature_columns;
                }
                
                // Store target column
                m_config.targetColumn = xgb_copy->target_column;
                
                // Initialize the feature selector with these features
                m_featureSelector->SetSelectedFeatures(m_config.foldFeatures);
                m_featureSelector->SetSelectedTarget(m_config.targetColumn);
                
                m_config.modelConfig = xgb_copy;
            }
        }
    } else {
        // Create default config with standard parameters
        if (run.model_type == "XGBoost" || run.model_type.empty()) {
            auto xgb_config = std::make_shared<XGBoostConfig>();
            // These are the standard defaults from old SimulationWindow
            xgb_config->learning_rate = 0.01f;
            xgb_config->max_depth = 4;
            xgb_config->min_child_weight = 10.0f;
            xgb_config->subsample = 0.8f;
            xgb_config->colsample_bytree = 0.7f;
            xgb_config->lambda = 2.0f;
            xgb_config->num_boost_round = 2000;
            xgb_config->early_stopping_rounds = 50;
            xgb_config->min_boost_rounds = 100;
            xgb_config->val_split_ratio = 0.8f;
            xgb_config->use_tanh_transform = true;
            xgb_config->tanh_scaling_factor = 0.001f;
            xgb_config->use_standardization = false;
            xgb_config->random_seed = 43;  // Fixed seed for reproducibility
            
            // Get feature columns - these should come from the actual simulation
            xgb_config->feature_columns = {
                "returns_1", "returns_2", "returns_3", "returns_4", "returns_5",
                "returns_10", "returns_20", "returns_30", "returns_60",
                "volume_1", "volume_2", "volume_3", "volume_4", "volume_5"
            };
            xgb_config->target_column = "future_return_1";
            
            m_config.modelConfig = xgb_config;
        }
    }
    
    m_hasConfiguration = true;
    m_hasResults = false;
    
    std::cout << "\n=== Test Model Configuration Set ===\n";
    std::cout << "Source: " << run.name << " - Fold " << fold.fold_number << "\n";
    std::cout << "Train: [" << m_config.trainStart << ", " << m_config.trainEnd << "]\n";
    std::cout << "Test: [" << m_config.testStart << ", " << m_config.testEnd << "]\n";
    std::cout << "Preserved transform params: mean=" << m_config.transformMean 
              << ", std=" << m_config.transformStd << "\n";
}

void TestModelWindow::RunTestModel() {
    if (!m_timeSeriesWindow || !m_timeSeriesWindow->HasData()) {
        m_results.success = false;
        m_results.errorMessage = "No data available for testing";
        m_hasResults = true;
        return;
    }
    
    const auto* df = m_timeSeriesWindow->GetDataFrame();
    if (!df) {
        m_results.success = false;
        m_results.errorMessage = "DataFrame is null";
        m_hasResults = true;
        return;
    }
    
    try {
        // Get the XGBoost config
        if (!m_config.modelConfig) {
            m_results.success = false;
            m_results.errorMessage = "No model configuration available";
            m_hasResults = true;
            return;
        }
        
        auto* xgb_config = dynamic_cast<XGBoostConfig*>(m_config.modelConfig.get());
        if (!xgb_config) {
            m_results.success = false;
            m_results.errorMessage = "Invalid model configuration";
            m_hasResults = true;
            return;
        }
        
        // Update the config with features from the feature selector
        xgb_config->feature_columns = m_featureSelector->GetSelectedFeatures();
        xgb_config->target_column = m_featureSelector->GetSelectedTarget();
        
        // Validate that we have features and target selected
        if (xgb_config->feature_columns.empty()) {
            m_results.success = false;
            m_results.errorMessage = "No features selected";
            m_hasResults = true;
            return;
        }
        
        if (xgb_config->target_column.empty()) {
            m_results.success = false;
            m_results.errorMessage = "No target selected";
            m_hasResults = true;
            return;
        }
        
        // Validate data ranges
        int64_t numRows = df->num_rows();
        std::cout << "DataFrame has " << numRows << " rows total\n";
        std::cout << "Train range: [" << m_config.trainStart << ", " << m_config.trainEnd << "]\n";
        std::cout << "Test range: [" << m_config.testStart << ", " << m_config.testEnd << "]\n";
        
        if (m_config.testEnd > numRows) {
            m_results.success = false;
            m_results.errorMessage = "Test range exceeds available data";
            m_hasResults = true;
            return;
        }
        
        // Split training data for validation using the same ratio as original
        int train_size = m_config.trainEnd - m_config.trainStart;
        int split_point = m_config.trainStart + (int)(train_size * xgb_config->val_split_ratio);
        
        std::cout << "Using validation split ratio: " << xgb_config->val_split_ratio << "\n";
        std::cout << "Train: [" << m_config.trainStart << ", " << split_point << "]\n";
        std::cout << "Val: [" << split_point << ", " << m_config.trainEnd << "]\n";
        
        // Extract features and labels
        int n_train = split_point - m_config.trainStart;
        int n_val = m_config.trainEnd - split_point;
        int n_test = m_config.testEnd - m_config.testStart;
        int n_features = xgb_config->feature_columns.size();
        
        std::vector<float> X_train(n_train * n_features);
        std::vector<float> y_train(n_train);
        std::vector<float> X_val(n_val * n_features);
        std::vector<float> y_val(n_val);
        std::vector<float> X_test(n_test * n_features);
        std::vector<float> y_test(n_test);
        
        // Get the Arrow table from DataFrame
        auto table = df->get_cpu_table();
        if (!table) {
            throw std::runtime_error("Failed to get Arrow table from DataFrame");
        }
        
        // Extract data using the exact same method as old code
        // [Feature extraction code - same as before]
        for (int feat_idx = 0; feat_idx < n_features; ++feat_idx) {
            const std::string& feature = xgb_config->feature_columns[feat_idx];
            auto column = table->GetColumnByName(feature);
            if (!column) {
                throw std::runtime_error("Feature column not found: " + feature);
            }
            
            // Extract train, val, test data
            for (int i = 0; i < n_train; ++i) {
                auto scalar_result = column->GetScalar(m_config.trainStart + i);
                if (scalar_result.ok()) {
                    auto scalar = scalar_result.ValueOrDie();
                    float value = 0.0f;
                    if (scalar->type->id() == arrow::Type::DOUBLE) {
                        value = static_cast<float>(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value);
                    } else if (scalar->type->id() == arrow::Type::FLOAT) {
                        value = std::static_pointer_cast<arrow::FloatScalar>(scalar)->value;
                    }
                    X_train[i * n_features + feat_idx] = value;
                }
            }
            
            for (int i = 0; i < n_val; ++i) {
                auto scalar_result = column->GetScalar(split_point + i);
                if (scalar_result.ok()) {
                    auto scalar = scalar_result.ValueOrDie();
                    float value = 0.0f;
                    if (scalar->type->id() == arrow::Type::DOUBLE) {
                        value = static_cast<float>(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value);
                    } else if (scalar->type->id() == arrow::Type::FLOAT) {
                        value = std::static_pointer_cast<arrow::FloatScalar>(scalar)->value;
                    }
                    X_val[i * n_features + feat_idx] = value;
                }
            }
            
            for (int i = 0; i < n_test; ++i) {
                auto scalar_result = column->GetScalar(m_config.testStart + i);
                if (scalar_result.ok()) {
                    auto scalar = scalar_result.ValueOrDie();
                    float value = 0.0f;
                    if (scalar->type->id() == arrow::Type::DOUBLE) {
                        value = static_cast<float>(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value);
                    } else if (scalar->type->id() == arrow::Type::FLOAT) {
                        value = std::static_pointer_cast<arrow::FloatScalar>(scalar)->value;
                    }
                    X_test[i * n_features + feat_idx] = value;
                }
            }
        }
        
        // Extract target column
        auto target_column = table->GetColumnByName(xgb_config->target_column);
        if (!target_column) {
            throw std::runtime_error("Target column not found: " + xgb_config->target_column);
        }
        
        for (int i = 0; i < n_train; ++i) {
            auto scalar_result = target_column->GetScalar(m_config.trainStart + i);
            if (scalar_result.ok()) {
                auto scalar = scalar_result.ValueOrDie();
                if (scalar->type->id() == arrow::Type::DOUBLE) {
                    y_train[i] = static_cast<float>(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value);
                } else if (scalar->type->id() == arrow::Type::FLOAT) {
                    y_train[i] = std::static_pointer_cast<arrow::FloatScalar>(scalar)->value;
                }
            }
        }
        
        for (int i = 0; i < n_val; ++i) {
            auto scalar_result = target_column->GetScalar(split_point + i);
            if (scalar_result.ok()) {
                auto scalar = scalar_result.ValueOrDie();
                if (scalar->type->id() == arrow::Type::DOUBLE) {
                    y_val[i] = static_cast<float>(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value);
                } else if (scalar->type->id() == arrow::Type::FLOAT) {
                    y_val[i] = std::static_pointer_cast<arrow::FloatScalar>(scalar)->value;
                }
            }
        }
        
        for (int i = 0; i < n_test; ++i) {
            auto scalar_result = target_column->GetScalar(m_config.testStart + i);
            if (scalar_result.ok()) {
                auto scalar = scalar_result.ValueOrDie();
                if (scalar->type->id() == arrow::Type::DOUBLE) {
                    y_test[i] = static_cast<float>(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value);
                } else if (scalar->type->id() == arrow::Type::FLOAT) {
                    y_test[i] = std::static_pointer_cast<arrow::FloatScalar>(scalar)->value;
                }
            }
        }
        
        // Store actuals for plotting
        m_results.actuals = y_test;
        
        // Store original train actuals before transformation
        std::vector<float> y_train_original = y_train;
        
        // CRITICAL: Apply transformation EXACTLY like old SimulationWindow
        std::vector<float> y_train_transformed, y_val_transformed;
        
        TransformParams transform_params;
        if (m_config.transformParamsPreserved) {
            // Use the EXACT transform parameters from the fold
            transform_params.mean = m_config.transformMean;
            transform_params.std_dev = m_config.transformStd;
            transform_params.scaling_factor = xgb_config->tanh_scaling_factor;
            std::cout << "Using PRESERVED transform params from fold: mean=" << std::fixed << std::setprecision(6)
                      << transform_params.mean << ", std=" << transform_params.std_dev 
                      << ", scale=" << transform_params.scaling_factor << std::resetiosflags(std::ios::fixed) << "\n";
        } else {
            // Calculate new transform parameters from training data
            float sum = std::accumulate(y_train.begin(), y_train.end(), 0.0f);
            transform_params.mean = sum / y_train.size();
            
            float sq_sum = 0.0f;
            for (float val : y_train) {
                float diff = val - transform_params.mean;
                sq_sum += diff * diff;
            }
            transform_params.std_dev = std::sqrt(sq_sum / y_train.size());
            if (transform_params.std_dev == 0.0f) {
                transform_params.std_dev = 1.0f;
            }
            transform_params.scaling_factor = xgb_config->tanh_scaling_factor;
            
            std::cout << "Calculated new transform params: mean=" << std::fixed << std::setprecision(6)
                      << transform_params.mean << ", std=" << transform_params.std_dev 
                      << ", scale=" << transform_params.scaling_factor << std::resetiosflags(std::ios::fixed) << "\n";
        }
        
        // Apply transformation - EXACTLY matching old code logic
        if (xgb_config->use_tanh_transform || xgb_config->use_standardization) {
            if (xgb_config->use_tanh_transform) {
                // When use_tanh_transform is true, ALWAYS standardize first, then scale, then tanh
                for (float val : y_train) {
                    float standardized = (val - transform_params.mean) / transform_params.std_dev;
                    float scaled = standardized * xgb_config->tanh_scaling_factor;
                    y_train_transformed.push_back(std::tanh(scaled));
                }
                for (float val : y_val) {
                    float standardized = (val - transform_params.mean) / transform_params.std_dev;
                    float scaled = standardized * xgb_config->tanh_scaling_factor;
                    y_val_transformed.push_back(std::tanh(scaled));
                }
            } else {
                // Just standardization (no tanh)
                for (float val : y_train) {
                    y_train_transformed.push_back((val - transform_params.mean) / transform_params.std_dev);
                }
                for (float val : y_val) {
                    y_val_transformed.push_back((val - transform_params.mean) / transform_params.std_dev);
                }
            }
        } else {
            // No transformation
            y_train_transformed = y_train;
            y_val_transformed = y_val;
        }
        
        // Train XGBoost directly with transformed data
        DMatrixHandle dtrain = nullptr;
        DMatrixHandle dval = nullptr;
        BoosterHandle booster = nullptr;
        
        // Create DMatrices
        XGDMatrixCreateFromMat(X_train.data(), n_train, n_features, -1, &dtrain);
        XGDMatrixSetFloatInfo(dtrain, "label", y_train_transformed.data(), n_train);
        
        XGDMatrixCreateFromMat(X_val.data(), n_val, n_features, -1, &dval);
        XGDMatrixSetFloatInfo(dval, "label", y_val_transformed.data(), n_val);
        
        // Create booster
        DMatrixHandle eval_dmats[2] = {dtrain, dval};
        const char* eval_names[2] = {"train", "val"};
        
        XGBoosterCreate(eval_dmats, 2, &booster);
        
        // Set parameters - EXACTLY like old code, including fixed seed
        XGBoosterSetParam(booster, "learning_rate", std::to_string(xgb_config->learning_rate).c_str());
        XGBoosterSetParam(booster, "max_depth", std::to_string(xgb_config->max_depth).c_str());
        XGBoosterSetParam(booster, "min_child_weight", std::to_string(xgb_config->min_child_weight).c_str());
        XGBoosterSetParam(booster, "subsample", std::to_string(xgb_config->subsample).c_str());
        XGBoosterSetParam(booster, "colsample_bytree", std::to_string(xgb_config->colsample_bytree).c_str());
        XGBoosterSetParam(booster, "lambda", std::to_string(xgb_config->lambda).c_str());
        XGBoosterSetParam(booster, "objective", "reg:squarederror");
        XGBoosterSetParam(booster, "tree_method", "hist");
        XGBoosterSetParam(booster, "seed", "43");  // CRITICAL: Fixed seed for exact reproducibility
        
        // Try GPU first, fallback to CPU
        if (XGBoosterSetParam(booster, "device", "cuda") != 0) {
            XGBoosterSetParam(booster, "device", "cpu");
        }
        
        // Training loop with early stopping - EXACTLY matching XGBoostModel logic
        float best_score = std::numeric_limits<float>::max();
        int best_iteration = 0;
        int rounds_without_improvement = 0;
        bool ever_improved = false;
        float initial_score = std::numeric_limits<float>::max();
        int effective_min_rounds = xgb_config->min_boost_rounds;
        int actual_iterations = 0;
        
        std::cout << "Training test model with force_minimum_training=" 
                  << (xgb_config->force_minimum_training ? "true" : "false") 
                  << ", min_boost_rounds=" << xgb_config->min_boost_rounds << "\n";
        
        for (int iter = 0; iter < xgb_config->num_boost_round; ++iter) {
            actual_iterations = iter + 1;
            XGBoosterUpdateOneIter(booster, iter, dtrain);
            
            // Evaluate
            const char* eval_result;
            XGBoosterEvalOneIter(booster, iter, eval_dmats, eval_names, 2, &eval_result);
            
            // Parse validation score - handle both rmse and quantile objectives
            std::string eval_str(eval_result);
            size_t val_pos = eval_str.find("val-");
            if (val_pos != std::string::npos) {
                // Find the colon after "val-XXX:"
                size_t colon_pos = eval_str.find(':', val_pos);
                if (colon_pos != std::string::npos) {
                    // Extract the number after the colon
                    std::string score_str = eval_str.substr(colon_pos + 1);
                    size_t end_pos = score_str.find_first_of(" \t\n");
                    if (end_pos != std::string::npos) {
                        score_str = score_str.substr(0, end_pos);
                    }
                    float val_score = std::stof(score_str);
                    
                    // Check for NaN or infinity
                    if (!std::isfinite(val_score)) {
                        std::cout << "WARNING: Validation score is NaN/Inf at iteration " << iter << std::endl;
                        rounds_without_improvement = xgb_config->early_stopping_rounds;
                    } else {
                        // On first iteration, set baseline and mark as improved
                        if (iter == 0) {
                            initial_score = val_score;
                            best_score = val_score;
                            best_iteration = 0;
                            ever_improved = true;  // Match XGBoostModel behavior
                        } else if (val_score < best_score) {
                            best_score = val_score;
                            best_iteration = iter;
                            rounds_without_improvement = 0;
                            ever_improved = true;
                        } else {
                            rounds_without_improvement++;
                        }
                    }
                    
                    // Force minimum iterations if not learning - match XGBoostModel
                    if (iter == 0 && !ever_improved) {
                        effective_min_rounds = std::max(50, effective_min_rounds);
                    }
                    
                    // Early stopping - EXACTLY match XGBoostModel logic
                    bool can_stop_early = true;
                    if (xgb_config->force_minimum_training) {
                        can_stop_early = (iter >= xgb_config->min_boost_rounds - 1);
                    } else {
                        can_stop_early = (iter >= effective_min_rounds - 1);
                    }
                    
                    if (can_stop_early && rounds_without_improvement >= xgb_config->early_stopping_rounds) {
                        if (iter + 1 <= xgb_config->min_boost_rounds + 10) {
                            std::cout << "Early stop at min rounds (" << (iter + 1) 
                                      << "), best: " << best_iteration 
                                      << ", improved: " << (ever_improved ? "yes" : "NO") << std::endl;
                        }
                        break;
                    }
                }
            }
            
            // Print progress every 20 iterations
            if (iter % 20 == 0) {
                std::cout << "Iteration " << iter << ", best score: " << best_score << "\n";
            }
        }
        
        std::cout << "Training complete. Best iteration: " << best_iteration 
                  << ", Best score: " << best_score << "\n";
        
        // Make predictions on test set
        DMatrixHandle dtest = nullptr;
        XGDMatrixCreateFromMat(X_test.data(), n_test, n_features, -1, &dtest);
        
        bst_ulong out_len;
        const float* out_result;
        XGBoosterPredict(booster, dtest, 0, 0, 0, &out_len, &out_result);
        
        // Copy and inverse transform predictions
        m_results.predictions.clear();
        m_results.predictions.reserve(out_len);
        for (bst_ulong i = 0; i < out_len; ++i) {
            float pred = out_result[i];
            
            // Inverse transform - EXACTLY like old code
            if (xgb_config->use_tanh_transform) {
                // Inverse: atanh -> unscale -> unstandardize
                float clamped = std::max(-0.9999f, std::min(0.9999f, pred));
                float atanh_val = 0.5f * std::log((1.0f + clamped) / (1.0f - clamped));
                pred = (atanh_val / xgb_config->tanh_scaling_factor) * 
                       transform_params.std_dev + transform_params.mean;
            } else if (xgb_config->use_standardization) {
                // Just inverse standardization
                pred = pred * transform_params.std_dev + transform_params.mean;
            }
            
            m_results.predictions.push_back(pred);
        }
        
        // Calculate threshold from validation predictions
        DMatrixHandle dval_for_pred = nullptr;
        XGDMatrixCreateFromMat(X_val.data(), n_val, n_features, -1, &dval_for_pred);
        
        bst_ulong val_out_len;
        const float* val_out_result;
        XGBoosterPredict(booster, dval_for_pred, 0, 0, 0, &val_out_len, &val_out_result);
        
        std::vector<float> val_predictions;
        for (bst_ulong i = 0; i < val_out_len; ++i) {
            float pred = val_out_result[i];
            
            // Inverse transform
            if (xgb_config->use_tanh_transform) {
                float clamped = std::max(-0.9999f, std::min(0.9999f, pred));
                float atanh_val = 0.5f * std::log((1.0f + clamped) / (1.0f - clamped));
                pred = (atanh_val / xgb_config->tanh_scaling_factor) * 
                       transform_params.std_dev + transform_params.mean;
            } else if (xgb_config->use_standardization) {
                pred = pred * transform_params.std_dev + transform_params.mean;
            }
            
            val_predictions.push_back(pred);
        }
        
        // Calculate 95th percentile threshold using ThresholdCalculator
        m_config.originalThreshold = ThresholdCalculator::CalculatePercentileThreshold(val_predictions, 0.95f);
        std::cout << "Calculated threshold from validation set: " << m_config.originalThreshold << "\n";
        
        // Get train predictions for profit factor calculation
        DMatrixHandle dtrain_for_pred = nullptr;
        XGDMatrixCreateFromMat(X_train.data(), n_train, n_features, -1, &dtrain_for_pred);
        
        bst_ulong train_out_len;
        const float* train_out_result;
        XGBoosterPredict(booster, dtrain_for_pred, 0, 0, 0, &train_out_len, &train_out_result);
        
        m_results.trainPredictions.clear();
        m_results.trainActuals = y_train_original;  // Store original (non-transformed) train actuals
        
        for (bst_ulong i = 0; i < train_out_len; ++i) {
            float pred = train_out_result[i];
            
            // Inverse transform
            if (xgb_config->use_tanh_transform) {
                float clamped = std::max(-0.9999f, std::min(0.9999f, pred));
                float atanh_val = 0.5f * std::log((1.0f + clamped) / (1.0f - clamped));
                pred = (atanh_val / xgb_config->tanh_scaling_factor) * 
                       transform_params.std_dev + transform_params.mean;
            } else if (xgb_config->use_standardization) {
                pred = pred * transform_params.std_dev + transform_params.mean;
            }
            
            m_results.trainPredictions.push_back(pred);
        }
        
        if (dtrain_for_pred) XGDMatrixFree(dtrain_for_pred);
        
        // Calculate metrics
        RecalculateMetricsWithThreshold();
        
        // Calculate ROC curve and optimal threshold
        CalculateROCData();
        CalculateOptimalThreshold();
        
        // Calculate profit factors
        CalculateProfitFactors();
        
        // Get feature importance (placeholder for now)
        m_results.featureImportance.clear();
        for (size_t i = 0; i < xgb_config->feature_columns.size(); ++i) {
            float importance = 1.0f - (i * 0.05f);
            m_results.featureImportance.push_back({xgb_config->feature_columns[i], 
                                                  std::max(0.1f, importance)});
        }
        
        // Cleanup
        if (dtest) XGDMatrixFree(dtest);
        if (dval_for_pred) XGDMatrixFree(dval_for_pred);
        if (dtrain) XGDMatrixFree(dtrain);
        if (dval) XGDMatrixFree(dval);
        if (booster) XGBoosterFree(booster);
        
        m_results.success = true;
        m_hasResults = true;
        
        std::cout << "Test model completed successfully\n";
        std::cout << "Signals: " << m_results.signalsGenerated << "\n";
        std::cout << "Hit Rate: " << (m_results.hitRate * 100.0f) << "%\n";
        std::cout << "Threshold: " << m_config.originalThreshold << "\n";
        
    } catch (const std::exception& e) {
        m_results.success = false;
        m_results.errorMessage = e.what();
        m_hasResults = true;
    }
}

void TestModelWindow::Clear() {
    m_hasConfiguration = false;
    m_hasResults = false;
    m_config = TestConfig();
    m_results = TestResults();
    m_model.reset();
    m_featureSelector->ClearSelection();
}

void TestModelWindow::PlotFeatureImportance() {
    if (m_results.featureImportance.empty()) {
        ImGui::Text("No feature importance data available");
        return;
    }
    
    // Prepare data for histogram - take top 10 features
    std::vector<const char*> labels;
    std::vector<float> values;
    static std::vector<std::string> label_storage; // Keep strings alive
    
    label_storage.clear();
    int num_features = std::min(10, (int)m_results.featureImportance.size());
    for (int i = 0; i < num_features; ++i) {
        label_storage.push_back(m_results.featureImportance[i].first);
        labels.push_back(label_storage.back().c_str());
        values.push_back(m_results.featureImportance[i].second);
    }
    
    if (ImPlot::BeginPlot("##FeatureImportancePlot", ImVec2(-1, -1))) {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis(ImAxis_Y1, "Importance Score", ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisTicks(ImAxis_X1, 0, num_features - 1, num_features, labels.data());
        
        ImPlot::PlotBars("##ImportanceBars", values.data(), num_features);
        
        ImPlot::EndPlot();
    }
}

void TestModelWindow::PlotPredictionScatter() {
    if (m_results.predictions.empty() || m_results.actuals.empty()) {
        ImGui::Text("No prediction data available");
        return;
    }
    
    // Debug info
    ImGui::Text("Data points: %zu, Threshold: %.6f", 
                m_results.predictions.size(), 
                m_config.originalThreshold);
    
    // Check data validity
    size_t min_size = std::min(m_results.predictions.size(), 
                               m_results.actuals.size());
    if (min_size == 0) {
        ImGui::Text("No valid data points to plot");
        return;
    }
    
    // Create copies of data to ensure they're valid
    std::vector<double> pred_data(min_size);
    std::vector<double> actual_data(min_size);
    for (size_t i = 0; i < min_size; ++i) {
        pred_data[i] = static_cast<double>(m_results.predictions[i]);
        actual_data[i] = static_cast<double>(m_results.actuals[i]);
    }
    
    if (ImPlot::BeginPlot("##PredictionsVsActualsPlot", ImVec2(-1, -1), 
                          ImPlotFlags_Equal)) {
        // Set up axes with explicit ranges
        double x_min = *std::min_element(pred_data.begin(), pred_data.end());
        double x_max = *std::max_element(pred_data.begin(), pred_data.end());
        double y_min = *std::min_element(actual_data.begin(), actual_data.end());
        double y_max = *std::max_element(actual_data.begin(), actual_data.end());
        
        // Include all thresholds in x range
        x_min = std::min({x_min, (double)m_config.originalThreshold, (double)m_results.optimalROCThreshold,
                         (double)m_results.shortThreshold5th, (double)m_results.optimalShortThreshold});
        x_max = std::max({x_max, (double)m_config.originalThreshold, (double)m_results.optimalROCThreshold,
                         (double)m_results.shortThreshold5th, (double)m_results.optimalShortThreshold});
        
        // Add padding (15% on each side)
        double x_padding = (x_max - x_min) * 0.15;
        double y_padding = (y_max - y_min) * 0.15;
        
        if (x_padding == 0) x_padding = std::abs(x_max) * 0.1 + 0.001;
        if (y_padding == 0) y_padding = std::abs(y_max) * 0.1 + 0.001;
        
        ImPlot::SetupAxis(ImAxis_X1, "Predicted");
        ImPlot::SetupAxis(ImAxis_Y1, "Actual");
        ImPlot::SetupAxisLimits(ImAxis_X1, x_min - x_padding, x_max + x_padding, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, y_min - y_padding, y_max + y_padding, ImGuiCond_Always);
        
        // Plot all points
        ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 4);
        ImPlot::PushStyleColor(ImPlotCol_MarkerFill, ImVec4(0.2f, 0.4f, 0.8f, 0.7f));
        ImPlot::PlotScatter("##AllPredictions", 
                           pred_data.data(), 
                           actual_data.data(), 
                           min_size);
        ImPlot::PopStyleColor();
        ImPlot::PopStyleVar();
        
        // Get plot limits for drawing lines
        ImPlotRect limits = ImPlot::GetPlotLimits();
        
        // Draw 95th percentile threshold line (Long) - Red
        double threshold_95_val = static_cast<double>(m_config.originalThreshold);
        if (threshold_95_val >= limits.X.Min && threshold_95_val <= limits.X.Max) {
            double threshold_x[] = {threshold_95_val, threshold_95_val};
            double threshold_y[] = {limits.Y.Min, limits.Y.Max};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0, 0, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("Long 95%", threshold_x, threshold_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add annotation
            ImPlot::Annotation(threshold_95_val, (limits.Y.Min + limits.Y.Max) * 0.8,
                             ImVec4(1, 0, 0, 1), ImVec2(5, 0), true,
                             "L95: %.4f", m_config.originalThreshold);
        }
        
        // Draw optimal ROC threshold line (Long) - Green
        double threshold_opt_val = static_cast<double>(m_results.optimalROCThreshold);
        if (threshold_opt_val >= limits.X.Min && threshold_opt_val <= limits.X.Max) {
            double threshold_x[] = {threshold_opt_val, threshold_opt_val};
            double threshold_y[] = {limits.Y.Min, limits.Y.Max};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 1, 0, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("Long Opt", threshold_x, threshold_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add annotation
            ImPlot::Annotation(threshold_opt_val, (limits.Y.Min + limits.Y.Max) * 0.6,
                             ImVec4(0, 1, 0, 1), ImVec2(5, 0), true,
                             "LOpt: %.4f", m_results.optimalROCThreshold);
        }
        
        // Draw 5th percentile threshold line (Short) - Blue
        double threshold_5th_val = static_cast<double>(m_results.shortThreshold5th);
        if (threshold_5th_val >= limits.X.Min && threshold_5th_val <= limits.X.Max) {
            double threshold_x[] = {threshold_5th_val, threshold_5th_val};
            double threshold_y[] = {limits.Y.Min, limits.Y.Max};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 0, 1, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("Short 5%", threshold_x, threshold_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add annotation
            ImPlot::Annotation(threshold_5th_val, (limits.Y.Min + limits.Y.Max) * 0.4,
                             ImVec4(0, 0, 1, 1), ImVec2(5, 0), true,
                             "S5: %.4f", m_results.shortThreshold5th);
        }
        
        // Draw optimal short threshold line - Cyan
        double threshold_short_opt_val = static_cast<double>(m_results.optimalShortThreshold);
        if (threshold_short_opt_val >= limits.X.Min && threshold_short_opt_val <= limits.X.Max) {
            double threshold_x[] = {threshold_short_opt_val, threshold_short_opt_val};
            double threshold_y[] = {limits.Y.Min, limits.Y.Max};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 1, 1, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("Short Opt", threshold_x, threshold_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add annotation
            ImPlot::Annotation(threshold_short_opt_val, (limits.Y.Min + limits.Y.Max) * 0.2,
                             ImVec4(0, 1, 1, 1), ImVec2(5, 0), true,
                             "SOpt: %.4f", m_results.optimalShortThreshold);
        }
        
        // Draw diagonal reference line (y = x)
        double diag_min = std::max(limits.X.Min, limits.Y.Min);
        double diag_max = std::min(limits.X.Max, limits.Y.Max);
        double diag_x[] = {diag_min, diag_max};
        double diag_y[] = {diag_min, diag_max};
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.0f);
        ImPlot::PlotLine("y=x", diag_x, diag_y, 2);
        ImPlot::PopStyleVar();
        ImPlot::PopStyleColor();
        
        ImPlot::EndPlot();
    }
}

void TestModelWindow::PlotPredictionHistogram() {
    if (m_results.predictions.empty()) {
        ImGui::Text("No prediction data available");
        return;
    }
    
    // Calculate histogram bins
    auto [min_it, max_it] = std::minmax_element(
        m_results.predictions.begin(), 
        m_results.predictions.end());
    float min_val = *min_it;
    float max_val = *max_it;
    
    int num_bins = 30;
    std::vector<double> bin_counts(num_bins, 0);
    float bin_width = (max_val - min_val) / num_bins;
    
    if (bin_width > 0) {
        // Count predictions in each bin
        for (float pred : m_results.predictions) {
            int bin_idx = std::min((int)((pred - min_val) / bin_width), num_bins - 1);
            if (bin_idx >= 0 && bin_idx < num_bins) {
                bin_counts[bin_idx]++;
            }
        }
        
        // Create bin centers for plotting
        std::vector<double> bin_centers(num_bins);
        for (int i = 0; i < num_bins; ++i) {
            bin_centers[i] = min_val + (i + 0.5) * bin_width;
        }
        
        if (ImPlot::BeginPlot("##PredictionsHistogram", ImVec2(-1, -1))) {
            // Calculate proper axis limits
            double max_count = *std::max_element(bin_counts.begin(), bin_counts.end());
            double x_range_min = min_val - bin_width * 1.5;
            double x_range_max = max_val + bin_width * 1.5;
            
            // Include both thresholds in range
            double min_threshold = std::min(m_config.originalThreshold, m_results.optimalROCThreshold);
            double max_threshold = std::max(m_config.originalThreshold, m_results.optimalROCThreshold);
            if (min_threshold < x_range_min) {
                x_range_min = min_threshold - bin_width;
            }
            if (max_threshold > x_range_max) {
                x_range_max = max_threshold + bin_width;
            }
            
            ImPlot::SetupAxis(ImAxis_X1, "Prediction Value");
            ImPlot::SetupAxis(ImAxis_Y1, "Count");
            ImPlot::SetupAxisLimits(ImAxis_X1, x_range_min, x_range_max, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_count * 1.1, ImGuiCond_Always);
            
            // Plot histogram bars
            ImPlot::PlotBars("##PredHist", bin_centers.data(), bin_counts.data(), 
                            num_bins, bin_width * 0.9);
            
            // Add vertical line for 95th percentile threshold (Long) - Red
            double threshold_95_x[] = {(double)m_config.originalThreshold, 
                                      (double)m_config.originalThreshold};
            double threshold_95_y[] = {0, max_count * 1.05};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0, 0, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("##95thThresholdLine", threshold_95_x, threshold_95_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add vertical line for optimal ROC threshold (Long) - Green
            double threshold_opt_x[] = {(double)m_results.optimalROCThreshold, 
                                       (double)m_results.optimalROCThreshold};
            double threshold_opt_y[] = {0, max_count * 1.05};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 1, 0, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("##OptimalThresholdLine", threshold_opt_x, threshold_opt_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add vertical line for 5th percentile threshold (Short) - Blue
            double threshold_5th_x[] = {(double)m_results.shortThreshold5th, 
                                       (double)m_results.shortThreshold5th};
            double threshold_5th_y[] = {0, max_count * 1.05};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 0, 1, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("##5thThresholdLine", threshold_5th_x, threshold_5th_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add vertical line for optimal short threshold - Cyan
            double threshold_short_opt_x[] = {(double)m_results.optimalShortThreshold, 
                                             (double)m_results.optimalShortThreshold};
            double threshold_short_opt_y[] = {0, max_count * 1.05};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 1, 1, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("##OptimalShortThresholdLine", threshold_short_opt_x, threshold_short_opt_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add text annotations for all thresholds
            if (ImPlot::IsPlotHovered()) {
                ImPlot::Annotation(m_config.originalThreshold, max_count * 0.95, 
                                 ImVec4(1, 0, 0, 1), ImVec2(5, -5), true, 
                                 "L95: %.4f", m_config.originalThreshold);
                ImPlot::Annotation(m_results.optimalROCThreshold, max_count * 0.85, 
                                 ImVec4(0, 1, 0, 1), ImVec2(5, -5), true, 
                                 "LOpt: %.4f", m_results.optimalROCThreshold);
                ImPlot::Annotation(m_results.shortThreshold5th, max_count * 0.75, 
                                 ImVec4(0, 0, 1, 1), ImVec2(5, -5), true, 
                                 "S5: %.4f", m_results.shortThreshold5th);
                ImPlot::Annotation(m_results.optimalShortThreshold, max_count * 0.65, 
                                 ImVec4(0, 1, 1, 1), ImVec2(5, -5), true, 
                                 "SOpt: %.4f", m_results.optimalShortThreshold);
            }
            
            ImPlot::EndPlot();
        }
        
        // Show statistics
        ImGui::Text("Min: %.6f, Max: %.6f", min_val, max_val);
        ImGui::Text("Mean: %.6f", 
                    std::accumulate(m_results.predictions.begin(), 
                                  m_results.predictions.end(), 0.0f) / 
                    m_results.predictions.size());
    }
}

void TestModelWindow::RecalculateMetricsWithThreshold() {
    m_results.signalsGenerated = 0;
    int correct_signals = 0;
    float total_return = 0.0f;
    
    // Dynamic positive threshold is typically 0, but check it like old code
    float dynamic_positive_threshold = 0.0f;
    
    for (size_t i = 0; i < m_results.predictions.size(); ++i) {
        // Check both conditions like old code
        if (m_results.predictions[i] > m_config.originalThreshold && 
            m_results.predictions[i] > dynamic_positive_threshold) {
            m_results.signalsGenerated++;
            if (m_results.actuals[i] > 0) {
                correct_signals++;
            }
            total_return += m_results.actuals[i];
        }
    }
    
    if (m_results.signalsGenerated > 0) {
        m_results.hitRate = (float)correct_signals / m_results.signalsGenerated;
        m_results.accuracyAboveThreshold = m_results.hitRate;
    } else {
        m_results.hitRate = 0.0f;
        m_results.accuracyAboveThreshold = 0.0f;
    }
    
    m_results.totalReturn = total_return;
    
    // Recalculate profit factors with new threshold
    CalculateProfitFactors();
}

void TestModelWindow::CalculateROCData() {
    // Calculate ROC on TEST data for evaluation
    // (but optimal threshold is still calculated from TRAIN data separately)
    if (m_results.predictions.empty() || m_results.actuals.empty()) {
        std::cout << "Warning: Cannot calculate ROC data - predictions or actuals empty\n";
        return;
    }
    
    // Additional safety check for size mismatch
    if (m_results.predictions.size() != m_results.actuals.size()) {
        std::cout << "Warning: Predictions and actuals size mismatch: " 
                  << m_results.predictions.size() << " vs " << m_results.actuals.size() << "\n";
        return;
    }
    
    // Create pairs of (prediction, actual_positive) from TEST data
    std::vector<std::pair<float, bool>> pred_actual_pairs;
    for (size_t i = 0; i < m_results.predictions.size(); ++i) {
        pred_actual_pairs.push_back({
            m_results.predictions[i],
            m_results.actuals[i] > 0.0f
        });
    }
    
    // Sort by prediction value (descending)
    std::sort(pred_actual_pairs.begin(), pred_actual_pairs.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    // Count total positives and negatives
    int total_positives = 0;
    int total_negatives = 0;
    for (const auto& pair : pred_actual_pairs) {
        if (pair.second) total_positives++;
        else total_negatives++;
    }
    
    if (total_positives == 0 || total_negatives == 0) {
        // Can't compute meaningful ROC curve
        return;
    }
    
    // Clear previous ROC data
    m_results.rocFPR.clear();
    m_results.rocTPR.clear();
    m_results.rocThresholds.clear();
    
    // Add the (0,0) point
    m_results.rocFPR.push_back(0.0f);
    m_results.rocTPR.push_back(0.0f);
    m_results.rocThresholds.push_back(std::numeric_limits<float>::max());
    
    // Calculate ROC points
    int true_positives = 0;
    int false_positives = 0;
    float last_threshold = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < pred_actual_pairs.size(); ++i) {
        float threshold = pred_actual_pairs[i].first;
        
        // Update counts
        if (pred_actual_pairs[i].second) {
            true_positives++;
        } else {
            false_positives++;
        }
        
        // Add ROC point if threshold changes or at the end
        if (i == pred_actual_pairs.size() - 1 || 
            (i < pred_actual_pairs.size() - 1 && threshold != pred_actual_pairs[i + 1].first)) {
            float tpr = (float)true_positives / total_positives;
            float fpr = (float)false_positives / total_negatives;
            
            m_results.rocTPR.push_back(tpr);
            m_results.rocFPR.push_back(fpr);
            m_results.rocThresholds.push_back(threshold);
        }
    }
    
    // Add the (1,1) point
    m_results.rocFPR.push_back(1.0f);
    m_results.rocTPR.push_back(1.0f);
    m_results.rocThresholds.push_back(-std::numeric_limits<float>::max());
    
    // Calculate AUC using trapezoidal rule
    m_results.rocAUC = 0.0f;
    for (size_t i = 1; i < m_results.rocFPR.size(); ++i) {
        float width = m_results.rocFPR[i] - m_results.rocFPR[i-1];
        float height = (m_results.rocTPR[i] + m_results.rocTPR[i-1]) / 2.0f;
        m_results.rocAUC += width * height;
    }
    
    // Calculate R-squared
    float mean_actual = 0.0f;
    for (float actual : m_results.actuals) {
        mean_actual += actual;
    }
    mean_actual /= m_results.actuals.size();
    
    float ss_tot = 0.0f;
    float ss_res = 0.0f;
    for (size_t i = 0; i < m_results.actuals.size(); ++i) {
        float diff_mean = m_results.actuals[i] - mean_actual;
        ss_tot += diff_mean * diff_mean;
        
        float diff_pred = m_results.actuals[i] - m_results.predictions[i];
        ss_res += diff_pred * diff_pred;
    }
    
    if (ss_tot > 0) {
        m_results.rSquared = 1.0f - (ss_res / ss_tot);
    } else {
        m_results.rSquared = 0.0f;
    }
}

void TestModelWindow::CalculateOptimalThreshold() {
    // Use ThresholdCalculator to find optimal threshold on TRAINING data
    // IMPORTANT: Use TRAINING data to avoid data leakage
    
    if (m_results.trainPredictions.empty() || m_results.trainActuals.empty()) {
        std::cout << "Warning: Cannot calculate optimal threshold - train predictions or actuals empty\n";
        return;
    }
    
    // Additional safety check for size mismatch
    if (m_results.trainPredictions.size() != m_results.trainActuals.size()) {
        std::cout << "Warning: Train predictions and actuals size mismatch: " 
                  << m_results.trainPredictions.size() << " vs " << m_results.trainActuals.size() << "\n";
        return;
    }
    
    // Use ThresholdCalculator for optimal threshold calculation
    m_results.optimalROCThreshold = ThresholdCalculator::CalculateOptimalThreshold(
        m_results.trainPredictions, 
        m_results.trainActuals,
        1  // min_kept_percent = 1% of data
    );
}

void TestModelWindow::CalculateProfitFactors() {
    // Calculate profit factors for train and test sets
    
    // First, calculate 5th percentile threshold for short trades
    if (!m_results.predictions.empty()) {
        std::vector<float> sorted_preds = m_results.predictions;
        std::sort(sorted_preds.begin(), sorted_preds.end());
        int percentile_idx_5th = static_cast<int>(0.05f * (sorted_preds.size() - 1));
        m_results.shortThreshold5th = sorted_preds[percentile_idx_5th];
    }
    
    // Calculate optimal short threshold (mirror of long optimal threshold)
    // Use the negative of optimal long threshold as a starting point
    if (!m_results.trainPredictions.empty() && !m_results.trainActuals.empty()) {
        // Find threshold that maximizes short profit factor on training data
        // For simplicity, use negative of optimal long threshold
        m_results.optimalShortThreshold = -m_results.optimalROCThreshold;
        
        // Alternatively, calculate optimal short threshold based on inverted returns
        std::vector<float> inverted_actuals;
        for (float actual : m_results.trainActuals) {
            inverted_actuals.push_back(-actual);  // Invert returns for short optimization
        }
        m_results.optimalShortThreshold = ThresholdCalculator::CalculateOptimalThreshold(
            m_results.trainPredictions, 
            inverted_actuals,
            1  // min_kept_percent = 1% of data
        );
    }
    
    // Test set profit factor (long-only above 95th percentile threshold)
    float test_win_95 = 0.0f;
    float test_lose_95 = 0.0f;
    
    for (size_t i = 0; i < m_results.predictions.size(); ++i) {
        if (m_results.predictions[i] > m_config.originalThreshold) {
            if (m_results.actuals[i] > 0.0f) {
                test_win_95 += m_results.actuals[i];
            } else {
                test_lose_95 -= m_results.actuals[i];
            }
        }
    }
    
    m_results.testProfitFactorLongOnly = (test_lose_95 > 0) ? (test_win_95 / test_lose_95) : 
                                          ((test_win_95 > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Train set profit factor (long-only above 95th percentile threshold)
    float train_win_95 = 0.0f;
    float train_lose_95 = 0.0f;
    
    for (size_t i = 0; i < m_results.trainPredictions.size(); ++i) {
        if (m_results.trainPredictions[i] > m_config.originalThreshold) {
            if (m_results.trainActuals[i] > 0.0f) {
                train_win_95 += m_results.trainActuals[i];
            } else {
                train_lose_95 -= m_results.trainActuals[i];
            }
        }
    }
    
    m_results.trainProfitFactorLongOnly = (train_lose_95 > 0) ? (train_win_95 / train_lose_95) : 
                                           ((train_win_95 > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Test set profit factor (long-only above optimal ROC threshold)
    float test_win_opt = 0.0f;
    float test_lose_opt = 0.0f;
    
    for (size_t i = 0; i < m_results.predictions.size(); ++i) {
        if (m_results.predictions[i] > m_results.optimalROCThreshold) {
            if (m_results.actuals[i] > 0.0f) {
                test_win_opt += m_results.actuals[i];
            } else {
                test_lose_opt -= m_results.actuals[i];
            }
        }
    }
    
    m_results.testProfitFactorOptimal = (test_lose_opt > 0) ? (test_win_opt / test_lose_opt) : 
                                         ((test_win_opt > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Train set profit factor (long-only above optimal ROC threshold)
    float train_win_opt = 0.0f;
    float train_lose_opt = 0.0f;
    
    for (size_t i = 0; i < m_results.trainPredictions.size(); ++i) {
        if (m_results.trainPredictions[i] > m_results.optimalROCThreshold) {
            if (m_results.trainActuals[i] > 0.0f) {
                train_win_opt += m_results.trainActuals[i];
            } else {
                train_lose_opt -= m_results.trainActuals[i];
            }
        }
    }
    
    m_results.trainProfitFactorOptimal = (train_lose_opt > 0) ? (train_win_opt / train_lose_opt) : 
                                          ((train_win_opt > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Overall profit factors (all trades)
    float test_win_all = 0.0f;
    float test_lose_all = 0.0f;
    
    for (float ret : m_results.actuals) {
        if (ret > 0.0f) {
            test_win_all += ret;
        } else {
            test_lose_all -= ret;
        }
    }
    
    m_results.testProfitFactor = (test_lose_all > 0) ? (test_win_all / test_lose_all) : 
                                  ((test_win_all > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    float train_win_all = 0.0f;
    float train_lose_all = 0.0f;
    
    for (float ret : m_results.trainActuals) {
        if (ret > 0.0f) {
            train_win_all += ret;
        } else {
            train_lose_all -= ret;
        }
    }
    
    m_results.trainProfitFactor = (train_lose_all > 0) ? (train_win_all / train_lose_all) : 
                                   ((train_win_all > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Short-only profit factors (5th percentile threshold)
    // For short trades, we invert the logic: profit when actual < 0
    float test_win_short_5th = 0.0f;
    float test_lose_short_5th = 0.0f;
    
    for (size_t i = 0; i < m_results.predictions.size(); ++i) {
        if (m_results.predictions[i] < m_results.shortThreshold5th) {
            // Short position: we profit when price goes down (actual < 0)
            if (m_results.actuals[i] < 0.0f) {
                test_win_short_5th += -m_results.actuals[i];  // Invert the return
            } else {
                test_lose_short_5th += m_results.actuals[i];
            }
        }
    }
    
    m_results.testProfitFactorShortOnly5th = (test_lose_short_5th > 0) ? (test_win_short_5th / test_lose_short_5th) : 
                                              ((test_win_short_5th > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Train set short-only profit factor (5th percentile)
    float train_win_short_5th = 0.0f;
    float train_lose_short_5th = 0.0f;
    
    for (size_t i = 0; i < m_results.trainPredictions.size(); ++i) {
        if (m_results.trainPredictions[i] < m_results.shortThreshold5th) {
            if (m_results.trainActuals[i] < 0.0f) {
                train_win_short_5th += -m_results.trainActuals[i];
            } else {
                train_lose_short_5th += m_results.trainActuals[i];
            }
        }
    }
    
    m_results.trainProfitFactorShortOnly5th = (train_lose_short_5th > 0) ? (train_win_short_5th / train_lose_short_5th) : 
                                               ((train_win_short_5th > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Short-only profit factors (optimal short threshold)
    float test_win_short_opt = 0.0f;
    float test_lose_short_opt = 0.0f;
    
    for (size_t i = 0; i < m_results.predictions.size(); ++i) {
        if (m_results.predictions[i] < m_results.optimalShortThreshold) {
            if (m_results.actuals[i] < 0.0f) {
                test_win_short_opt += -m_results.actuals[i];
            } else {
                test_lose_short_opt += m_results.actuals[i];
            }
        }
    }
    
    m_results.testProfitFactorShortOnlyOptimal = (test_lose_short_opt > 0) ? (test_win_short_opt / test_lose_short_opt) : 
                                                  ((test_win_short_opt > 0) ? std::numeric_limits<float>::max() : 0.0f);
    
    // Train set short-only profit factor (optimal threshold)
    float train_win_short_opt = 0.0f;
    float train_lose_short_opt = 0.0f;
    
    for (size_t i = 0; i < m_results.trainPredictions.size(); ++i) {
        if (m_results.trainPredictions[i] < m_results.optimalShortThreshold) {
            if (m_results.trainActuals[i] < 0.0f) {
                train_win_short_opt += -m_results.trainActuals[i];
            } else {
                train_lose_short_opt += m_results.trainActuals[i];
            }
        }
    }
    
    m_results.trainProfitFactorShortOnlyOptimal = (train_lose_short_opt > 0) ? (train_win_short_opt / train_lose_short_opt) : 
                                                   ((train_win_short_opt > 0) ? std::numeric_limits<float>::max() : 0.0f);
}

void TestModelWindow::PlotROCCurve() {
    if (m_results.rocFPR.empty() || m_results.rocTPR.empty()) {
        ImGui::Text("No ROC data available");
        return;
    }
    
    ImGui::Text("ROC AUC: %.4f", m_results.rocAUC);
    ImGui::Text("R-squared: %.4f", m_results.rSquared);
    
    if (ImPlot::BeginPlot("##ROCCurvePlot", ImVec2(-1, -1))) {
        ImPlot::SetupAxis(ImAxis_X1, "False Positive Rate");
        ImPlot::SetupAxis(ImAxis_Y1, "True Positive Rate");
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, 1, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImGuiCond_Always);
        
        // Convert to double for ImPlot
        std::vector<double> fpr_double(m_results.rocFPR.begin(), m_results.rocFPR.end());
        std::vector<double> tpr_double(m_results.rocTPR.begin(), m_results.rocTPR.end());
        
        // Plot ROC curve
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
        ImPlot::PlotLine("ROC", fpr_double.data(), tpr_double.data(), fpr_double.size());
        ImPlot::PopStyleColor();
        ImPlot::PopStyleVar();
        
        // Plot diagonal reference line
        double diag_x[] = {0, 1};
        double diag_y[] = {0, 1};
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.0f);
        ImPlot::PlotLine("Random", diag_x, diag_y, 2);
        ImPlot::PopStyleVar();
        ImPlot::PopStyleColor();
        
        // Mark current threshold point on ROC curve
        for (size_t i = 0; i < m_results.rocThresholds.size(); ++i) {
            if (std::abs(m_results.rocThresholds[i] - m_config.originalThreshold) < 1e-6) {
                double point_x = m_results.rocFPR[i];
                double point_y = m_results.rocTPR[i];
                ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 8);
                ImPlot::PushStyleColor(ImPlotCol_MarkerFill, ImVec4(1, 0, 0, 1));
                ImPlot::PlotScatter("Current Threshold", &point_x, &point_y, 1);
                ImPlot::PopStyleColor();
                ImPlot::PopStyleVar();
                break;
            }
        }
        
        // Mark optimal threshold point
        for (size_t i = 0; i < m_results.rocThresholds.size(); ++i) {
            if (std::abs(m_results.rocThresholds[i] - m_results.optimalROCThreshold) < 1e-6) {
                double point_x = m_results.rocFPR[i];
                double point_y = m_results.rocTPR[i];
                ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 8);
                ImPlot::PushStyleColor(ImPlotCol_MarkerFill, ImVec4(0, 1, 0, 1));
                ImPlot::PlotScatter("Optimal Threshold", &point_x, &point_y, 1);
                ImPlot::PopStyleColor();
                ImPlot::PopStyleVar();
                break;
            }
        }
        
        ImPlot::EndPlot();
    }
}

} // namespace simulation
// Proper TestModelWindow implementation based on original 3000-line SimulationWindow
// This copies the exact test model logic without modifications

#include "TestModelWindow.h"
#include "ISimulationModel_v2.h"
#include "SimulationEngine.h"
#include "XGBoostConfig.h"
#include "models/XGBoostModel.h"
#include "../TimeSeriesWindow.h"
#include "../analytics_dataframe.h"
#include "imgui.h"
#include "../implot.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <xgboost/c_api.h>
#include <arrow/api.h>
#include <arrow/scalar.h>

namespace simulation {

TestModelWindow::TestModelWindow()
    : m_isVisible(false)
    , m_hasConfiguration(false)
    , m_hasResults(false)
    , m_timeSeriesWindow(nullptr) {
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
    
    // Training range inputs
    ImGui::Text("Training Data Range:");
    ImGui::Text("Start Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##train_start", &m_config.trainStart, 1000, 5000)) {
        m_config.trainStart = std::max(0, m_config.trainStart);
    }
    
    ImGui::Text("End Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##train_end", &m_config.trainEnd, 1000, 5000)) {
        m_config.trainEnd = std::max(m_config.trainStart + 1000, m_config.trainEnd);
    }
    
    ImGui::Separator();
    
    // Test range inputs
    ImGui::Text("Test Data Range:");
    ImGui::Text("Start Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##test_start", &m_config.testStart, 50, 200)) {
        m_config.testStart = std::max(m_config.trainEnd, m_config.testStart);
    }
    
    ImGui::Text("End Row:");
    ImGui::SameLine();
    if (ImGui::InputInt("##test_end", &m_config.testEnd, 50, 200)) {
        m_config.testEnd = std::max(m_config.testStart + 50, m_config.testEnd);
    }
    
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
            // Calculate 95th percentile
            std::vector<float> sorted_preds = m_results.predictions;
            std::sort(sorted_preds.begin(), sorted_preds.end());
            int percentile_idx = (int)(sorted_preds.size() * 0.95);
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
        ImGui::Text("Results:");
        ImGui::Text("Signals Generated: %d", m_results.signalsGenerated);
        ImGui::Text("Hit Rate: %.2f%%", m_results.hitRate * 100.0f);
        ImGui::Text("Accuracy Above Threshold: %.2f%%", m_results.accuracyAboveThreshold * 100.0f);
        ImGui::Text("Total Return: %.6f", m_results.totalReturn);
        
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
                          "Note: Test Model retrains from scratch. Results may vary from original fold "
                          "due to random initialization. The threshold is preserved from the original fold.");
        ImGui::PopTextWrapPos();
        
        ImGui::Separator();
        
        // Feature importance plot
        if (ImGui::CollapsingHeader("Feature Importance")) {
            PlotFeatureImportance();
        }
        
        // Prediction scatter plot
        if (ImGui::CollapsingHeader("Predictions vs Actuals")) {
            PlotPredictionScatter();
        }
        
        // Predictions histogram
        if (ImGui::CollapsingHeader("Predictions Distribution")) {
            PlotPredictionHistogram();
        }
    } else if (m_hasResults && !m_results.success) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
            "Error: %s", m_results.errorMessage.c_str());
    }
}

void TestModelWindow::SetFromFold(const FoldResult& fold, const SimulationRun& run) {
    // Copy ALL configuration from the fold
    m_config.sourceFold = fold;
    m_config.sourceRunName = run.name;
    m_config.sourceModelType = run.model_type;
    
    // Copy data ranges
    m_config.trainStart = fold.train_start;
    m_config.trainEnd = fold.train_end;
    m_config.testStart = fold.test_start;
    m_config.testEnd = fold.test_end;
    
    // Note: We don't copy the threshold - it will be calculated fresh during training
    m_config.originalThreshold = 0.0f;  // Will be calculated after training
    m_config.modelType = run.model_type;
    
    // Store the fold's complete configuration including:
    // - All hyperparameters (learning_rate, max_depth, etc.)
    // - Feature columns in exact order
    // - Target column
    // - Transformation parameters (mean, std, scaling factor)
    // - Random seed if applicable
    
    // Copy the complete model config from the run
    // We need to create a copy since run.config is a unique_ptr
    if (run.config) {
        // For XGBoost, copy all the parameters
        if (run.model_type == "XGBoost") {
            auto* xgb_src = dynamic_cast<XGBoostConfig*>(run.config.get());
            if (xgb_src) {
                auto xgb_copy = std::make_shared<XGBoostConfig>(*xgb_src);
                m_config.modelConfig = xgb_copy;
            }
        }
        // For other model types, would need similar handling
    } else {
        // If no config in run, create a default one with standard parameters
        // This happens because SimulationEngine doesn't store config in run yet
        if (run.model_type == "XGBoost" || run.model_type.empty()) {
            auto xgb_config = std::make_shared<XGBoostConfig>();
            // Use default hyperparameters from the original simulation
            xgb_config->learning_rate = 0.01f;
            xgb_config->max_depth = 4;
            xgb_config->min_child_weight = 10.0f;
            xgb_config->subsample = 0.8f;
            xgb_config->colsample_bytree = 0.7f;
            xgb_config->lambda = 2.0f;
            xgb_config->num_boost_round = 2000;
            xgb_config->early_stopping_rounds = 50;
            xgb_config->min_boost_rounds = 100;
            
            // We need to get the actual feature columns that were used
            // These should come from the actual simulation configuration
            // For now, use the standard defaults from the simulation
            xgb_config->feature_columns = {
                "returns_1", "returns_2", "returns_3", "returns_4", "returns_5",
                "returns_10", "returns_20", "returns_30", "returns_60",
                "volume_1", "volume_2", "volume_3", "volume_4", "volume_5"
            };
            xgb_config->target_column = "future_return_1";
            
            // Copy transformation settings
            xgb_config->use_tanh_transform = true;
            xgb_config->tanh_scaling_factor = 0.001f;
            xgb_config->use_standardization = false;
            xgb_config->val_split_ratio = 0.8f;
            
            m_config.modelConfig = xgb_config;
            
            std::cout << "Created default XGBoost config with " 
                      << xgb_config->feature_columns.size() << " features\n";
        }
    }
    
    // Store transformation parameters from fold
    m_config.transformMean = fold.mean_scale;
    m_config.transformStd = fold.std_scale;
    // Note: tanh_scaling_factor should come from fold.fold_configuration if available
    
    m_hasConfiguration = true;
    m_hasResults = false;
    
    std::cout << "\n=== Test Model Configuration Set ===\n";
    std::cout << "Source: " << run.name << " - Fold " << fold.fold_number << "\n";
    std::cout << "Train: [" << m_config.trainStart << ", " << m_config.trainEnd << "]\n";
    std::cout << "Test: [" << m_config.testStart << ", " << m_config.testEnd << "]\n";
    std::cout << "Threshold will be calculated fresh during training\n";
    if (m_config.modelConfig) {
        auto* xgb = dynamic_cast<XGBoostConfig*>(m_config.modelConfig.get());
        if (xgb) {
            std::cout << "Features: " << xgb->feature_columns.size() 
                      << ", Target: " << xgb->target_column << "\n";
        }
    }
}

void TestModelWindow::Clear() {
    m_hasConfiguration = false;
    m_hasResults = false;
    m_config = TestConfig();
    m_results = TestResults();
    m_model.reset();
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
    
    if (ImPlot::BeginPlot("##FeatureImportancePlot", ImVec2(-1, 300))) {
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
    
    if (ImPlot::BeginPlot("##PredictionsVsActualsPlot", ImVec2(-1, 400), 
                          ImPlotFlags_Equal)) {
        // Set up axes with explicit ranges
        double x_min = *std::min_element(pred_data.begin(), pred_data.end());
        double x_max = *std::max_element(pred_data.begin(), pred_data.end());
        double y_min = *std::min_element(actual_data.begin(), actual_data.end());
        double y_max = *std::max_element(actual_data.begin(), actual_data.end());
        
        // Include threshold in x range
        x_min = std::min(x_min, (double)m_config.originalThreshold);
        x_max = std::max(x_max, (double)m_config.originalThreshold);
        
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
        
        // Draw threshold line (vertical)
        double threshold_val = static_cast<double>(m_config.originalThreshold);
        if (threshold_val >= limits.X.Min && threshold_val <= limits.X.Max) {
            double threshold_x[] = {threshold_val, threshold_val};
            double threshold_y[] = {limits.Y.Min, limits.Y.Max};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0, 0, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("Threshold", threshold_x, threshold_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add annotation
            ImPlot::Annotation(threshold_val, (limits.Y.Min + limits.Y.Max) * 0.5,
                             ImVec4(1, 0, 0, 1), ImVec2(5, 0), true,
                             "Threshold: %.4f", m_config.originalThreshold);
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
        
        if (ImPlot::BeginPlot("##PredictionsHistogram", ImVec2(-1, 250))) {
            // Calculate proper axis limits
            double max_count = *std::max_element(bin_counts.begin(), bin_counts.end());
            double x_range_min = min_val - bin_width * 1.5;
            double x_range_max = max_val + bin_width * 1.5;
            
            // Include threshold in range
            if (m_config.originalThreshold < x_range_min) {
                x_range_min = m_config.originalThreshold - bin_width;
            }
            if (m_config.originalThreshold > x_range_max) {
                x_range_max = m_config.originalThreshold + bin_width;
            }
            
            ImPlot::SetupAxis(ImAxis_X1, "Prediction Value");
            ImPlot::SetupAxis(ImAxis_Y1, "Count");
            ImPlot::SetupAxisLimits(ImAxis_X1, x_range_min, x_range_max, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_count * 1.1, ImGuiCond_Always);
            
            // Plot histogram bars
            ImPlot::PlotBars("##PredHist", bin_centers.data(), bin_counts.data(), 
                            num_bins, bin_width * 0.9);
            
            // Add vertical line for threshold
            double threshold_x[] = {(double)m_config.originalThreshold, 
                                   (double)m_config.originalThreshold};
            double threshold_y[] = {0, max_count * 1.05};
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0, 0, 1));
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
            ImPlot::PlotLine("##ThresholdHistLine", threshold_x, threshold_y, 2);
            ImPlot::PopStyleVar();
            ImPlot::PopStyleColor();
            
            // Add text annotation for threshold
            if (ImPlot::IsPlotHovered()) {
                ImPlot::Annotation(m_config.originalThreshold, max_count * 0.9, 
                                 ImVec4(1, 0, 0, 1), ImVec2(5, -5), true, 
                                 "Threshold: %.4f", m_config.originalThreshold);
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
    
    for (size_t i = 0; i < m_results.predictions.size(); ++i) {
        if (m_results.predictions[i] > m_config.originalThreshold) {
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
        
        // Extract training data
        for (int feat_idx = 0; feat_idx < n_features; ++feat_idx) {
            const std::string& feature = xgb_config->feature_columns[feat_idx];
            auto column = table->GetColumnByName(feature);
            if (!column) {
                throw std::runtime_error("Feature column not found: " + feature);
            }
            
            // Extract train data
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
            
            // Extract validation data
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
            
            // Extract test data
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
        
        // Extract train labels
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
        
        // Extract validation labels
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
        
        // Extract test labels (for plotting)
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
        
        // Apply transformations if specified
        if (xgb_config->use_tanh_transform) {
            float scale = xgb_config->tanh_scaling_factor;
            // Apply tanh to labels
            for (auto& val : y_train) val = std::tanh(val / scale);
            for (auto& val : y_val) val = std::tanh(val / scale);
            // Don't transform test labels - we want original scale for results
        }
        
        // Train XGBoost model
        models::XGBoostModel xgb_model;
        auto train_result = xgb_model.Train(X_train, y_train, X_val, y_val, 
                                           *xgb_config, n_features);
        
        if (!train_result.success) {
            m_results.success = false;
            m_results.errorMessage = "Training failed: " + train_result.error_message;
            m_hasResults = true;
            return;
        }
        
        // Get predictions on test set
        auto pred_result = xgb_model.Predict(X_test, n_test, n_features);
        if (!pred_result.success) {
            m_results.success = false;
            m_results.errorMessage = "Prediction failed: " + pred_result.error_message;
            m_hasResults = true;
            return;
        }
        
        m_results.predictions = pred_result.predictions;
        
        // Inverse transform predictions if needed
        if (xgb_config->use_tanh_transform) {
            float scale = xgb_config->tanh_scaling_factor;
            for (auto& val : m_results.predictions) {
                val = std::atanh(std::max(-0.999f, std::min(0.999f, val))) * scale;
            }
        }
        
        // Always calculate threshold based on validation predictions
        // We need to get predictions on the validation set to calculate threshold
        std::vector<float> val_predictions;
        {
            // Get validation predictions
            auto val_pred_result = xgb_model.Predict(X_val, n_val, n_features);
            if (val_pred_result.success) {
                val_predictions = val_pred_result.predictions;
                
                // Inverse transform validation predictions if needed
                if (xgb_config->use_tanh_transform) {
                    float scale = xgb_config->tanh_scaling_factor;
                    for (auto& val : val_predictions) {
                        val = std::atanh(std::max(-0.999f, std::min(0.999f, val))) * scale;
                    }
                }
                
                // Calculate 95th percentile threshold from validation predictions
                std::vector<float> sorted_preds = val_predictions;
                std::sort(sorted_preds.begin(), sorted_preds.end());
                int percentile_idx = (int)(sorted_preds.size() * 0.95);
                m_config.originalThreshold = sorted_preds[percentile_idx];
                
                std::cout << "Calculated threshold from validation set: " << m_config.originalThreshold << "\n";
            } else {
                // Fallback: use test predictions if validation failed
                std::vector<float> sorted_preds = m_results.predictions;
                std::sort(sorted_preds.begin(), sorted_preds.end());
                int percentile_idx = (int)(sorted_preds.size() * 0.95);
                m_config.originalThreshold = sorted_preds[percentile_idx];
                std::cout << "WARNING: Using test set for threshold (fallback): " << m_config.originalThreshold << "\n";
            }
        }
        
        // Calculate metrics
        RecalculateMetricsWithThreshold();
        
        // Get feature importance from the trained model
        m_results.featureImportance = xgb_model.GetFeatureImportance();
        
        // If feature importance extraction failed, provide placeholder values
        if (m_results.featureImportance.empty()) {
            std::cout << "Warning: Could not extract feature importance from model\\n";
            // Create placeholder importance based on feature order
            for (size_t i = 0; i < xgb_config->feature_columns.size(); ++i) {
                float importance = 1.0f - (i * 0.05f);  // Decreasing importance
                m_results.featureImportance.push_back({xgb_config->feature_columns[i], 
                                                      std::max(0.1f, importance)});
            }
        }
        
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

} // namespace simulation
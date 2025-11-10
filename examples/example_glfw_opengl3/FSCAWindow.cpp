#include "FSCAWindow.h"

#include "TimeSeriesWindow.h"
#include "analytics_dataframe.h"
#include "column_view.h"

#include <algorithm>
#include <arrow/status.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <future>

FSCAWindow::FSCAWindow()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_numComponents(3)
    , m_standardize(true)
    , m_isRunning(false)
    , m_progress(std::make_shared<std::atomic<double>>(0.0))
    , m_hasResult(false)
    , m_hasError(false) {

    m_statusMessage = "Idle";
    m_featureSelector.SetTargetPrefix("tgt_");
    m_featureSelector.SetShowOnlyTargetsWithPrefix(false);
    m_featureSelector.SetSortAlphabetically(true);
}

void FSCAWindow::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void FSCAWindow::UpdateColumnList() {
    if (!m_dataSource || !m_dataSource->HasData()) {
        m_availableColumns.clear();
        return;
    }
    auto df = m_dataSource->GetDataFrame();
    if (!df) {
        m_availableColumns.clear();
        return;
    }
    m_availableColumns = df->column_names();
    m_featureSelector.SetAvailableColumns(m_availableColumns);
}

void FSCAWindow::ResetResults() {
    m_hasResult = false;
    m_hasError = false;
    m_errorMessage.clear();
    m_result = fsca::FscaResult{};
    m_selectedFeatureNames.clear();
}

void FSCAWindow::Draw() {
    if (!m_isVisible) {
        return;
    }

    if (m_isRunning && m_future.valid() &&
        m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_result = m_future.get();
            m_hasResult = !m_result.components.empty();
            m_statusMessage = m_hasResult ? "Analysis complete" : "No components extracted";
            m_progress->store(1.0);
        } catch (const std::exception& ex) {
            m_hasError = true;
            m_errorMessage = ex.what();
            m_statusMessage = "Analysis failed";
            m_progress->store(1.0);
        }
        m_isRunning = false;
    }

    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Forward Selection Component Analysis", &m_isVisible)) {
        ImGui::End();
        return;
    }

    if (!m_dataSource || !m_dataSource->HasData()) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "No data loaded. Please load data in the Time Series window.");
        ImGui::End();
        return;
    }

    ImGui::Columns(2, nullptr, true);
    ImGui::SetColumnWidth(0, 420);

    DrawConfigurationPanel();

    ImGui::NextColumn();
    DrawResultsPanel();

    ImGui::Columns(1);
    DrawStatusBar();

    ImGui::End();
}

void FSCAWindow::DrawConfigurationPanel() {
    ImGui::BeginChild("FSCAConfig", ImVec2(0, 0), true);

    ImGui::Text("Feature Selection");
    ImGui::Separator();
    m_featureSelector.DrawFeatureSelection();

    ImGui::Spacing();
    ImGui::Text("Component Settings");
    ImGui::Separator();

    ImGui::SliderInt("Components", &m_numComponents, 1, 20);
    ImGui::Checkbox("Standardize Inputs", &m_standardize);

    ImGui::Spacing();
    if (ImGui::Button("Run FSCA", ImVec2(-1, 0)) && !m_isRunning) {
        StartAnalysis();
    }

    if (m_isRunning) {
        double progressValue = (m_progress ? m_progress->load() : 0.0);
        if (progressValue < 0.0) progressValue = 0.0;
        if (progressValue > 1.0) progressValue = 1.0;
        ImGui::Spacing();
        ImGui::ProgressBar(static_cast<float>(progressValue), ImVec2(-1, 0), "Running...");
    }

    ImGui::EndChild();
}

void FSCAWindow::DrawResultsPanel() {
    ImGui::BeginChild("FSCAResults", ImVec2(0, -120), true);

    if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
        ImGui::EndChild();
        return;
    }

    if (!m_hasResult) {
        if (!m_isRunning) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Select features and run the analysis.");
        }
        ImGui::EndChild();
        return;
    }

    double totalVar = m_result.totalVariance;
    ImGui::Text("Total variance: %.4f   Explained: %.4f (%.2f%%)",
                totalVar,
                m_result.explainedVariance,
                totalVar > 0.0 ? (100.0 * m_result.explainedVariance / totalVar) : 0.0);

    if (ImGui::BeginTable("FSCAComponents", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY, ImVec2(0, 260))) {
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthStretch, 150);
        ImGui::TableSetupColumn("Unique Var", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Cumulative", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("% of Total", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < m_result.components.size(); ++i) {
            const auto& comp = m_result.components[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            ImGui::Text("%s", comp.variableName.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", comp.uniqueVariance);
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", comp.cumulativeVariance);
            ImGui::TableNextColumn();
            double percent = totalVar > 0.0 ? (100.0 * comp.cumulativeVariance / totalVar) : 0.0;
            ImGui::Text("%.2f%%", percent);
        }

        ImGui::EndTable();
    }

    if (!m_result.components.empty()) {
        ImGui::Spacing();
        ImGui::Text("Component Loadings (correlation with original variables)");
        ImGui::Separator();

        int numVariables = static_cast<int>(m_selectedFeatureNames.size());

        if (ImGui::BeginTable("FSCALoadings", static_cast<int>(m_result.components.size()) + 1,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthStretch);
            for (std::size_t c = 0; c < m_result.components.size(); ++c) {
                std::string header = "Comp " + std::to_string(c + 1);
                ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 80);
            }
            ImGui::TableHeadersRow();

            for (int var = 0; var < numVariables; ++var) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", m_selectedFeatureNames[var].c_str());
                for (const auto& comp : m_result.components) {
                    ImGui::TableNextColumn();
                    if (var < comp.loadings.size()) {
                        ImGui::Text("%.4f", comp.loadings[var]);
                    } else {
                        ImGui::Text("-");
                    }
                }
            }

            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
}

void FSCAWindow::DrawStatusBar() {
    ImGui::Separator();
    if (m_isRunning) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), "Running analysis...");
    } else if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
    } else {
        ImGui::Text("%s", m_statusMessage.c_str());
    }
}

void FSCAWindow::StartAnalysis() {
    if (m_isRunning) {
        return;
    }

    Eigen::MatrixXd features;
    std::vector<std::string> featureNames;
    std::string error;

    if (!PrepareData(features, featureNames, error)) {
        m_hasError = true;
        m_errorMessage = error;
        m_statusMessage = "Failed to prepare data";
        return;
    }

    if (featureNames.size() < 1) {
        m_hasError = true;
        m_errorMessage = "Select at least one feature.";
        return;
    }

    ResetResults();
    m_selectedFeatureNames = featureNames;
    m_statusMessage = "Running analysis...";
    m_progress->store(0.0);
    m_isRunning = true;

    fsca::FscaConfig config;
    config.numComponents = m_numComponents;
    config.standardize = m_standardize;

    auto progress = m_progress;

    m_future = std::async(std::launch::async,
                          [config,
                           data = std::move(features),
                           names = featureNames,
                           progress]() mutable {
                              fsca::FscaAnalyzer analyzer(config);
                              auto callback = [progress](double fraction) {
                                  if (progress) {
                                      progress->store(fraction);
                                  }
                              };
                              return analyzer.analyze(data, names);
                          });
}

bool FSCAWindow::PrepareData(Eigen::MatrixXd& features,
                             std::vector<std::string>& featureNames,
                             std::string& errorMessage) const {
    if (!m_dataSource || !m_dataSource->HasData()) {
        errorMessage = "Data source is unavailable.";
        return false;
    }

    auto dfPtr = m_dataSource->GetDataFrame();
    if (!dfPtr) {
        errorMessage = "Analytics data frame is null.";
        return false;
    }

    auto selectedFeatures = m_featureSelector.GetSelectedFeatures();
    if (selectedFeatures.empty()) {
        errorMessage = "Select at least one feature column.";
        return false;
    }

    auto cpuResult = dfPtr->to_cpu();
    if (!cpuResult.ok()) {
        errorMessage = cpuResult.status().ToString();
        return false;
    }
    auto cpuFrame = cpuResult.MoveValueUnsafe();
    int64_t numRows = cpuFrame.num_rows();
    if (numRows <= 0) {
        errorMessage = "No rows available in dataset.";
        return false;
    }

    std::vector<chronosflow::ColumnView<double>> featureViews;
    featureViews.reserve(selectedFeatures.size());
    for (const auto& feature : selectedFeatures) {
        auto viewResult = cpuFrame.get_column_view<double>(feature);
        if (!viewResult.ok()) {
            errorMessage = viewResult.status().ToString();
            return false;
        }
        featureViews.emplace_back(viewResult.MoveValueUnsafe());
    }

    features.resize(numRows, static_cast<int>(featureViews.size()));
    for (int64_t row = 0; row < numRows; ++row) {
        for (std::size_t col = 0; col < featureViews.size(); ++col) {
            features(static_cast<int>(row), static_cast<int>(col)) = featureViews[col].data()[row];
        }
    }

    featureNames = selectedFeatures;

    std::vector<int> validRows;
    validRows.reserve(numRows);
    for (int64_t row = 0; row < numRows; ++row) {
        bool valid = true;
        for (std::size_t col = 0; col < featureViews.size(); ++col) {
            if (!std::isfinite(featureViews[col].data()[row])) {
                valid = false;
                break;
            }
        }
        if (valid) {
            validRows.push_back(static_cast<int>(row));
        }
    }

    if (validRows.size() < static_cast<std::size_t>(featureViews.size() + 5)) {
        errorMessage = "Insufficient valid rows after filtering missing values.";
        return false;
    }

    Eigen::MatrixXd filtered(validRows.size(), features.cols());
    for (std::size_t i = 0; i < validRows.size(); ++i) {
        filtered.row(static_cast<int>(i)) = features.row(validRows[i]);
    }
    features = std::move(filtered);

    return true;
}

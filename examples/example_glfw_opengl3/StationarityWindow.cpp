#include "StationarityWindow.h"

#include "TimeSeriesWindow.h"
#include "analytics_dataframe.h"
#include "column_view.h"

#include <algorithm>
#include <arrow/status.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <future>
#include <random>

StationarityWindow::StationarityWindow()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_minSegmentLength(30)
    , m_standardize(false)
    , m_isRunning(false)
    , m_progress(std::make_shared<std::atomic<double>>(0.0))
    , m_hasResult(false)
    , m_hasError(false)
    , m_lastSeriesLength(0) {

    m_statusMessage = "Idle";
    m_featureSelector.SetTargetPrefix("tgt_");
    m_featureSelector.SetShowOnlyTargetsWithPrefix(false);
    m_featureSelector.SetSortAlphabetically(true);
    m_featureSelector.SetAllowMultipleTargets(false);
}

void StationarityWindow::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void StationarityWindow::UpdateColumnList() {
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

void StationarityWindow::ResetResults() {
    m_hasResult = false;
    m_hasError = false;
    m_errorMessage.clear();
    m_selectedColumn.clear();
    m_result = stationarity::MeanBreakResult{};
    m_lastSeriesLength = 0;
}

void StationarityWindow::Draw() {
    if (!m_isVisible) {
        return;
    }

    if (m_isRunning && m_future.valid() &&
        m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_result = m_future.get();
            m_hasResult = m_result.valid;
            m_statusMessage = m_hasResult ? "Analysis complete" : "No significant break detected";
            m_progress->store(1.0);
        } catch (const std::exception& ex) {
            m_hasError = true;
            m_errorMessage = ex.what();
            m_statusMessage = "Analysis failed";
            m_progress->store(1.0);
        }
        m_isRunning = false;
    }

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Stationarity: Break in Mean", &m_isVisible)) {
        ImGui::End();
        return;
    }

    if (!m_dataSource || !m_dataSource->HasData()) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "No data loaded. Please load data in the Time Series window.");
        ImGui::End();
        return;
    }

    ImGui::Columns(2, nullptr, true);
    ImGui::SetColumnWidth(0, 360);

    DrawConfigurationPanel();

    ImGui::NextColumn();
    DrawResultsPanel();

    ImGui::Columns(1);
    DrawStatusBar();

    ImGui::End();
}

void StationarityWindow::DrawConfigurationPanel() {
    ImGui::BeginChild("StationarityConfig", ImVec2(0, 0), true);

    ImGui::Text("Target Series");
    ImGui::Separator();
    m_featureSelector.DrawTargetSelection();

    ImGui::Spacing();
    ImGui::Text("Test Configuration");
    ImGui::Separator();

    ImGui::SliderInt("Min Segment Length", &m_minSegmentLength, 10, 500);
    ImGui::Checkbox("Standardize Series", &m_standardize);

    ImGui::Spacing();
    if (ImGui::Button("Run Break Test", ImVec2(-1, 0)) && !m_isRunning) {
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

void StationarityWindow::DrawResultsPanel() {
    ImGui::BeginChild("StationarityResults", ImVec2(0, -100), true);

    if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
        ImGui::EndChild();
        return;
    }

    if (!m_hasResult) {
        if (!m_isRunning) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Run the test to evaluate stationarity.");
        }
        ImGui::EndChild();
        return;
    }

    ImGui::Text("Analysis Summary");
    ImGui::Separator();

    ImGui::Text("Series: %s", m_selectedColumn.c_str());
    ImGui::Text("Break Index: %d", m_result.breakIndex);
    int seg1 = m_result.breakIndex;
    int seg2 = m_lastSeriesLength - m_result.breakIndex;
    ImGui::Text("Segment lengths: %d before, %d after", seg1, seg2);
    ImGui::Text("Mean before: %.6f", m_result.meanBefore);
    ImGui::Text("Mean after: %.6f", m_result.meanAfter);
    ImGui::Text("Effect size (after - before): %.6f", m_result.effectSize);
    ImGui::Text("F-statistic: %.4f", m_result.fStatistic);
    ImGui::Text("p-value: %.5f", m_result.pValue);

    ImGui::Spacing();
    ImGui::Text("Sum of Squared Errors");
    ImGui::Separator();
    ImGui::Text("Single mean SSE: %.4f", m_result.sseSingle);
    ImGui::Text("Segmented SSE: %.4f", m_result.sseCombined);
    ImGui::Text("Improvement: %.4f", m_result.sseSingle - m_result.sseCombined);

    ImGui::EndChild();
}

void StationarityWindow::DrawStatusBar() {
    ImGui::Separator();
    if (m_isRunning) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), "Running analysis...");
    } else if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
    } else {
        ImGui::Text("%s", m_statusMessage.c_str());
    }
}

void StationarityWindow::StartAnalysis() {
    if (m_isRunning) {
        return;
    }

    Eigen::VectorXd series;
    std::string column;
    std::string error;

    if (!PrepareSeries(series, column, error)) {
        m_hasError = true;
        m_errorMessage = error;
        m_statusMessage = "Failed to prepare data";
        return;
    }

    if (series.size() < 2 * m_minSegmentLength + 1) {
        m_hasError = true;
        m_errorMessage = "Series is too short for the requested minimum segment length.";
        return;
    }

    ResetResults();
    m_selectedColumn = column;
    m_lastSeriesLength = static_cast<int>(series.size());
    m_statusMessage = "Running analysis...";
    m_progress->store(0.0);
    m_isRunning = true;

    stationarity::MeanBreakConfig config;
    config.minSegmentLength = m_minSegmentLength;
    config.standardize = m_standardize;

    uint64_t seed = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto progress = m_progress;

    m_future = std::async(std::launch::async,
                          [config,
                           seriesData = std::move(series),
                           progress,
                           seed]() mutable {
                              stationarity::MeanBreakTest test(config);
                              std::mt19937_64 rng(seed);
                              (void)rng; // RNG not used but reserved for future extensions
                              auto callback = [progress](double fraction) {
                                  if (progress) {
                                      progress->store(fraction);
                                  }
                              };
                              return test.run(seriesData, callback);
                          });
}

bool StationarityWindow::PrepareSeries(Eigen::VectorXd& series,
                                       std::string& columnName,
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

    columnName = m_featureSelector.GetSelectedTarget();
    if (columnName.empty()) {
        errorMessage = "Select a target column.";
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

    auto viewResult = cpuFrame.get_column_view<double>(columnName);
    if (!viewResult.ok()) {
        errorMessage = viewResult.status().ToString();
        return false;
    }

    auto columnView = viewResult.MoveValueUnsafe();
    series.resize(numRows);
    for (int64_t i = 0; i < numRows; ++i) {
        series[static_cast<int>(i)] = columnView.data()[i];
    }

    std::vector<int> validIndices;
    validIndices.reserve(numRows);
    for (int64_t i = 0; i < numRows; ++i) {
        if (std::isfinite(series[static_cast<int>(i)])) {
            validIndices.push_back(static_cast<int>(i));
        }
    }

    if (validIndices.size() < static_cast<std::size_t>(2 * m_minSegmentLength + 1)) {
        errorMessage = "Insufficient valid rows after filtering missing values.";
        return false;
    }

    Eigen::VectorXd filtered(validIndices.size());
    for (std::size_t i = 0; i < validIndices.size(); ++i) {
        filtered[static_cast<int>(i)] = series[validIndices[i]];
    }
    series = std::move(filtered);
    return true;
}

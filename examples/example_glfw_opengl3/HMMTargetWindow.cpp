#include "HMMTargetWindow.h"

#include "TimeSeriesWindow.h"
#include "analytics_dataframe.h"
#include "column_view.h"
#include "hmm/HmmGpu.h"

#include <arrow/status.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>
#include <cmath>

HMMTargetWindow::HMMTargetWindow()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_numStates(3)
    , m_combinationSize(2)
    , m_maxIterations(300)
    , m_numRestarts(4)
    , m_tolerance(1e-5)
    , m_regularization(1e-6)
    , m_mcptReplications(0)
    , m_maxThreads(static_cast<int>(std::max(2u, std::thread::hardware_concurrency())))
    , m_standardize(true)
    , m_useGpu(false)
    , m_isRunning(false)
    , m_progress(std::make_shared<std::atomic<double>>(0.0))
    , m_hasResults(false)
    , m_hasError(false)
    , m_selectedResultIndex(-1) {

    m_statusMessage = "Idle";
    m_featureSelector.SetTargetPrefix("tgt_");
    m_featureSelector.SetShowOnlyTargetsWithPrefix(false);
    m_featureSelector.SetSortAlphabetically(true);
}

void HMMTargetWindow::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void HMMTargetWindow::UpdateColumnList() {
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

void HMMTargetWindow::ResetResults() {
    m_hasResults = false;
    m_hasError = false;
    m_errorMessage.clear();
    m_selectedResultIndex = -1;
    m_results.combinations.clear();
    m_results.mcptReplicationsEvaluated = 1;
}

void HMMTargetWindow::Draw() {
    if (!m_isVisible) {
        return;
    }

    if (m_isRunning && m_future.valid() &&
        m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_results = m_future.get();
            m_hasResults = !m_results.combinations.empty();
            m_statusMessage = m_hasResults ? "Analysis complete" : "No combinations satisfied the criteria";
            m_selectedResultIndex = m_hasResults ? 0 : -1;
            m_progress->store(1.0);
        } catch (const std::exception& ex) {
            m_hasError = true;
            m_errorMessage = ex.what();
            m_statusMessage = "Analysis failed";
            m_progress->store(1.0);
        }
        m_isRunning = false;
    }

    ImGui::SetNextWindowSize(ImVec2(1280, 780), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HMM Target Correlation", &m_isVisible)) {
        ImGui::End();
        return;
    }

    if (!m_dataSource || !m_dataSource->HasData()) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "No data loaded. Please load data in the Time Series window.");
        ImGui::End();
        return;
    }

    ImGui::Columns(2, nullptr, true);
    ImGui::SetColumnWidth(0, 430);

    DrawConfigurationPanel();

    ImGui::NextColumn();
    DrawResultsPanel();

    ImGui::Columns(1);
    DrawStatusBar();

    ImGui::End();
}

void HMMTargetWindow::DrawConfigurationPanel() {
    ImGui::BeginChild("HMM_Config", ImVec2(0, 0), true);

    ImGui::Text("Feature Selection");
    ImGui::Separator();
    m_featureSelector.Draw();

    ImGui::Spacing();
    ImGui::Text("Model Configuration");
    ImGui::Separator();

    ImGui::SliderInt("States", &m_numStates, 2, 6);
    ImGui::SliderInt("Predictors in Combo", &m_combinationSize, 1, 3);
    ImGui::SliderInt("Restarts", &m_numRestarts, 1, 10);
    ImGui::SliderInt("Max Iterations", &m_maxIterations, 50, 2000);
    ImGui::InputDouble("Tolerance", &m_tolerance, 1e-6, 1e-5, "%.2e");
    ImGui::InputDouble("Regularization", &m_regularization, 1e-7, 1e-6, "%.2e");

    ImGui::Spacing();
    ImGui::Text("Computation");
    ImGui::Separator();

    ImGui::SliderInt("MCPT Replications", &m_mcptReplications, 0, 200);
    ImGui::SliderInt("Max Threads", &m_maxThreads, 1, 64);
    ImGui::Checkbox("Standardize Predictors", &m_standardize);

    bool gpuAvailable = hmm::HmmGpuAvailable() &&
                        (m_combinationSize <= hmm::HmmGpuLimits::kMaxFeatures) &&
                        (m_numStates <= hmm::HmmGpuLimits::kMaxStates);
    if (!gpuAvailable) {
        m_useGpu = false;
    }
    ImGui::BeginDisabled(!gpuAvailable);
    if (ImGui::Checkbox("Use GPU (experimental)", &m_useGpu) && !gpuAvailable) {
        m_useGpu = false;
    }
    if (!gpuAvailable && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("CUDA-capable GPU not detected or CUDA runtime unavailable.");
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    bool canRun = !m_isRunning;
    if (ImGui::Button("Run Analysis", ImVec2(-1, 0)) && canRun) {
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

void HMMTargetWindow::DrawResultsPanel() {
    ImGui::BeginChild("HMM_Results", ImVec2(0, -120), true);

    if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
    }

    if (!m_hasResults) {
        if (!m_isRunning) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "No results yet. Configure the analysis and press Run.");
        }
        ImGui::EndChild();
        return;
    }

    if (ImGui::BeginTable("HMMResultsTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY, ImVec2(0, 260))) {
        ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Features", ImGuiTableColumnFlags_WidthStretch, 240);
        ImGui::TableSetupColumn("R^2", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("RMSE", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("p (solo)", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("p (best)", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < m_results.combinations.size(); ++i) {
            const auto& combo = m_results.combinations[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);

            ImGui::TableNextColumn();
            std::ostringstream oss;
            for (std::size_t j = 0; j < combo.featureNames.size(); ++j) {
                if (j > 0) oss << ", ";
                oss << combo.featureNames[j];
            }
            std::string featureLabel = oss.str();
            bool selected = (static_cast<int>(i) == m_selectedResultIndex);
            if (ImGui::Selectable(featureLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedResultIndex = static_cast<int>(i);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.4f", combo.rSquared);

            ImGui::TableNextColumn();
            ImGui::Text("%.4f", combo.rmse);

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", combo.mcptSoloPValue);

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", combo.mcptBestOfPValue);
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    DrawSelectedModelDetails();

    ImGui::EndChild();
}

void HMMTargetWindow::DrawSelectedModelDetails() {
    if (m_selectedResultIndex < 0 || m_selectedResultIndex >= static_cast<int>(m_results.combinations.size())) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Select a model to inspect HMM parameters.");
        return;
    }

    const auto& combo = m_results.combinations[m_selectedResultIndex];
    const hmm::HmmModelParameters& params = combo.hmmFit.parameters;

    ImGui::Text("Selected Combination:");
    ImGui::SameLine();
    for (std::size_t i = 0; i < combo.featureNames.size(); ++i) {
        if (i > 0) ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", combo.featureNames[i].c_str());
        if (i + 1 < combo.featureNames.size()) {
            ImGui::SameLine();
            ImGui::Text("|");
        }
    }

    ImGui::Columns(2, "HMMDetails", false);
    ImGui::Text("Model Metrics");
    ImGui::Separator();
    ImGui::Text("R^2: %.5f", combo.rSquared);
    ImGui::Text("RMSE: %.5f", combo.rmse);
    ImGui::Text("Log Likelihood: %.2f", combo.logLikelihood);
    ImGui::Text("Iterations: %d", combo.hmmFit.iterations);
    ImGui::Text("Converged: %s", combo.hmmFit.converged ? "yes" : "no");
    ImGui::NextColumn();

    ImGui::Text("MCPT Results");
    ImGui::Separator();
    ImGui::Text("Solo count: %d", combo.mcptSoloCount);
    ImGui::Text("Best-of count: %d", combo.mcptBestOfCount);
    ImGui::Text("Replications: %d", m_results.mcptReplicationsEvaluated);

    ImGui::Columns(1);
    ImGui::Separator();

    if (ImGui::TreeNode("Initial Probabilities")) {
        for (int s = 0; s < params.initialProbabilities.size(); ++s) {
            ImGui::Text("State %d: %.4f", s, params.initialProbabilities[s]);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Transition Matrix")) {
        if (ImGui::BeginTable("HMMTransition", params.transitionMatrix.cols(),
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            for (int i = 0; i < params.transitionMatrix.rows(); ++i) {
                ImGui::TableNextRow();
                for (int j = 0; j < params.transitionMatrix.cols(); ++j) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%.4f", params.transitionMatrix(i, j));
                }
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("State Means & StdDev")) {
        for (int state = 0; state < params.means.rows(); ++state) {
            ImGui::Text("State %d", state);
            for (int feature = 0; feature < params.means.cols(); ++feature) {
                double variance = params.covariances[state](feature, feature);
                double stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
                ImGui::BulletText("Feature %d: mean=%.4f  std=%.4f",
                                  feature, params.means(state, feature), stddev);
            }
        }
        ImGui::TreePop();
    }
}

void HMMTargetWindow::DrawStatusBar() {
    ImGui::Separator();
    if (m_isRunning) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), "Running analysis...");
    } else if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
    } else {
        ImGui::Text("%s", m_statusMessage.c_str());
    }
}

void HMMTargetWindow::StartAnalysis() {
    if (m_isRunning) {
        return;
    }

    Eigen::MatrixXd features;
    Eigen::VectorXd targetVec;
    std::vector<std::string> featureNames;
    std::string error;

    if (!PrepareData(features, featureNames, targetVec, error)) {
        m_hasError = true;
        m_errorMessage = error;
        m_statusMessage = "Failed to prepare data";
        return;
    }

    if (featureNames.size() < static_cast<std::size_t>(m_combinationSize)) {
        m_hasError = true;
        m_errorMessage = "Select at least as many features as the combination size.";
        return;
    }

    ResetResults();
    m_statusMessage = "Running analysis...";
    m_progress->store(0.0);
    m_isRunning = true;

    hmm::TargetCorrelationConfig config;
    config.numStates = m_numStates;
    config.combinationSize = m_combinationSize;
    config.maxIterations = m_maxIterations;
    config.numRestarts = m_numRestarts;
    config.tolerance = m_tolerance;
    config.regularization = m_regularization;
    config.mcptReplications = m_mcptReplications;
    config.maxThreads = m_maxThreads;
    config.standardize = m_standardize;
    config.useGpu = m_useGpu;

    uint64_t seed = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto progress = m_progress;

    m_future = std::async(std::launch::async,
                          [config,
                           data = std::move(features),
                           featureNames = std::move(featureNames),
                           targetData = std::move(targetVec),
                           progress,
                           seed]() mutable {
                              hmm::TargetCorrelationAnalyzer analyzer(config);
                              std::mt19937_64 rng(seed);
                              auto callback = [progress](double fraction) {
                                  if (progress) {
                                      progress->store(fraction);
                                  }
                              };
                              return analyzer.analyze(data, featureNames, targetData, rng, callback);
                          });
}

bool HMMTargetWindow::PrepareData(Eigen::MatrixXd& features,
                                  std::vector<std::string>& featureNames,
                                  Eigen::VectorXd& target,
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
    std::string targetName = m_featureSelector.GetSelectedTarget();

    if (selectedFeatures.empty() || targetName.empty()) {
        errorMessage = "Select at least one feature and one target column.";
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
        errorMessage = "No rows available in the dataset.";
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

    auto targetViewResult = cpuFrame.get_column_view<double>(targetName);
    if (!targetViewResult.ok()) {
        errorMessage = targetViewResult.status().ToString();
        return false;
    }
    chronosflow::ColumnView<double> targetView = targetViewResult.MoveValueUnsafe();

    features.resize(numRows, static_cast<int>(featureViews.size()));
    target.resize(numRows);
    featureNames = selectedFeatures;

    for (int row = 0; row < numRows; ++row) {
        target[row] = targetView.data()[row];
        for (std::size_t col = 0; col < featureViews.size(); ++col) {
            features(row, static_cast<int>(col)) = featureViews[col].data()[row];
        }
    }

    std::vector<int> validRows;
    validRows.reserve(numRows);
    for (int row = 0; row < numRows; ++row) {
        bool valid = std::isfinite(target[row]);
        if (!valid) continue;
        for (int col = 0; col < features.cols(); ++col) {
            if (!std::isfinite(features(row, col))) {
                valid = false;
                break;
            }
        }
        if (valid) {
            validRows.push_back(row);
        }
    }

    if (validRows.size() < static_cast<std::size_t>(features.cols() + 5)) {
        errorMessage = "Insufficient valid rows after filtering missing values.";
        return false;
    }

    Eigen::MatrixXd filteredFeatures(validRows.size(), features.cols());
    Eigen::VectorXd filteredTarget(validRows.size());
    for (std::size_t i = 0; i < validRows.size(); ++i) {
        int row = validRows[i];
        filteredTarget[static_cast<int>(i)] = target[row];
        for (int col = 0; col < features.cols(); ++col) {
            filteredFeatures(static_cast<int>(i), col) = features(row, col);
        }
    }

    features = std::move(filteredFeatures);
    target = std::move(filteredTarget);
    return true;
}

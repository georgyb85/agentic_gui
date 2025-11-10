#include "HMMMemoryWindow.h"

#include "TimeSeriesWindow.h"
#include "analytics_dataframe.h"
#include "column_view.h"
#include "hmm/HmmGpu.h"

#include <algorithm>
#include <arrow/status.h>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <thread>

HMMMemoryWindow::HMMMemoryWindow()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_numStates(3)
    , m_maxIterations(300)
    , m_numRestarts(4)
    , m_tolerance(1e-5)
    , m_regularization(1e-6)
    , m_mcptReplications(50)
    , m_maxThreads(static_cast<int>(std::max(2u, std::thread::hardware_concurrency())))
    , m_standardize(true)
    , m_useGpu(false)
    , m_isRunning(false)
    , m_progress(std::make_shared<std::atomic<double>>(0.0))
    , m_hasResults(false)
    , m_hasError(false) {

    m_statusMessage = "Idle";
    m_featureSelector.SetTargetPrefix("tgt_");
    m_featureSelector.SetShowOnlyTargetsWithPrefix(false);
    m_featureSelector.SetSortAlphabetically(true);
}

void HMMMemoryWindow::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void HMMMemoryWindow::UpdateColumnList() {
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

void HMMMemoryWindow::ResetResults() {
    m_hasResults = false;
    m_hasError = false;
    m_errorMessage.clear();
    m_result = hmm::HmmMemoryResult{};
}

void HMMMemoryWindow::Draw() {
    if (!m_isVisible) {
        return;
    }

    if (m_isRunning && m_future.valid() &&
        m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_result = m_future.get();
            m_hasResults = true;
            m_statusMessage = "Analysis complete";
            m_progress->store(1.0);
        } catch (const std::exception& ex) {
            m_hasError = true;
            m_errorMessage = ex.what();
            m_statusMessage = "Analysis failed";
            m_progress->store(1.0);
        }
        m_isRunning = false;
    }

    ImGui::SetNextWindowSize(ImVec2(960, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HMM Memory Test", &m_isVisible)) {
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

void HMMMemoryWindow::DrawConfigurationPanel() {
    ImGui::BeginChild("HMConfig", ImVec2(0, 0), true);

    ImGui::Text("Predictor Selection");
    ImGui::Separator();
    m_featureSelector.DrawFeatureSelection();

    ImGui::Spacing();
    ImGui::Text("Model Configuration");
    ImGui::Separator();

    ImGui::SliderInt("States", &m_numStates, 2, 6);
    ImGui::SliderInt("Restarts", &m_numRestarts, 1, 10);
    ImGui::SliderInt("Max Iterations", &m_maxIterations, 50, 2000);
    ImGui::InputDouble("Tolerance", &m_tolerance, 1e-6, 1e-5, "%.2e");
    ImGui::InputDouble("Regularization", &m_regularization, 1e-7, 1e-6, "%.2e");

    ImGui::Spacing();
    ImGui::Text("Permutation Test");
    ImGui::Separator();
    ImGui::SliderInt("Total Replications", &m_mcptReplications, 1, 200);
    ImGui::SliderInt("Max Threads", &m_maxThreads, 1, 64);
    ImGui::Checkbox("Standardize Predictors", &m_standardize);

    int selectedCount = static_cast<int>(m_featureSelector.GetSelectedFeatures().size());
    bool gpuAvailable = hmm::HmmGpuAvailable() && selectedCount > 0 &&
                        selectedCount <= hmm::HmmGpuLimits::kMaxFeatures &&
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
    if (ImGui::Button("Run Memory Test", ImVec2(-1, 0)) && !m_isRunning) {
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

void HMMMemoryWindow::DrawResultsPanel() {
    ImGui::BeginChild("HMResults", ImVec2(0, -120), true);

    if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
        ImGui::EndChild();
        return;
    }

    if (!m_hasResults) {
        if (!m_isRunning) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Run the memory test to view results.");
        }
        ImGui::EndChild();
        return;
    }

    ImGui::Text("HMM Memory Assessment");
    ImGui::Separator();

    ImGui::Text("Original Log-Likelihood: %.3f", m_result.originalLogLikelihood);
    ImGui::Text("Permutation Mean: %.3f", m_result.meanPermutationLogLikelihood);
    ImGui::Text("Permutation StdDev: %.3f", m_result.stdPermutationLogLikelihood);
    ImGui::Text("Estimated p-value: %.4f", m_result.pValue);

    ImGui::Spacing();
    ImGui::Text("Permutation Log-Likelihoods");
    ImGui::Separator();

    if (ImGui::BeginTable("HMMPerms", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(0, 220))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Log-Likelihood", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < m_result.permutationLogLikelihoods.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", m_result.permutationLogLikelihoods[i]);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Original HMM Parameters");
    ImGui::Separator();

    const auto& params = m_result.originalFit.parameters;
    if (ImGui::TreeNode("Initial Probabilities")) {
        for (int s = 0; s < params.initialProbabilities.size(); ++s) {
            ImGui::Text("State %d: %.4f", s, params.initialProbabilities[s]);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Transition Matrix")) {
        if (ImGui::BeginTable("HMTransition", params.transitionMatrix.cols(),
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

    ImGui::EndChild();
}

void HMMMemoryWindow::DrawStatusBar() {
    ImGui::Separator();
    if (m_isRunning) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), "Running analysis...");
    } else if (m_hasError) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_errorMessage.c_str());
    } else {
        ImGui::Text("%s", m_statusMessage.c_str());
    }
}

void HMMMemoryWindow::StartAnalysis() {
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

    if (featureNames.empty()) {
        m_hasError = true;
        m_errorMessage = "Select at least one predictor.";
        return;
    }

    ResetResults();
    m_statusMessage = "Running analysis...";
    m_progress->store(0.0);
    m_isRunning = true;

    hmm::HmmMemoryConfig config;
    config.numStates = m_numStates;
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
                           progress,
                           seed]() mutable {
                              hmm::HmmMemoryAnalyzer analyzer(config);
                              std::mt19937_64 rng(seed);
                              auto callback = [progress](double fraction) {
                                  if (progress) {
                                      progress->store(fraction);
                                  }
                              };
                              return analyzer.analyze(data, rng, callback);
                          });
}

bool HMMMemoryWindow::PrepareData(Eigen::MatrixXd& features,
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
        errorMessage = "Select at least one predictor column.";
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
    for (int row = 0; row < numRows; ++row) {
        for (std::size_t col = 0; col < featureViews.size(); ++col) {
            features(row, static_cast<int>(col)) = featureViews[col].data()[row];
        }
    }

    featureNames = selectedFeatures;

    std::vector<int> validRows;
    validRows.reserve(numRows);
    for (int row = 0; row < numRows; ++row) {
        bool valid = true;
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

    Eigen::MatrixXd filtered(validRows.size(), features.cols());
    for (std::size_t i = 0; i < validRows.size(); ++i) {
        filtered.row(static_cast<int>(i)) = features.row(validRows[i]);
    }
    features = std::move(filtered);
    return true;
}

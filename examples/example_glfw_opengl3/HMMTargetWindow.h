#pragma once

#include "FeatureSelectorWidget.h"
#include "hmm/HmmTargetCorrelation.h"
#include "imgui.h"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

class TimeSeriesWindow;

class HMMTargetWindow {
public:
    HMMTargetWindow();
    ~HMMTargetWindow() = default;

    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }

    void SetDataSource(const TimeSeriesWindow* dataSource);
    void UpdateColumnList();

private:
    void DrawConfigurationPanel();
    void DrawResultsPanel();
    void DrawStatusBar();
    void DrawSelectedModelDetails();
    void StartAnalysis();

    bool PrepareData(Eigen::MatrixXd& features,
                     std::vector<std::string>& featureNames,
                     Eigen::VectorXd& target,
                     std::string& errorMessage) const;

    void ResetResults();

    bool m_isVisible;
    const TimeSeriesWindow* m_dataSource;

    FeatureSelectorWidget m_featureSelector;
    std::vector<std::string> m_availableColumns;

    // Configuration
    int m_numStates;
    int m_combinationSize;
    int m_maxIterations;
    int m_numRestarts;
    double m_tolerance;
    double m_regularization;
    int m_mcptReplications;
    int m_maxThreads;
    bool m_standardize;
    bool m_useGpu;

    // Execution state
    std::atomic<bool> m_isRunning;
    std::shared_ptr<std::atomic<double>> m_progress;
    std::future<hmm::TargetCorrelationResult> m_future;
    hmm::TargetCorrelationResult m_results;
    bool m_hasResults;
    bool m_hasError;
    std::string m_statusMessage;
    std::string m_errorMessage;
    int m_selectedResultIndex;
};

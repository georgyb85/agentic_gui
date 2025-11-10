#pragma once

#include "FeatureSelectorWidget.h"
#include "fsca/FscaAnalyzer.h"
#include "imgui.h"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

class TimeSeriesWindow;

class FSCAWindow {
public:
    FSCAWindow();
    ~FSCAWindow() = default;

    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }

    void SetDataSource(const TimeSeriesWindow* dataSource);
    void UpdateColumnList();

private:
    void DrawConfigurationPanel();
    void DrawResultsPanel();
    void DrawStatusBar();
    void StartAnalysis();

    bool PrepareData(Eigen::MatrixXd& features,
                     std::vector<std::string>& featureNames,
                     std::string& errorMessage) const;

    void ResetResults();

    bool m_isVisible;
    const TimeSeriesWindow* m_dataSource;

    FeatureSelectorWidget m_featureSelector;
    std::vector<std::string> m_availableColumns;

    int m_numComponents;
    bool m_standardize;

    std::atomic<bool> m_isRunning;
    std::shared_ptr<std::atomic<double>> m_progress;
    std::future<fsca::FscaResult> m_future;
    fsca::FscaResult m_result;
    bool m_hasResult;
    bool m_hasError;
    std::string m_statusMessage;
    std::string m_errorMessage;
    std::vector<std::string> m_selectedFeatureNames;
};

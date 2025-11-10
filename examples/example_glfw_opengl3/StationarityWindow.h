#pragma once

#include "FeatureSelectorWidget.h"
#include "stationarity/MeanBreakTest.h"
#include "imgui.h"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

class TimeSeriesWindow;

class StationarityWindow {
public:
    StationarityWindow();
    ~StationarityWindow() = default;

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

    bool PrepareSeries(Eigen::VectorXd& series,
                       std::string& columnName,
                       std::string& errorMessage) const;

    void ResetResults();

    bool m_isVisible;
    const TimeSeriesWindow* m_dataSource;

    FeatureSelectorWidget m_featureSelector;
    std::vector<std::string> m_availableColumns;

    int m_minSegmentLength;
    bool m_standardize;

    std::atomic<bool> m_isRunning;
    std::shared_ptr<std::atomic<double>> m_progress;
    std::future<stationarity::MeanBreakResult> m_future;
    stationarity::MeanBreakResult m_result;
    bool m_hasResult;
    bool m_hasError;
    std::string m_statusMessage;
    std::string m_errorMessage;
    std::string m_selectedColumn;
    int m_lastSeriesLength;
};

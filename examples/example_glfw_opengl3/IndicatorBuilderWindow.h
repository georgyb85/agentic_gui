#pragma once

#include "imgui.h"
#include <cstdint>
#include <string>
#include <vector>
#include <future>
#include <optional>
#include <chrono>

#include "IndicatorConfig.hpp"
#include "Series.hpp"

class CandlestickChart;

namespace tssb {
    struct IndicatorDefinition;
    struct SingleMarketSeries;
}

class IndicatorBuilderWindow {
public:
    IndicatorBuilderWindow();

    void Draw();

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    void SetCandlestickChart(CandlestickChart* chart) { m_candlestickChart = chart; }

private:
    struct BuildJobResult {
        bool success = false;
        std::string errorMessage;
        size_t rowCount = 0;
        std::vector<std::string> indicatorNames;
        std::vector<std::vector<double>> indicatorValues;
    };

    struct SeriesExtraction {
        tssb::SingleMarketSeries series;
        std::vector<int64_t> timestampsMs;
    };

    enum class BuildState {
        Idle,
        Validating,
        Computing,
        Ready,
        Error
    };

    void DrawScriptSection();
    void DrawResultsSection();
    void DrawStatusBar();
    void DrawDataTable();
    void DrawPlotArea();

    void UpdateDisplayCache();
    void UpdatePlotCache();

    bool ValidateScript(std::string* errorMessage = nullptr);
    bool EnsureDefinitionsReady(std::string* errorMessage);
    std::optional<SeriesExtraction> ExtractSeriesFromOhlcv(std::string* errorMessage) const;
    void BuildTimestampCaches(const std::vector<int64_t>& timestampsMs);

    void BeginBuild();
    void PollBuildFuture();
    void ClearResults();
    void HandleBuildSuccess(BuildJobResult&& result);
    void HandleBuildFailure(const std::string& message);

    bool HasResults() const { return !m_indicatorNames.empty(); }
    size_t GetRowCount() const { return m_currentRowCount; }

    static std::string Trim(const std::string& value);
    static bool IsCommentLine(const std::string& value);
    static std::string FormatDate(int64_t timestampMs);
    static std::string FormatTime(int64_t timestampMs);
    static std::string FormatNumeric(double value);
    static BuildJobResult RunBuildJob(std::vector<tssb::IndicatorDefinition> definitions,
                                      tssb::SingleMarketSeries series);
    bool HasOhlcvData() const;

    CandlestickChart* m_candlestickChart;
    bool m_visible;

    std::string m_scriptText;
    std::string m_lastValidatedScript;
    std::vector<tssb::IndicatorDefinition> m_parsedDefinitions;
    bool m_lastValidationSuccess;
    std::string m_validationStatus;

    BuildState m_buildState;
    std::string m_statusMessage;
    bool m_statusIsError;
    double m_lastBuildDurationMs;
    std::future<BuildJobResult> m_buildFuture;
    std::chrono::steady_clock::time_point m_buildStartTime;

    static constexpr int kMetadataColumns = 3;
    static constexpr int kMaxDisplayRows = 250;

    std::vector<std::string> m_columnHeaders;
    std::vector<std::vector<std::string>> m_displayCache;
    std::vector<std::string> m_indicatorNames;
    std::vector<std::vector<double>> m_indicatorValues;
    size_t m_currentRowCount;
    std::vector<int64_t> m_timestampMs;
    std::vector<double> m_timestampSeconds;

    int m_selectedColumnIndex;
    std::string m_selectedIndicator;
    bool m_autoFitPlot;
    float m_tableHeight;
    float m_plotHeight;
    ImGuiTableFlags m_tableFlags;

    std::vector<double> m_plotTimes;
    std::vector<double> m_plotValues;
};

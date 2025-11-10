#pragma once

#include "Stage1RestClient.h"

#include <array>
#include <string>
#include <vector>

class Stage1ServerWindow {
public:
    Stage1ServerWindow();
    void Draw();
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }

private:
    void ApplyApiSettings();
    void EnsureClientConfig();
    void PingApi();
    void RefreshDatasets();
    void RefreshRuns();
    void LoadRunDetail();
    void RefreshJobs();
    void RefreshMeasurements();
    void ExecuteQuestDbQuery(const std::string& overrideSql = std::string());
    void PreviewMeasurement();
    void RenderStatusLine(const std::string& message, bool success);
    void RenderResultTable(const std::vector<std::string>& columns,
                           const std::vector<std::vector<std::string>>& rows,
                           const char* tableId,
                           float height);
    void RenderDatasetTable();
    void RenderRunsTable();
    void RenderJobsTable();
    void RenderMeasurementsTable();

    bool m_visible = false;

    std::array<char, 256> m_apiUrlBuffer{};
    std::array<char, 256> m_apiTokenBuffer{};
    std::array<char, 64> m_datasetIdBuffer{};
    std::array<char, 64> m_runIdBuffer{};
    std::array<char, 2048> m_qdbQueryBuffer{};
    std::array<char, 128> m_measurementBuffer{};

    std::string m_apiStatusMessage;
    bool m_apiStatusSuccess = true;

    std::vector<stage1::DatasetSummary> m_datasetSummaries;
    std::vector<std::string> m_datasetColumns;
    std::vector<std::vector<std::string>> m_datasetRows;
    int m_selectedDatasetIndex = -1;
    std::string m_datasetStatusMessage;
    bool m_datasetStatusSuccess = true;

    std::vector<stage1::RunSummary> m_runSummaries;
    std::vector<std::string> m_runColumns;
    std::vector<std::vector<std::string>> m_runRows;
    int m_selectedRunIndex = -1;
    std::string m_runStatusMessage;
    bool m_runStatusSuccess = true;

    std::string m_runDetailText;
    std::string m_runDetailStatusMessage;
    bool m_runDetailStatusSuccess = true;

    std::vector<stage1::JobStatus> m_jobEntries;
    std::vector<std::string> m_jobColumns;
    std::vector<std::vector<std::string>> m_jobRows;
    std::string m_jobStatusMessage;
    bool m_jobStatusSuccess = true;

    std::vector<stage1::MeasurementInfo> m_measurements;
    std::string m_measurementStatusMessage;
    bool m_measurementStatusSuccess = true;

    std::vector<std::string> m_qdbColumns;
    std::vector<std::vector<std::string>> m_qdbRows;
    std::string m_qdbStatusMessage;
    bool m_qdbStatusSuccess = true;
};

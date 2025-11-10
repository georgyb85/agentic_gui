#include "Stage1ServerWindow.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {

void FillDefault(char* buffer, size_t bufferSize, const char* value) {
    if (!buffer || bufferSize == 0) {
        return;
    }
    std::snprintf(buffer, bufferSize, "%s", value ? value : "");
}

std::string FormatInt64(int64_t value) {
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "%lld", static_cast<long long>(value));
    return tmp;
}

std::string FormatProgress(const stage1::JobStatus& job) {
    if (job.total <= 0) {
        return "-";
    }
    double pct = static_cast<double>(job.progress) / static_cast<double>(job.total) * 100.0;
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "%lld/%lld (%.1f%%)",
                  static_cast<long long>(job.progress),
                  static_cast<long long>(job.total),
                  pct);
    return tmp;
}

} // namespace

Stage1ServerWindow::Stage1ServerWindow() {
    stage1::RestClient& api = stage1::RestClient::Instance();
    FillDefault(m_apiUrlBuffer.data(), m_apiUrlBuffer.size(), api.GetBaseUrl().c_str());
    FillDefault(m_apiTokenBuffer.data(), m_apiTokenBuffer.size(), api.GetApiToken().c_str());
    FillDefault(m_datasetIdBuffer.data(), m_datasetIdBuffer.size(), "");
    FillDefault(m_runIdBuffer.data(), m_runIdBuffer.size(), "");
    FillDefault(m_measurementBuffer.data(), m_measurementBuffer.size(), "");
    FillDefault(m_qdbQueryBuffer.data(), m_qdbQueryBuffer.size(),
                "SELECT timestamp_unix, open, high, low, close FROM measurement LIMIT 50;");

    m_datasetColumns = {"Slug", "Dataset ID", "Symbol", "OHLCV Table", "Indicator Table",
                        "OHLCV Rows", "Indicator Rows", "Runs", "Simulations", "Updated"};
    m_runColumns = {"Run ID", "Measurement", "Status", "Started", "Completed"};
    m_jobColumns = {"Job ID", "Type", "Status", "Progress", "Message", "Updated"};
}

void Stage1ServerWindow::Draw() {
    if (!m_visible) {
        return;
    }

    if (!ImGui::Begin("Stage1 Server Debugger", &m_visible)) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("API Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Base URL", m_apiUrlBuffer.data(), m_apiUrlBuffer.size());
        ImGui::InputText("API Token", m_apiTokenBuffer.data(), m_apiTokenBuffer.size(), ImGuiInputTextFlags_Password);
        if (ImGui::Button("Apply")) {
            ApplyApiSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ping API")) {
            PingApi();
        }
        RenderStatusLine(m_apiStatusMessage, m_apiStatusSuccess);
    }

    if (ImGui::CollapsingHeader("Datasets & Runs", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Refresh Datasets")) {
            RefreshDatasets();
        }
        ImGui::SameLine();
        if (ImGui::Button("List Runs")) {
            RefreshRuns();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Run Detail")) {
            LoadRunDetail();
        }

        ImGui::InputText("Dataset ID", m_datasetIdBuffer.data(), m_datasetIdBuffer.size());
        ImGui::InputText("Run ID", m_runIdBuffer.data(), m_runIdBuffer.size());

        RenderStatusLine(m_datasetStatusMessage, m_datasetStatusSuccess);
        RenderDatasetTable();
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::TextUnformatted("Runs");
        RenderStatusLine(m_runStatusMessage, m_runStatusSuccess);
        RenderRunsTable();

        ImGui::TextUnformatted("Run Detail");
        RenderStatusLine(m_runDetailStatusMessage, m_runDetailStatusSuccess);
        if (m_runDetailText.empty()) {
            ImGui::TextDisabled("No run loaded.");
        } else {
            ImGui::BeginChild("stage1-run-detail", ImVec2(0, 160.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(m_runDetailText.c_str());
            ImGui::EndChild();
        }
    }

    if (ImGui::CollapsingHeader("QuestDB", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Run Query")) {
            ExecuteQuestDbQuery();
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh Measurements")) {
            RefreshMeasurements();
        }
        ImGui::InputTextMultiline("SQL", m_qdbQueryBuffer.data(), m_qdbQueryBuffer.size(),
                                  ImVec2(-FLT_MIN, 120.0f));

        ImGui::InputText("Measurement", m_measurementBuffer.data(), m_measurementBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Preview Measurement")) {
            PreviewMeasurement();
        }

        RenderStatusLine(m_measurementStatusMessage, m_measurementStatusSuccess);
        RenderMeasurementsTable();

        RenderStatusLine(m_qdbStatusMessage, m_qdbStatusSuccess);
        RenderResultTable(m_qdbColumns, m_qdbRows, "stage1-qdb-table", 220.0f);
    }

    if (ImGui::CollapsingHeader("Jobs", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Refresh Jobs")) {
            RefreshJobs();
        }
        RenderStatusLine(m_jobStatusMessage, m_jobStatusSuccess);
        RenderJobsTable();
    }

    ImGui::End();
}

void Stage1ServerWindow::ApplyApiSettings() {
    EnsureClientConfig();
    m_apiStatusSuccess = true;
    m_apiStatusMessage = "Stage1 API settings applied.";
}

void Stage1ServerWindow::EnsureClientConfig() {
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::string url = m_apiUrlBuffer.data();
    if (!url.empty() && url != api.GetBaseUrl()) {
        api.SetBaseUrl(url);
    }
    std::string token = m_apiTokenBuffer.data();
    if (token != api.GetApiToken()) {
        api.SetApiToken(token);
    }
}

void Stage1ServerWindow::PingApi() {
    EnsureClientConfig();
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::string payload;
    std::string error;
    if (api.GetHealth(&payload, &error)) {
        m_apiStatusSuccess = true;
        m_apiStatusMessage = payload.empty() ? "API healthy." : payload;
    } else {
        m_apiStatusSuccess = false;
        m_apiStatusMessage = error;
    }
}

void Stage1ServerWindow::RefreshDatasets() {
    EnsureClientConfig();
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::vector<stage1::DatasetSummary> remote;
    std::string error;
    if (!api.FetchDatasets(100, 0, &remote, &error)) {
        m_datasetStatusSuccess = false;
        m_datasetStatusMessage = error;
        m_datasetRows.clear();
        return;
    }
    m_datasetSummaries = remote;
    m_datasetRows.clear();
    for (const auto& summary : remote) {
        std::vector<std::string> row;
        row.reserve(m_datasetColumns.size());
        row.push_back(summary.dataset_slug);
        row.push_back(summary.dataset_id);
        row.push_back(summary.symbol);
        row.push_back(summary.ohlcv_measurement);
        row.push_back(summary.indicator_measurement);
        row.push_back(FormatInt64(summary.ohlcv_row_count));
        row.push_back(FormatInt64(summary.indicator_row_count));
        row.push_back(FormatInt64(summary.run_count));
        row.push_back(FormatInt64(summary.simulation_count));
        row.push_back(summary.updated_at);
        m_datasetRows.push_back(std::move(row));
    }
    if (m_selectedDatasetIndex >= static_cast<int>(m_datasetSummaries.size())) {
        m_selectedDatasetIndex = -1;
    }
    m_datasetStatusSuccess = true;
    std::ostringstream oss;
    oss << "Loaded " << m_datasetSummaries.size() << " dataset(s).";
    m_datasetStatusMessage = oss.str();
}

void Stage1ServerWindow::RefreshRuns() {
    EnsureClientConfig();
    std::string datasetId = m_datasetIdBuffer.data();
    if (datasetId.empty() && m_selectedDatasetIndex >= 0 &&
        m_selectedDatasetIndex < static_cast<int>(m_datasetSummaries.size())) {
        datasetId = m_datasetSummaries[static_cast<size_t>(m_selectedDatasetIndex)].dataset_id;
        FillDefault(m_datasetIdBuffer.data(), m_datasetIdBuffer.size(), datasetId.c_str());
    }
    if (datasetId.empty()) {
        m_runStatusSuccess = false;
        m_runStatusMessage = "Dataset ID is required.";
        return;
    }
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::vector<stage1::RunSummary> remote;
    std::string error;
    if (!api.FetchDatasetRuns(datasetId, 200, 0, &remote, &error)) {
        m_runStatusSuccess = false;
        m_runStatusMessage = error;
        m_runRows.clear();
        return;
    }
    m_runSummaries = remote;
    m_runRows.clear();
    for (const auto& run : remote) {
        std::vector<std::string> row;
        row.reserve(m_runColumns.size());
        row.push_back(run.run_id);
        row.push_back(run.prediction_measurement);
        row.push_back(run.status);
        row.push_back(run.started_at);
        row.push_back(run.completed_at);
        m_runRows.push_back(std::move(row));
    }
    if (m_selectedRunIndex >= static_cast<int>(m_runSummaries.size())) {
        m_selectedRunIndex = -1;
    }
    m_runStatusSuccess = true;
    std::ostringstream oss;
    oss << "Loaded " << m_runSummaries.size() << " run(s).";
    m_runStatusMessage = oss.str();
}

void Stage1ServerWindow::LoadRunDetail() {
    EnsureClientConfig();
    std::string runId = m_runIdBuffer.data();
    if (runId.empty() && m_selectedRunIndex >= 0 &&
        m_selectedRunIndex < static_cast<int>(m_runSummaries.size())) {
        runId = m_runSummaries[static_cast<size_t>(m_selectedRunIndex)].run_id;
        FillDefault(m_runIdBuffer.data(), m_runIdBuffer.size(), runId.c_str());
    }
    if (runId.empty()) {
        m_runDetailStatusSuccess = false;
        m_runDetailStatusMessage = "Run ID is required.";
        return;
    }
    stage1::RestClient& api = stage1::RestClient::Instance();
    stage1::RunDetail detail;
    std::string error;
    if (!api.FetchRunDetail(runId, &detail, &error)) {
        m_runDetailStatusSuccess = false;
        m_runDetailStatusMessage = error;
        m_runDetailText.clear();
        return;
    }

    std::ostringstream oss;
    oss << "Run ID: " << detail.run_id << "\n";
    oss << "Dataset: " << detail.dataset_slug << " (" << detail.dataset_id << ")\n";
    oss << "Measurement: " << detail.prediction_measurement << "\n";
    oss << "Status: " << detail.status << "\n";
    oss << "Started: " << detail.started_at << "\n";
    oss << "Completed: " << detail.completed_at << "\n";
    oss << "Target: " << detail.target_column << "\n";
    oss << "Features (" << detail.feature_columns.size() << "): ";
    for (size_t i = 0; i < detail.feature_columns.size(); ++i) {
        oss << detail.feature_columns[i];
        if (i + 1 < detail.feature_columns.size()) {
            oss << ", ";
        }
    }
    oss << "\n\nFolds:\n";
    for (const auto& fold : detail.folds) {
        oss << "  Fold " << fold.fold_number
            << ": train=" << fold.train_start << "-" << fold.train_end
            << ", test=" << fold.test_start << "-" << fold.test_end
            << ", hit_rate=" << fold.hit_rate
            << ", profit_factor=" << fold.profit_factor_test << "\n";
    }

    m_runDetailText = oss.str();
    m_runDetailStatusSuccess = true;
    m_runDetailStatusMessage = "Loaded run detail.";
}

void Stage1ServerWindow::RefreshJobs() {
    EnsureClientConfig();
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::vector<stage1::JobStatus> jobs;
    std::string error;
    if (!api.FetchJobs(100, 0, &jobs, &error)) {
        m_jobStatusSuccess = false;
        m_jobStatusMessage = error;
        m_jobRows.clear();
        return;
    }
    m_jobEntries = jobs;
    m_jobRows.clear();
    for (const auto& job : jobs) {
        std::vector<std::string> row;
        row.reserve(m_jobColumns.size());
        row.push_back(job.job_id);
        row.push_back(job.job_type);
        row.push_back(job.status);
        row.push_back(FormatProgress(job));
        row.push_back(job.message);
        row.push_back(job.updated_at);
        m_jobRows.push_back(std::move(row));
    }
    m_jobStatusSuccess = true;
    std::ostringstream oss;
    oss << "Loaded " << m_jobEntries.size() << " job(s).";
    m_jobStatusMessage = oss.str();
}

void Stage1ServerWindow::RefreshMeasurements() {
    EnsureClientConfig();
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::vector<stage1::MeasurementInfo> list;
    std::string error;
    if (!api.ListMeasurements("", &list, &error)) {
        m_measurementStatusSuccess = false;
        m_measurementStatusMessage = error;
        m_measurements.clear();
        return;
    }
    m_measurements = list;
    m_measurementStatusSuccess = true;
    std::ostringstream oss;
    oss << "Loaded " << m_measurements.size() << " measurement(s).";
    m_measurementStatusMessage = oss.str();
}

void Stage1ServerWindow::ExecuteQuestDbQuery(const std::string& overrideSql) {
    EnsureClientConfig();
    const char* sqlPtr = overrideSql.empty() ? m_qdbQueryBuffer.data() : overrideSql.c_str();
    if (!sqlPtr || std::strlen(sqlPtr) == 0) {
        m_qdbStatusSuccess = false;
        m_qdbStatusMessage = "SQL query cannot be empty.";
        return;
    }
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    std::string error;
    if (!api.QuestDbQuery(sqlPtr, &columns, &rows, &error)) {
        m_qdbStatusSuccess = false;
        m_qdbStatusMessage = error;
        m_qdbColumns.clear();
        m_qdbRows.clear();
        return;
    }
    m_qdbColumns = std::move(columns);
    m_qdbRows = std::move(rows);
    m_qdbStatusSuccess = true;
    std::ostringstream oss;
    oss << "Retrieved " << m_qdbRows.size() << " row(s).";
    m_qdbStatusMessage = oss.str();
}

void Stage1ServerWindow::PreviewMeasurement() {
    if (m_measurementBuffer[0] == '\0') {
        m_qdbStatusSuccess = false;
        m_qdbStatusMessage = "Measurement name is required.";
        return;
    }
    std::ostringstream sql;
    sql << "SELECT * FROM \"" << m_measurementBuffer.data() << "\" LIMIT 200;";
    ExecuteQuestDbQuery(sql.str());
}

void Stage1ServerWindow::RenderStatusLine(const std::string& message, bool success) {
    if (message.empty()) {
        return;
    }
    const ImVec4 color = success ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
    ImGui::TextColored(color, "%s", message.c_str());
}

void Stage1ServerWindow::RenderResultTable(const std::vector<std::string>& columns,
                                           const std::vector<std::vector<std::string>>& rows,
                                           const char* tableId,
                                           float height) {
    if (columns.empty()) {
        ImGui::TextDisabled("No results.");
        return;
    }
    if (ImGui::BeginTable(tableId, static_cast<int>(columns.size()),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, height))) {
        for (const auto& name : columns) {
            ImGui::TableSetupColumn(name.c_str());
        }
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow();
                const auto& rowData = rows[static_cast<size_t>(row)];
                for (int col = 0; col < static_cast<int>(columns.size()); ++col) {
                    ImGui::TableSetColumnIndex(col);
                    const std::string& value = (col < static_cast<int>(rowData.size())) ? rowData[col] : std::string();
                    ImGui::TextUnformatted(value.c_str());
                }
            }
        }
        ImGui::EndTable();
    }
}

void Stage1ServerWindow::RenderDatasetTable() {
    if (m_datasetRows.empty()) {
        ImGui::TextDisabled("No datasets loaded.");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("stage1-datasets", static_cast<int>(m_datasetColumns.size()), flags, ImVec2(0, 200.0f))) {
        for (const auto& col : m_datasetColumns) {
            ImGui::TableSetupColumn(col.c_str());
        }
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_datasetRows.size()));
        while (clipper.Step()) {
            for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
                ImGui::TableNextRow();
                const auto& row = m_datasetRows[static_cast<size_t>(rowIdx)];
                for (int colIdx = 0; colIdx < static_cast<int>(row.size()); ++colIdx) {
                    ImGui::TableSetColumnIndex(colIdx);
                    if (colIdx == 0) {
                        bool selected = (m_selectedDatasetIndex == rowIdx);
                        ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
                        if (ImGui::Selectable(row[colIdx].c_str(), selected, selectFlags)) {
                            m_selectedDatasetIndex = rowIdx;
                            const auto& summary = m_datasetSummaries[static_cast<size_t>(rowIdx)];
                            FillDefault(m_datasetIdBuffer.data(), m_datasetIdBuffer.size(), summary.dataset_id.c_str());
                        }
                    } else {
                        ImGui::TextUnformatted(row[colIdx].c_str());
                    }
                }
            }
        }
        ImGui::EndTable();
    }
}

void Stage1ServerWindow::RenderRunsTable() {
    if (m_runRows.empty()) {
        ImGui::TextDisabled("No runs loaded.");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("stage1-runs", static_cast<int>(m_runColumns.size()), flags, ImVec2(0, 200.0f))) {
        for (const auto& col : m_runColumns) {
            ImGui::TableSetupColumn(col.c_str());
        }
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_runRows.size()));
        while (clipper.Step()) {
            for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
                ImGui::TableNextRow();
                const auto& row = m_runRows[static_cast<size_t>(rowIdx)];
                for (int colIdx = 0; colIdx < static_cast<int>(row.size()); ++colIdx) {
                    ImGui::TableSetColumnIndex(colIdx);
                    if (colIdx == 0) {
                        bool selected = (m_selectedRunIndex == rowIdx);
                        ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
                        if (ImGui::Selectable(row[colIdx].c_str(), selected, selectFlags)) {
                            m_selectedRunIndex = rowIdx;
                            const auto& summary = m_runSummaries[static_cast<size_t>(rowIdx)];
                            FillDefault(m_runIdBuffer.data(), m_runIdBuffer.size(), summary.run_id.c_str());
                        }
                    } else {
                        ImGui::TextUnformatted(row[colIdx].c_str());
                    }
                }
            }
        }
        ImGui::EndTable();
    }
}

void Stage1ServerWindow::RenderJobsTable() {
    if (m_jobRows.empty()) {
        ImGui::TextDisabled("No jobs loaded.");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("stage1-jobs", static_cast<int>(m_jobColumns.size()), flags, ImVec2(0, 200.0f))) {
        for (const auto& col : m_jobColumns) {
            ImGui::TableSetupColumn(col.c_str());
        }
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_jobRows.size()));
        while (clipper.Step()) {
            for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
                ImGui::TableNextRow();
                const auto& row = m_jobRows[static_cast<size_t>(rowIdx)];
                for (int colIdx = 0; colIdx < static_cast<int>(row.size()); ++colIdx) {
                    ImGui::TableSetColumnIndex(colIdx);
                    ImGui::TextUnformatted(row[colIdx].c_str());
                }
            }
        }
        ImGui::EndTable();
    }
}

void Stage1ServerWindow::RenderMeasurementsTable() {
    if (m_measurements.empty()) {
        ImGui::TextDisabled("No measurements loaded.");
        return;
    }
    if (ImGui::BeginTable("stage1-measurements", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 180.0f))) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Rows");
        ImGui::TableSetupColumn("Partition");
        ImGui::TableSetupColumn("First");
        ImGui::TableSetupColumn("Last");
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_measurements.size()));
        while (clipper.Step()) {
            for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
                ImGui::TableNextRow();
                const auto& measurement = m_measurements[static_cast<size_t>(rowIdx)];
                ImGui::TableSetColumnIndex(0);
                bool selected = (std::strcmp(m_measurementBuffer.data(), measurement.name.c_str()) == 0);
                if (ImGui::Selectable(measurement.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    FillDefault(m_measurementBuffer.data(), m_measurementBuffer.size(), measurement.name.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(FormatInt64(measurement.row_count).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(measurement.partition_by.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(measurement.first_ts.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(measurement.last_ts.c_str());
            }
        }
        ImGui::EndTable();
    }
}

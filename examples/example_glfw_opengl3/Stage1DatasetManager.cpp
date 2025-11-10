#include "Stage1DatasetManager.h"

#include "Stage1RestClient.h"
#include "TimeSeriesWindow.h"
#include "candlestick_chart.h"
#include "stage1_metadata_writer.h"
#include "analytics_dataframe.h"
#include "imgui.h"
#include <arrow/csv/writer.h>
#include <arrow/io/api.h>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <cctype>
#include <thread>

namespace {

std::string ToDisplay(const std::string& value) {
    if (value.empty() || value == "NULL") {
        return "-";
    }
    return value;
}

bool DataFrameToCsv(const chronosflow::AnalyticsDataFrame& frame,
                    std::string* csvOut,
                    std::string* error) {
    if (!csvOut) {
        if (error) *error = "CSV output buffer is null.";
        return false;
    }
    auto table = frame.get_cpu_table();
    if (!table) {
        if (error) *error = "AnalyticsDataFrame does not contain a CPU table.";
        return false;
    }
    auto maybeStream = arrow::io::BufferOutputStream::Create();
    if (!maybeStream.ok()) {
        if (error) *error = maybeStream.status().ToString();
        return false;
    }
    std::shared_ptr<arrow::io::BufferOutputStream> stream = *maybeStream;
    auto options = arrow::csv::WriteOptions::Defaults();
    options.include_header = true;
    auto status = arrow::csv::WriteCSV(*table, options, stream.get());
    if (!status.ok()) {
        if (error) *error = status.ToString();
        return false;
    }
    auto maybeBuffer = stream->Finish();
    if (!maybeBuffer.ok()) {
        if (error) *error = maybeBuffer.status().ToString();
        return false;
    }
    auto buffer = *maybeBuffer;
    csvOut->assign(reinterpret_cast<const char*>(buffer->data()), buffer->size());
    return true;
}

} // namespace

Stage1DatasetManager::Stage1DatasetManager() {
    m_datasetSlug[0] = '\0';
    m_indicatorMeasurementBuffer[0] = '\0';
    m_ohlcvMeasurementBuffer[0] = '\0';
}

bool Stage1DatasetManager::HasIndicatorData() const {
    return m_timeSeriesWindow && m_timeSeriesWindow->HasData();
}

bool Stage1DatasetManager::HasOhlcvData() const {
    return m_candlestickChart && m_candlestickChart->GetAnalyticsDataFrame();
}

std::string Stage1DatasetManager::SanitizeSlug(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    std::string slug;
    slug.reserve(value.size());
    bool lastUnderscore = false;
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            lastUnderscore = false;
        } else {
            if (!lastUnderscore) {
                slug.push_back('_');
                lastUnderscore = true;
            }
        }
    }
    while (!slug.empty() && slug.back() == '_') {
        slug.pop_back();
    }
    if (!slug.empty() && slug.front() == '_') {
        slug.erase(slug.begin());
    }
    return slug;
}

void Stage1DatasetManager::Draw() {
    if (!m_visible) {
        return;
    }

    if (!ImGui::Begin("Stage1 Dataset Manager", &m_visible)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Refresh") || m_refreshPending) {
        RefreshRows();
        m_refreshPending = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Selected")) {
        LoadSelectedDataset();
    }

    ImGui::Separator();
    ImGui::Text("Export Dataset");
    ImGui::SetNextItemWidth(220.0f);
    bool slugChanged = ImGui::InputText("Slug", m_datasetSlug, sizeof(m_datasetSlug));

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("OHLCV Measurement", m_ohlcvMeasurementBuffer, sizeof(m_ohlcvMeasurementBuffer));
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Indicator Measurement", m_indicatorMeasurementBuffer, sizeof(m_indicatorMeasurementBuffer));

    if (slugChanged && std::strlen(m_datasetSlug) > 0) {
        if (m_indicatorMeasurementBuffer[0] == '\0') {
            std::snprintf(m_indicatorMeasurementBuffer, sizeof(m_indicatorMeasurementBuffer), "%s_ind", m_datasetSlug);
        }
        if (m_ohlcvMeasurementBuffer[0] == '\0') {
            std::snprintf(m_ohlcvMeasurementBuffer, sizeof(m_ohlcvMeasurementBuffer), "%s_ohlcv", m_datasetSlug);
        }
    }

    const bool canExport = HasIndicatorData() && HasOhlcvData();
    ImGui::BeginDisabled(!canExport);
    if (ImGui::Button("Export Current Data")) {
        ExportCurrentDataset();
    }
    ImGui::EndDisabled();
    if (!canExport) {
        ImGui::SameLine();
        ImGui::TextDisabled("(Load both OHLCV + indicator data first)");
    }

    if (!m_statusMessage.empty()) {
        const ImVec4 color = m_statusSuccess
            ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
            : ImVec4(0.9f, 0.4f, 0.2f, 1.0f);
        m_statusBuffer.assign(m_statusMessage.begin(), m_statusMessage.end());
        m_statusBuffer.push_back('\0');
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.08f, 0.5f));
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll | ImGuiInputTextFlags_AllowTabInput;
        ImGui::InputTextMultiline("##stage1-dataset-status", m_statusBuffer.data(), static_cast<int>(m_statusBuffer.size()), ImVec2(-1, 100.0f), flags);
        ImGui::PopStyleColor(2);
    }

    if (ImGui::BeginTable("DatasetTable", 8,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Slug", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("OHLCV Table", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Indicator Table", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("OHLCV Rows", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Indicator Rows", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Runs", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Sims", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Updated", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_rows.size()));
        while (clipper.Step()) {
            for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
                const auto& row = m_rows[rowIdx];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool isSelected = (m_selectedIndex == rowIdx);
                if (ImGui::Selectable(row.dataset_slug.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    m_selectedIndex = rowIdx;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("Dataset ID: %s", row.dataset_id.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ToDisplay(row.ohlcv_measurement).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ToDisplay(row.indicator_measurement).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(row.ohlcv_rows));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(row.indicator_rows));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(row.run_count));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(row.simulation_count));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ToDisplay(row.updated_at).c_str());
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

void Stage1DatasetManager::RefreshRows() {
    stage1::RestClient& api = stage1::RestClient::Instance();
    std::vector<stage1::DatasetSummary> datasets;
    std::string error;
    if (!api.FetchDatasets(200, 0, &datasets, &error)) {
        m_statusMessage = error.empty() ? "Failed to load datasets from Stage1 API." : error;
        m_statusSuccess = false;
        m_rows.clear();
        return;
    }

    m_rows.clear();
    m_rows.reserve(datasets.size());
    for (const auto& summary : datasets) {
        DatasetRow parsed;
        parsed.dataset_id = summary.dataset_id;
        parsed.dataset_slug = summary.dataset_slug;
        parsed.symbol = summary.symbol;
        parsed.granularity = summary.granularity;
        parsed.ohlcv_measurement = summary.ohlcv_measurement;
        parsed.indicator_measurement = summary.indicator_measurement;
        parsed.ohlcv_rows = summary.ohlcv_row_count;
        parsed.indicator_rows = summary.indicator_row_count;
        parsed.ohlcv_first_ts = summary.ohlcv_first_ts;
        parsed.ohlcv_last_ts = summary.ohlcv_last_ts;
        parsed.indicator_first_ts = summary.indicator_first_ts;
        parsed.indicator_last_ts = summary.indicator_last_ts;
        parsed.run_count = summary.run_count;
        parsed.simulation_count = summary.simulation_count;
        parsed.updated_at = summary.updated_at;
        m_rows.push_back(std::move(parsed));
    }

    if (m_rows.empty()) {
        m_statusMessage = "No datasets available on Stage1.";
    } else {
        m_statusMessage = "Retrieved " + std::to_string(m_rows.size()) + " dataset(s).";
    }
    m_statusSuccess = true;
    if (m_selectedIndex >= static_cast<int>(m_rows.size())) {
        m_selectedIndex = -1;
    }
}

void Stage1DatasetManager::LoadSelectedDataset() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_rows.size())) {
        m_statusMessage = "Select a dataset first.";
        m_statusSuccess = false;
        return;
    }
    if (!m_timeSeriesWindow) {
        m_statusMessage = "Time Series window is not available.";
        m_statusSuccess = false;
        return;
    }
    const auto& row = m_rows[m_selectedIndex];
    DatasetMetadata metadata;
    metadata.dataset_id = row.dataset_id;
    metadata.dataset_slug = row.dataset_slug;
    metadata.indicator_measurement = row.indicator_measurement;
    metadata.ohlcv_measurement = row.ohlcv_measurement;
    metadata.indicator_rows = row.indicator_rows;
    metadata.ohlcv_rows = row.ohlcv_rows;

    bool ohlcvLoaded = true;
    if (m_candlestickChart && !row.ohlcv_measurement.empty()) {
        std::string qdbStatus;
        if (!m_candlestickChart->LoadFromQuestDb(row.ohlcv_measurement, &qdbStatus)) {
            ohlcvLoaded = false;
            std::string measurementLabel = row.ohlcv_measurement.empty() ? "<unspecified>" : row.ohlcv_measurement;
            m_statusMessage = "Failed to load OHLCV measurement '" + measurementLabel + "'.";
            if (!qdbStatus.empty()) {
                m_statusMessage += " " + qdbStatus;
            }
            m_statusSuccess = false;
        }
    }

    m_timeSeriesWindow->LoadDatasetFromMetadata(metadata);
    std::snprintf(m_datasetSlug, sizeof(m_datasetSlug), "%s", row.dataset_slug.c_str());
    std::snprintf(m_indicatorMeasurementBuffer, sizeof(m_indicatorMeasurementBuffer), "%s",
                  row.indicator_measurement.c_str());
    std::snprintf(m_ohlcvMeasurementBuffer, sizeof(m_ohlcvMeasurementBuffer), "%s",
                  row.ohlcv_measurement.c_str());
    if (ohlcvLoaded) {
        m_statusMessage = "Loaded dataset " + row.dataset_slug + ".";
        m_statusSuccess = true;
    }
}

void Stage1DatasetManager::ExportCurrentDataset() {
    if (!m_timeSeriesWindow || !m_candlestickChart) {
        m_statusMessage = "OHLCV or indicator window unavailable.";
        m_statusSuccess = false;
        return;
    }
    if (!HasIndicatorData()) {
        m_statusMessage = "Load indicator data before exporting.";
        m_statusSuccess = false;
        return;
    }
    if (!HasOhlcvData()) {
        m_statusMessage = "Load OHLCV data before exporting.";
        m_statusSuccess = false;
        return;
    }

    std::string slug = SanitizeSlug(m_datasetSlug[0] ? m_datasetSlug : m_timeSeriesWindow->GetSuggestedDatasetId());
    if (slug.empty()) {
        m_statusMessage = "Dataset slug cannot be empty.";
        m_statusSuccess = false;
        return;
    }

    std::string indicatorMeasurement = SanitizeSlug(m_indicatorMeasurementBuffer);
    if (indicatorMeasurement.empty()) {
        indicatorMeasurement = slug + "_ind";
    }
    std::string ohlcvMeasurement = SanitizeSlug(m_ohlcvMeasurementBuffer);
    if (ohlcvMeasurement.empty()) {
        ohlcvMeasurement = slug + "_ohlcv";
    }

    const std::string datasetId = Stage1MetadataWriter::MakeDeterministicUuid(slug);
    DatasetMetadata metadata;
    metadata.dataset_id = datasetId;
    metadata.dataset_slug = slug;
    metadata.indicator_measurement = indicatorMeasurement;
    metadata.ohlcv_measurement = ohlcvMeasurement;

    const auto* indicatorFrame = m_timeSeriesWindow->GetDataFrame();
    if (!indicatorFrame) {
        m_statusMessage = "Indicator dataframe is not available for export.";
        m_statusSuccess = false;
        return;
    }
    const auto* ohlcvFrame = m_candlestickChart->GetAnalyticsDataFrame();
    if (!ohlcvFrame) {
        m_statusMessage = "OHLCV dataframe is not available for export.";
        m_statusSuccess = false;
        return;
    }

    std::string indicatorCsv;
    std::string csvError;
    if (!DataFrameToCsv(*indicatorFrame, &indicatorCsv, &csvError)) {
        m_statusMessage = csvError.empty() ? "Failed to serialize indicator dataset." : csvError;
        m_statusSuccess = false;
        return;
    }

    std::string ohlcvCsv;
    if (!DataFrameToCsv(*ohlcvFrame, &ohlcvCsv, &csvError)) {
        m_statusMessage = csvError.empty() ? "Failed to serialize OHLCV dataset." : csvError;
        m_statusSuccess = false;
        return;
    }

    stage1::RestClient& api = stage1::RestClient::Instance();
    auto waitForJob = [&](const std::string& jobId, const std::string& label) -> bool {
        constexpr int kMaxAttempts = 120;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
            stage1::JobStatus status;
            std::string error;
            if (!api.GetJobStatus(jobId, &status, &error)) {
                m_statusMessage = "Failed to query Stage1 job (" + label + "): " + error;
                m_statusSuccess = false;
                return false;
            }
            if (status.status == "COMPLETED") {
                return true;
            }
            if (status.status == "FAILED" || status.status == "CANCELLED") {
                m_statusMessage = "Stage1 job (" + label + ") " + status.status;
                if (!status.error.empty()) {
                    m_statusMessage += ": " + status.error;
                }
                m_statusSuccess = false;
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        m_statusMessage = "Timed out waiting for Stage1 job completion (" + label + ").";
        m_statusSuccess = false;
        return false;
    };

    std::string jobId;
    if (!api.SubmitQuestDbImport(indicatorMeasurement, indicatorCsv, indicatorMeasurement + ".csv", &jobId, &csvError)) {
        m_statusMessage = "Failed to upload indicator data: " + csvError;
        m_statusSuccess = false;
        return;
    }
    if (!waitForJob(jobId, "indicator data")) {
        return;
    }

    if (!api.SubmitQuestDbImport(ohlcvMeasurement, ohlcvCsv, ohlcvMeasurement + ".csv", &jobId, &csvError)) {
        m_statusMessage = "Failed to upload OHLCV data: " + csvError;
        m_statusSuccess = false;
        return;
    }
    if (!waitForJob(jobId, "ohlcv data")) {
        return;
    }

    metadata.indicator_rows = static_cast<int64_t>(m_timeSeriesWindow->GetRowCount());
    metadata.ohlcv_rows = static_cast<int64_t>(m_candlestickChart->GetOhlcvData().getRawDataCount());

    Stage1MetadataWriter::DatasetRecord record;
    record.dataset_id = datasetId;
    record.dataset_slug = slug;
    record.symbol = slug;
    record.granularity = "unknown";
    record.source = "laptop_imgui";
    record.ohlcv_measurement = ohlcvMeasurement;
    record.indicator_measurement = indicatorMeasurement;
    record.ohlcv_row_count = metadata.ohlcv_rows;
    record.indicator_row_count = metadata.indicator_rows;
    auto indicatorBounds = m_timeSeriesWindow->GetTimestampBounds();
    auto ohlcvBounds = m_candlestickChart->GetTimestampBoundsMs();
    record.indicator_first_timestamp_unix = indicatorBounds.first;
    record.indicator_last_timestamp_unix = indicatorBounds.second;
    record.ohlcv_first_timestamp_unix = ohlcvBounds.first;
    record.ohlcv_last_timestamp_unix = ohlcvBounds.second;
    record.created_at = std::chrono::system_clock::now();
    Stage1MetadataWriter::Instance().RecordDatasetExport(record);

    m_timeSeriesWindow->SetActiveDatasetMetadata(metadata);
    std::snprintf(m_datasetSlug, sizeof(m_datasetSlug), "%s", slug.c_str());
    std::snprintf(m_indicatorMeasurementBuffer, sizeof(m_indicatorMeasurementBuffer), "%s", indicatorMeasurement.c_str());
    std::snprintf(m_ohlcvMeasurementBuffer, sizeof(m_ohlcvMeasurementBuffer), "%s", ohlcvMeasurement.c_str());
    m_statusMessage = "Dataset '" + slug + "' exported.";
    m_statusSuccess = true;
    m_refreshPending = true;
    m_selectedIndex = -1;
}

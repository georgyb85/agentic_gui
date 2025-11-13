#include "Stage1DatasetManager.h"

#include "Stage1DatasetManifest.h"
#include "Stage1RestClient.h"
#include "TimeSeriesWindow.h"
#include "candlestick_chart.h"
#include "stage1_metadata_writer.h"
#include "analytics_dataframe.h"
#include "imgui.h"
#include <arrow/table.h>
#include <arrow/scalar.h>
#include <arrow/type.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <sstream>
#include <exception>
#include <unordered_set>
#include <json/json.h>
#include <array>

namespace {

std::string ToDisplay(const std::string& value) {
    if (value.empty() || value == "NULL") {
        return "-";
    }
    return value;
}

const char* kTimestampCandidates[] = {
    "timestamp_unix",
    "timestamp",
    "timestamp_seconds",
    "timestamp_unix_s",
    "ts",
    "time"
};

std::string DetectTimestampColumn(const chronosflow::AnalyticsDataFrame& frame) {
    auto table = frame.get_cpu_table();
    if (!table) {
        return {};
    }
    auto schema = table->schema();
    if (!schema) {
        return {};
    }
    for (const char* candidate : kTimestampCandidates) {
        if (!candidate) continue;
        if (schema->GetFieldIndex(candidate) >= 0) {
            return candidate;
        }
    }
    return {};
}

std::optional<int64_t> ParseIsoToMillis(const std::string& text) {
    if (text.size() < 19) {
        return std::nullopt;
    }
    auto ParseInt = [&](size_t pos, size_t len) -> std::optional<int> {
        if (pos + len > text.size()) return std::nullopt;
        int value = 0;
        for (size_t i = 0; i < len; ++i) {
            char ch = text[pos + i];
            if (ch < '0' || ch > '9') {
                return std::nullopt;
            }
            value = value * 10 + (ch - '0');
        }
        return value;
    };

    auto year = ParseInt(0, 4);
    auto month = ParseInt(5, 2);
    auto day = ParseInt(8, 2);
    auto hour = ParseInt(11, 2);
    auto minute = ParseInt(14, 2);
    auto second = ParseInt(17, 2);
    if (!year || !month || !day || !hour || !minute || !second) {
        return std::nullopt;
    }

    int64_t fractionMillis = 0;
    auto dotPos = text.find('.', 19);
    if (dotPos != std::string::npos) {
        size_t fracStart = dotPos + 1;
        size_t fracEnd = fracStart;
        while (fracEnd < text.size() && std::isdigit(static_cast<unsigned char>(text[fracEnd]))) {
            ++fracEnd;
        }
        std::string fraction = text.substr(fracStart, fracEnd - fracStart);
        while (fraction.size() < 3) fraction.push_back('0');
        if (fraction.size() > 3) fraction.resize(3);
        try {
            fractionMillis = std::stoll(fraction);
        } catch (...) {
            fractionMillis = 0;
        }
    }

    std::tm tm = {};
    tm.tm_year = *year - 1900;
    tm.tm_mon = *month - 1;
    tm.tm_mday = *day;
    tm.tm_hour = *hour;
    tm.tm_min = *minute;
    tm.tm_sec = *second;
#if defined(_WIN32)
    time_t seconds = _mkgmtime(&tm);
#else
    time_t seconds = timegm(&tm);
#endif
    if (seconds == static_cast<time_t>(-1)) {
        return std::nullopt;
    }
    return static_cast<int64_t>(seconds) * 1000LL + fractionMillis;
}

std::optional<int64_t> ScalarToMillis(const std::shared_ptr<arrow::Scalar>& scalar) {
    if (!scalar || !scalar->is_valid) {
        return std::nullopt;
    }
    const bool coerceSeconds = true;
    switch (scalar->type->id()) {
        case arrow::Type::INT64: {
            int64_t value = std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value;
            if (coerceSeconds && std::llabs(value) < 4'000'000'000LL) {
                value *= 1000;
            }
            return value;
        }
        case arrow::Type::INT32: {
            int64_t value = std::static_pointer_cast<arrow::Int32Scalar>(scalar)->value;
            if (coerceSeconds && std::llabs(value) < 4'000'000'000LL) {
                value *= 1000;
            }
            return value;
        }
        case arrow::Type::DOUBLE:
        case arrow::Type::FLOAT: {
            double numeric = (scalar->type->id() == arrow::Type::DOUBLE)
                ? std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value
                : static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value);
            int64_t value = static_cast<int64_t>(std::llround(numeric));
            if (coerceSeconds && std::llabs(value) < 4'000'000'000LL) {
                value *= 1000;
            }
            return value;
        }
        case arrow::Type::STRING:
        case arrow::Type::LARGE_STRING: {
            auto text = scalar->ToString();
            if (text.empty()) {
                return std::nullopt;
            }
            bool isNumeric = std::all_of(text.begin(), text.end(), [](unsigned char ch) {
                return std::isdigit(ch) || ch == '-' || ch == '+';
            });
            if (isNumeric) {
                try {
                    int64_t value = std::stoll(text);
                    if (coerceSeconds && std::llabs(value) < 4'000'000'000LL) {
                        value *= 1000;
                    }
                    return value;
                } catch (...) {
                }
            }
            return ParseIsoToMillis(text);
        }
        case arrow::Type::TIMESTAMP: {
            auto tsScalar = std::static_pointer_cast<arrow::TimestampScalar>(scalar);
            int64_t value = tsScalar->value;
            auto type = std::static_pointer_cast<arrow::TimestampType>(tsScalar->type);
            switch (type->unit()) {
                case arrow::TimeUnit::SECOND: return value * 1000;
                case arrow::TimeUnit::MILLI: return value;
                case arrow::TimeUnit::MICRO: return value / 1000;
                case arrow::TimeUnit::NANO: return value / 1000000;
            }
            break;
        }
        default:
            break;
    }
    return std::nullopt;
}

bool ScalarToDouble(const std::shared_ptr<arrow::Scalar>& scalar, double* out) {
    if (!scalar || !scalar->is_valid || !out) {
        return false;
    }
    switch (scalar->type->id()) {
        case arrow::Type::DOUBLE:
            *out = std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
            return std::isfinite(*out);
        case arrow::Type::FLOAT:
            *out = static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value);
            return std::isfinite(*out);
        case arrow::Type::INT64:
            *out = static_cast<double>(std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value);
            return true;
        case arrow::Type::INT32:
            *out = static_cast<double>(std::static_pointer_cast<arrow::Int32Scalar>(scalar)->value);
            return true;
        case arrow::Type::INT16:
            *out = static_cast<double>(std::static_pointer_cast<arrow::Int16Scalar>(scalar)->value);
            return true;
        case arrow::Type::INT8:
            *out = static_cast<double>(std::static_pointer_cast<arrow::Int8Scalar>(scalar)->value);
            return true;
        case arrow::Type::UINT64:
            *out = static_cast<double>(std::static_pointer_cast<arrow::UInt64Scalar>(scalar)->value);
            return true;
        case arrow::Type::UINT32:
            *out = static_cast<double>(std::static_pointer_cast<arrow::UInt32Scalar>(scalar)->value);
            return true;
        case arrow::Type::UINT16:
            *out = static_cast<double>(std::static_pointer_cast<arrow::UInt16Scalar>(scalar)->value);
            return true;
        case arrow::Type::UINT8:
            *out = static_cast<double>(std::static_pointer_cast<arrow::UInt8Scalar>(scalar)->value);
            return true;
        case arrow::Type::BOOL:
            *out = std::static_pointer_cast<arrow::BooleanScalar>(scalar)->value ? 1.0 : 0.0;
            return true;
        default:
            break;
    }
    return false;
}

std::optional<int64_t> ComputeBarIntervalMs(const CandlestickChart* chart) {
    if (!chart) {
        return std::nullopt;
    }
    const auto& raw = chart->GetOhlcvData().getRawData();
    if (raw.size() < 2) {
        return std::nullopt;
    }
    time_t prev = raw.front().time;
    for (size_t i = 1; i < raw.size(); ++i) {
        time_t current = raw[i].time;
        if (current > prev) {
            return static_cast<int64_t>(current - prev) * 1000LL;
        }
    }
    return std::nullopt;
}

std::string FormatGranularityFromInterval(int64_t intervalMs) {
    if (intervalMs <= 0) {
        return "unknown";
    }
    if (intervalMs == 60 * 1000) return "1m";
    if (intervalMs == 5 * 60 * 1000) return "5m";
    if (intervalMs == 15 * 60 * 1000) return "15m";
    if (intervalMs == 30 * 60 * 1000) return "30m";
    if (intervalMs == 60 * 60 * 1000) return "1h";
    if (intervalMs == 4 * 60 * 60 * 1000) return "4h";
    if (intervalMs == 24 * 60 * 60 * 1000) return "1d";
    return std::to_string(intervalMs) + "ms";
}

int64_t ComputeLookbackRows(const stage1::DatasetManifest& manifest) {
    const int64_t fallback = manifest.ohlcv_rows > manifest.indicator_rows
        ? manifest.ohlcv_rows - manifest.indicator_rows
        : 0;
    if (manifest.bar_interval_ms <= 0 ||
        manifest.first_indicator_timestamp_ms <= 0 ||
        manifest.first_ohlcv_timestamp_ms <= 0) {
        return fallback;
    }
    const auto diff = manifest.first_indicator_timestamp_ms - manifest.first_ohlcv_timestamp_ms;
    if (diff <= 0) {
        return fallback;
    }
    const double bars = static_cast<double>(diff) / static_cast<double>(manifest.bar_interval_ms);
    const auto rounded = static_cast<int64_t>(std::llround(bars));
    return rounded > 0 ? rounded : fallback;
}

std::filesystem::path ResolveDatasetBaseDirectory() {
    static const std::array<std::filesystem::path, 5> kCandidates = {
        std::filesystem::path("docs/fixtures/stage1_3/datasets"),
        std::filesystem::path("../docs/fixtures/stage1_3/datasets"),
        std::filesystem::path("../../docs/fixtures/stage1_3/datasets"),
        std::filesystem::path("../../../docs/fixtures/stage1_3/datasets"),
        std::filesystem::path("../../../..//docs/fixtures/stage1_3/datasets")
    };

    auto CanonicalIfPossible = [](const std::filesystem::path& path) {
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            return canonical;
        }
        return std::filesystem::absolute(path);
    };

    for (const auto& candidate : kCandidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return CanonicalIfPossible(candidate);
        }
    }

    for (const auto& candidate : kCandidates) {
        auto parent = candidate.parent_path();
        if (parent.empty()) {
            continue;
        }
        std::error_code ec;
        if (std::filesystem::exists(parent, ec) && !ec) {
            return CanonicalIfPossible(candidate);
        }
    }

    return CanonicalIfPossible(kCandidates.front());
}

std::filesystem::path DatasetBaseDirectory() {
    static std::filesystem::path resolved = ResolveDatasetBaseDirectory();
    return resolved;
}

std::filesystem::path ManifestDirectory(const std::string& slug) {
    std::filesystem::path base = DatasetBaseDirectory();
    if (slug.empty()) {
        return base / "unnamed_dataset";
    }
    return base / slug;
}

constexpr std::size_t kStage1AppendBatchSize = 1000;

bool FlushRowBatch(const std::string& datasetId,
                   Json::Value* rows,
                   stage1::RestClient::AppendTarget target,
                   std::string* error) {
    if (!rows || !rows->isArray() || rows->empty()) {
        return true;
    }
    Json::Value payload(Json::objectValue);
    payload["rows"] = *rows;
    bool ok = stage1::RestClient::Instance().AppendDatasetRows(datasetId, payload, target, error);
    *rows = Json::Value(Json::arrayValue);
    return ok;
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

    if (ImGui::BeginTable("DatasetTable", 9,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 70.0f);
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
                ImGui::TextUnformatted(row.local_only ? "Local" : "Stage1");
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
    bool remoteOk = api.FetchDatasets(200, 0, &datasets, &error);
    if (!remoteOk) {
        std::cerr << "[Stage1DatasetManager] Failed to load datasets from Stage1 API: "
                  << (error.empty() ? "unknown error" : error) << std::endl;
    }

    m_rows.clear();
    std::unordered_set<std::string> seenKeys;
    seenKeys.reserve(datasets.size() + 8);
    auto tryAddRow = [&](DatasetRow&& row) -> bool {
        std::string key = MakeRowKey(row);
        if (key.empty()) {
            if (!row.dataset_slug.empty()) {
                row.dataset_id = Stage1MetadataWriter::MakeDeterministicUuid(row.dataset_slug);
                key = row.dataset_id;
            }
        }
        if (key.empty()) {
            return false;
        }
        if (!seenKeys.insert(key).second) {
            return false;
        }
        m_rows.push_back(std::move(row));
        return true;
    };

    size_t remoteCount = 0;
    if (remoteOk) {
        for (const auto& summary : datasets) {
            DatasetRow parsed;
            parsed.dataset_id = summary.dataset_id;
            parsed.dataset_slug = summary.dataset_slug.empty() ? summary.dataset_id : summary.dataset_slug;
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
            parsed.local_only = false;
            if (tryAddRow(std::move(parsed))) {
                ++remoteCount;
            }
        }
    }

    auto localRows = LoadLocalDatasetRows();
    size_t localCount = 0;
    for (auto& row : localRows) {
        if (tryAddRow(std::move(row))) {
            ++localCount;
        }
    }

    const size_t totalCount = remoteCount + localCount;
    if (totalCount == 0) {
        if (remoteOk) {
            m_statusMessage = "No datasets available on Stage1 or in local manifests.";
            m_statusSuccess = true;
        } else {
            m_statusMessage = error.empty()
                ? "Failed to load datasets from Stage1 API."
                : error;
            m_statusSuccess = false;
        }
    } else {
        std::ostringstream oss;
        oss << "Showing " << totalCount << " dataset(s) ("
            << remoteCount << " Stage1, " << localCount << " local)";
        if (!remoteOk) {
            oss << ". Stage1 API unavailable";
            if (!error.empty()) {
                oss << ": " << error;
            }
            m_statusSuccess = (localCount > 0);
        } else {
            m_statusSuccess = true;
        }
        m_statusMessage = oss.str();
    }

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

    auto indicatorTsColumn = DetectTimestampColumn(*indicatorFrame);
    if (indicatorTsColumn.empty()) {
        m_statusMessage = "Indicator data is missing a timestamp column.";
        m_statusSuccess = false;
        return;
    }
    auto ohlcvTsColumn = DetectTimestampColumn(*ohlcvFrame);
    if (ohlcvTsColumn.empty()) {
        m_statusMessage = "OHLCV data is missing a timestamp column.";
        m_statusSuccess = false;
        return;
    }

    metadata.indicator_rows = static_cast<int64_t>(m_timeSeriesWindow->GetRowCount());
    metadata.ohlcv_rows = static_cast<int64_t>(m_candlestickChart->GetOhlcvData().getRawDataCount());

    auto indicatorBounds = m_timeSeriesWindow->GetTimestampBounds();
    auto ohlcvBounds = m_candlestickChart->GetTimestampBoundsMs();
    auto barInterval = ComputeBarIntervalMs(m_candlestickChart);
    const auto exportedAt = std::chrono::system_clock::now();
    const bool stage1NetworkEnabled = Stage1MetadataWriter::NetworkExportsEnabled();

    stage1::DatasetManifest manifest;
    manifest.dataset_id = datasetId;
    manifest.dataset_slug = slug;
    manifest.symbol = slug;
    manifest.granularity = FormatGranularityFromInterval(barInterval.value_or(0));
    manifest.source = "laptop_imgui";
    manifest.ohlcv_measurement = ohlcvMeasurement;
    manifest.indicator_measurement = indicatorMeasurement;
    manifest.bar_interval_ms = barInterval.value_or(0);
    manifest.ohlcv_rows = metadata.ohlcv_rows;
    manifest.indicator_rows = metadata.indicator_rows;
    manifest.first_ohlcv_timestamp_ms = ohlcvBounds.first.value_or(0);
    manifest.last_ohlcv_timestamp_ms = ohlcvBounds.second.value_or(0);
    manifest.first_indicator_timestamp_ms = indicatorBounds.first.value_or(0);
    manifest.last_indicator_timestamp_ms = indicatorBounds.second.value_or(0);
    manifest.lookback_rows = ComputeLookbackRows(manifest);
    manifest.exported_at_iso = stage1::FormatIsoTimestamp(exportedAt);
    const std::string manifestJson = manifest.ToJsonString();

    std::string manifestError;
    if (!stage1::WriteManifestToDirectory(manifest, ManifestDirectory(slug), &manifestError) &&
        !manifestError.empty()) {
        std::cerr << "[Stage1DatasetManager] Failed to write manifest: " << manifestError << std::endl;
    }

    Stage1MetadataWriter::DatasetRecord record;
    record.dataset_id = datasetId;
    record.dataset_slug = slug;
    record.symbol = slug;
    record.granularity = manifest.granularity.empty() ? "unknown" : manifest.granularity;
    record.source = "laptop_imgui";
    record.ohlcv_measurement = ohlcvMeasurement;
    record.indicator_measurement = indicatorMeasurement;
    record.ohlcv_row_count = metadata.ohlcv_rows;
    record.indicator_row_count = metadata.indicator_rows;
    record.indicator_first_timestamp_unix = indicatorBounds.first;
    record.indicator_last_timestamp_unix = indicatorBounds.second;
    record.ohlcv_first_timestamp_unix = ohlcvBounds.first;
    record.ohlcv_last_timestamp_unix = ohlcvBounds.second;
    record.metadata_json = manifestJson;
    record.created_at = exportedAt;
    std::string metadataError;
    if (!Stage1MetadataWriter::Instance().RecordDatasetExport(record, &metadataError)) {
        m_statusMessage = metadataError.empty()
            ? "Failed to register dataset metadata with Stage1."
            : metadataError;
        m_statusSuccess = false;
        return;
    }

    if (!stage1NetworkEnabled) {
        m_timeSeriesWindow->SetActiveDatasetMetadata(metadata);
        std::snprintf(m_datasetSlug, sizeof(m_datasetSlug), "%s", slug.c_str());
        std::snprintf(m_indicatorMeasurementBuffer, sizeof(m_indicatorMeasurementBuffer), "%s", indicatorMeasurement.c_str());
        std::snprintf(m_ohlcvMeasurementBuffer, sizeof(m_ohlcvMeasurementBuffer), "%s", ohlcvMeasurement.c_str());
        m_statusMessage = "Dataset '" + slug + "' exported locally. Stage1 network exports are disabled (set STAGE1_ENABLE_EXPORTS=1 to sync).";
        m_statusSuccess = true;
        m_refreshPending = true;
        m_selectedIndex = -1;
        return;
    }

    std::string resolvedDatasetId;
    std::string verifyError;
    if (!EnsureStage1DatasetReady(datasetId, slug, &resolvedDatasetId, &verifyError)) {
        resolvedDatasetId = datasetId;
        if (!verifyError.empty()) {
            std::cerr << "[Stage1DatasetManager] Warning: " << verifyError
                      << " Proceeding with dataset_id=" << resolvedDatasetId << std::endl;
        }
    }

    // Create or update dataset on Stage1 server before uploading data
    std::string createError;
    if (!stage1::RestClient::Instance().CreateOrUpdateDataset(
            resolvedDatasetId,
            slug,
            manifest.granularity,
            manifest.bar_interval_ms,
            manifest.lookback_rows,
            manifest.first_ohlcv_timestamp_ms,
            manifest.first_indicator_timestamp_ms,
            manifestJson,
            &createError)) {
        m_statusMessage = createError.empty()
            ? "Failed to create dataset on Stage1 server."
            : createError;
        m_statusSuccess = false;
        return;
    }

    std::string uploadError;
    if (!UploadOhlcvRowsToStage1(resolvedDatasetId, &uploadError)) {
        m_statusMessage = uploadError.empty()
            ? "Failed to upload OHLCV rows to Stage1."
            : uploadError;
        m_statusSuccess = false;
        return;
    }
    if (!UploadIndicatorRowsToStage1(resolvedDatasetId, indicatorTsColumn, &uploadError)) {
        m_statusMessage = uploadError.empty()
            ? "Failed to upload indicator rows to Stage1."
            : uploadError;
        m_statusSuccess = false;
        return;
    }

    m_timeSeriesWindow->SetActiveDatasetMetadata(metadata);
    std::snprintf(m_datasetSlug, sizeof(m_datasetSlug), "%s", slug.c_str());
    std::snprintf(m_indicatorMeasurementBuffer, sizeof(m_indicatorMeasurementBuffer), "%s", indicatorMeasurement.c_str());
    std::snprintf(m_ohlcvMeasurementBuffer, sizeof(m_ohlcvMeasurementBuffer), "%s", ohlcvMeasurement.c_str());
    m_statusMessage = "Dataset '" + slug + "' exported.";
    m_statusSuccess = true;
    m_refreshPending = true;
    m_selectedIndex = -1;
}

std::vector<Stage1DatasetManager::DatasetRow> Stage1DatasetManager::LoadLocalDatasetRows() const {
    std::vector<DatasetRow> rows;
    const auto base = DatasetBaseDirectory();
    if (!std::filesystem::exists(base)) {
        return rows;
    }
    try {
        for (const auto& entry : std::filesystem::directory_iterator(base)) {
            if (!entry.is_directory()) {
                continue;
            }
            stage1::DatasetManifest manifest;
            std::string manifestError;
            if (!stage1::ReadManifestFromDirectory(entry.path(), &manifest, &manifestError)) {
                std::cerr << "[Stage1DatasetManager] Failed to read manifest from "
                          << entry.path() << ": " << manifestError << std::endl;
                continue;
            }

            DatasetRow row;
            std::string slug = manifest.dataset_slug.empty()
                ? entry.path().filename().string()
                : manifest.dataset_slug;
            if (slug.empty()) {
                slug = "dataset_" + entry.path().filename().string();
            }
            std::string sanitized = SanitizeSlug(slug);
            if (!sanitized.empty()) {
                slug = sanitized;
            }

            row.dataset_slug = slug;
            row.dataset_id = manifest.dataset_id;
            if (row.dataset_id.empty() && !slug.empty()) {
                row.dataset_id = Stage1MetadataWriter::MakeDeterministicUuid(slug);
            }
            row.symbol = manifest.symbol.empty() ? slug : manifest.symbol;
            row.granularity = manifest.granularity.empty()
                ? FormatGranularityFromInterval(manifest.bar_interval_ms)
                : manifest.granularity;
            row.ohlcv_measurement = manifest.ohlcv_measurement;
            row.indicator_measurement = manifest.indicator_measurement;
            row.ohlcv_rows = manifest.ohlcv_rows;
            row.indicator_rows = manifest.indicator_rows;
            row.ohlcv_first_ts = FormatTimestampMs(manifest.first_ohlcv_timestamp_ms);
            row.ohlcv_last_ts = FormatTimestampMs(manifest.last_ohlcv_timestamp_ms);
            row.indicator_first_ts = FormatTimestampMs(manifest.first_indicator_timestamp_ms);
            row.indicator_last_ts = FormatTimestampMs(manifest.last_indicator_timestamp_ms);
            row.updated_at = manifest.exported_at_iso.empty() ? "-" : manifest.exported_at_iso;
            row.local_only = true;
            rows.push_back(std::move(row));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[Stage1DatasetManager] Directory iteration error: " << ex.what() << std::endl;
    }
    return rows;
}

std::string Stage1DatasetManager::FormatTimestampMs(int64_t timestamp_ms) {
    if (timestamp_ms <= 0) {
        return "-";
    }
    auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(timestamp_ms));
    return stage1::FormatIsoTimestamp(tp);
}

std::string Stage1DatasetManager::MakeRowKey(const DatasetRow& row) {
    if (!row.dataset_id.empty()) {
        return row.dataset_id;
    }
    return row.dataset_slug;
}

bool Stage1DatasetManager::UploadOhlcvRowsToStage1(const std::string& datasetId,
                                                   std::string* error) {
    if (datasetId.empty()) {
        if (error) *error = "Dataset ID is required for OHLCV upload.";
        return false;
    }
    if (!m_candlestickChart) {
        if (error) *error = "Candlestick chart is unavailable.";
        return false;
    }
    const auto& raw = m_candlestickChart->GetOhlcvData().getRawData();
    if (raw.empty()) {
        if (error) *error = "No OHLCV rows loaded.";
        return false;
    }

    Json::Value rows(Json::arrayValue);
    size_t appended = 0;
    for (const auto& candle : raw) {
        Json::Value row(Json::objectValue);
        const int64_t timestampMs = static_cast<int64_t>(candle.time) * 1000LL;
        row["timestamp"] = static_cast<Json::Int64>(timestampMs);
        row["open"] = candle.open;
        row["high"] = candle.high;
        row["low"] = candle.low;
        row["close"] = candle.close;
        row["volume"] = candle.volume;
        rows.append(row);
        ++appended;
        if (rows.size() >= kStage1AppendBatchSize) {
            if (!FlushRowBatch(datasetId, &rows, stage1::RestClient::AppendTarget::Ohlcv, error)) {
                return false;
            }
        }
    }
    if (!FlushRowBatch(datasetId, &rows, stage1::RestClient::AppendTarget::Ohlcv, error)) {
        return false;
    }
    if (appended == 0) {
        if (error) *error = "Uploaded zero OHLCV rows.";
        return false;
    }
    return true;
}

bool Stage1DatasetManager::UploadIndicatorRowsToStage1(const std::string& datasetId,
                                                       const std::string& timestampColumn,
                                                       std::string* error) {
    if (datasetId.empty()) {
        if (error) *error = "Dataset ID is required for indicator upload.";
        return false;
    }
    if (timestampColumn.empty()) {
        if (error) *error = "Indicator timestamp column is not specified.";
        return false;
    }
    if (!m_timeSeriesWindow) {
        if (error) *error = "Time Series window unavailable.";
        return false;
    }
    const auto* frame = m_timeSeriesWindow->GetDataFrame();
    if (!frame) {
        if (error) *error = "Indicator dataframe is null.";
        return false;
    }
    auto table = frame->get_cpu_table();
    if (!table) {
        if (error) *error = "Indicator dataframe is not on CPU.";
        return false;
    }
    auto schema = table->schema();
    if (!schema) {
        if (error) *error = "Indicator dataframe schema is unavailable.";
        return false;
    }
    const int timestampIdx = schema->GetFieldIndex(timestampColumn);
    if (timestampIdx < 0) {
        if (error) *error = "Timestamp column '" + timestampColumn + "' not found in indicator data.";
        return false;
    }
    auto timestampData = table->column(timestampIdx);
    if (!timestampData) {
        if (error) *error = "Timestamp column '" + timestampColumn + "' is unavailable.";
        return false;
    }

    std::vector<int> valueColumns;
    valueColumns.reserve(schema->num_fields());
    for (int col = 0; col < schema->num_fields(); ++col) {
        if (col == timestampIdx) {
            continue;
        }
        switch (schema->field(col)->type()->id()) {
            case arrow::Type::DOUBLE:
            case arrow::Type::FLOAT:
            case arrow::Type::INT64:
            case arrow::Type::INT32:
            case arrow::Type::INT16:
            case arrow::Type::INT8:
            case arrow::Type::UINT64:
            case arrow::Type::UINT32:
            case arrow::Type::UINT16:
            case arrow::Type::UINT8:
            case arrow::Type::BOOL:
                valueColumns.push_back(col);
                break;
            default:
                break;
        }
    }
    if (valueColumns.empty()) {
        if (error) *error = "Indicator dataframe has no numeric columns to export.";
        return false;
    }

    Json::Value rows(Json::arrayValue);
    int64_t totalRows = table->num_rows();
    size_t appended = 0;
    for (int64_t rowIndex = 0; rowIndex < totalRows; ++rowIndex) {
        auto tsScalarResult = timestampData->GetScalar(rowIndex);
        if (!tsScalarResult.ok()) {
            if (error) {
                *error = "Failed to read indicator timestamp at row "
                    + std::to_string(rowIndex) + ": " + tsScalarResult.status().ToString();
            }
            return false;
        }
        auto tsScalar = tsScalarResult.ValueOrDie();
        auto timestampMs = ScalarToMillis(tsScalar);
        if (!timestampMs || *timestampMs <= 0) {
            continue;
        }

        Json::Value row(Json::objectValue);
        row["timestamp"] = static_cast<Json::Int64>(*timestampMs);
        bool hasField = false;
        for (int colIndex : valueColumns) {
            auto column = table->column(colIndex);
            if (!column) {
                continue;
            }
            auto scalarResult = column->GetScalar(rowIndex);
            if (!scalarResult.ok()) {
                continue;
            }
            auto scalar = scalarResult.ValueOrDie();
            double numeric = 0.0;
            if (!ScalarToDouble(scalar, &numeric)) {
                continue;
            }
            if (!std::isfinite(numeric)) {
                continue;
            }
            const std::string& name = schema->field(colIndex)->name();
            if (name == timestampColumn) {
                continue;
            }
            row[name] = numeric;
            hasField = true;
        }
        if (!hasField) {
            continue;
        }

        rows.append(row);
        ++appended;
        if (rows.size() >= kStage1AppendBatchSize) {
            if (!FlushRowBatch(datasetId, &rows, stage1::RestClient::AppendTarget::Indicators, error)) {
                return false;
            }
        }
    }

    if (!FlushRowBatch(datasetId, &rows, stage1::RestClient::AppendTarget::Indicators, error)) {
        return false;
    }
    if (appended == 0) {
        if (error) *error = "Uploaded zero indicator rows.";
        return false;
    }
    return true;
}

bool Stage1DatasetManager::EnsureStage1DatasetReady(const std::string& preferredId,
                                                    const std::string& slug,
                                                    std::string* resolvedId,
                                                    std::string* error) const {
    stage1::RestClient& api = stage1::RestClient::Instance();
    if (resolvedId) {
        *resolvedId = preferredId;
    }
    if (!preferredId.empty()) {
        stage1::DatasetSummary summary;
        std::string restError;
        if (api.FetchDataset(preferredId, &summary, &restError)) {
            if (resolvedId) {
                *resolvedId = summary.dataset_id.empty() ? preferredId : summary.dataset_id;
            }
            return true;
        }
        if (!restError.empty() && restError != "Dataset not found") {
            if (error) *error = restError;
            return false;
        }
    }

    std::vector<stage1::DatasetSummary> datasets;
    std::string listError;
    if (!api.FetchDatasets(200, 0, &datasets, &listError)) {
        if (error) {
            *error = listError.empty()
                ? "Failed to list Stage1 datasets."
                : listError;
        }
        return false;
    }
    for (const auto& entry : datasets) {
        if (!slug.empty() && entry.dataset_slug == slug) {
            if (resolvedId) {
                *resolvedId = entry.dataset_id.empty() ? preferredId : entry.dataset_id;
            }
            return true;
        }
    }
    if (error) {
        *error = "Dataset not yet visible on Stage1 API (will attempt upload anyway).";
    }
    return false;
}

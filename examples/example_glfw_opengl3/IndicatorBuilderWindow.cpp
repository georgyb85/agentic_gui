#include "IndicatorBuilderWindow.h"

#include "candlestick_chart.h"
#include "implot.h"
#include "misc/cpp/imgui_stdlib.h"
#include "IndicatorConfig.hpp"
#include "TaskExecutor.hpp"
#include "Series.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string TrimCopy(const std::string& value) {
    auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
    auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::tm ToUtcTm(int64_t timestampSeconds) {
    std::tm tm{};
    std::time_t tt = static_cast<std::time_t>(timestampSeconds);
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    return tm;
}

} // namespace

IndicatorBuilderWindow::IndicatorBuilderWindow()
    : m_candlestickChart(nullptr)
    , m_visible(false)
    , m_lastValidationSuccess(false)
    , m_buildState(BuildState::Idle)
    , m_statusIsError(false)
    , m_lastBuildDurationMs(0.0)
    , m_currentRowCount(0)
    , m_selectedColumnIndex(-1)
    , m_autoFitPlot(true)
    , m_tableHeight(220.0f)
    , m_plotHeight(320.0f)
{
    m_scriptText =
        "# Paste a TSSB-style script below\n"
        "RSI_S: RSI 14\n"
        "ADX_S: ADX 14\n"
        "ATR_RATIO_S: ATR RATIO 14 2\n";

    m_tableFlags = ImGuiTableFlags_Borders |
                   ImGuiTableFlags_RowBg |
                   ImGuiTableFlags_ScrollY |
                   ImGuiTableFlags_ScrollX |
                   ImGuiTableFlags_Resizable |
                   ImGuiTableFlags_Sortable;

    m_statusMessage = "Paste a script and click Build Indicators.";
}

void IndicatorBuilderWindow::Draw() {
    PollBuildFuture();

    if (!m_visible) {
        return;
    }

    if (ImGui::Begin("Indicator Builder", &m_visible)) {
        DrawScriptSection();
        ImGui::Separator();
        DrawResultsSection();
        DrawStatusBar();
    }
    ImGui::End();
}

void IndicatorBuilderWindow::DrawScriptSection() {
    ImGui::TextUnformatted("Indicator Script");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::InputTextMultiline(
        "##IndicatorScript",
        &m_scriptText,
        ImVec2(-FLT_MIN, 180.0f),
        ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopStyleColor();

    const bool scriptEmpty = TrimCopy(m_scriptText).empty();
    const bool computing = (m_buildState == BuildState::Computing);
    const bool hasOhlcv = HasOhlcvData();

    ImGui::BeginDisabled(scriptEmpty);
    if (ImGui::Button("Validate Script")) {
        std::string validationError;
        if (ValidateScript(&validationError)) {
            m_validationStatus = validationError;
        } else {
            m_validationStatus = validationError;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(scriptEmpty || computing || !hasOhlcv);
    if (ImGui::Button("Build Indicators")) {
        BeginBuild();
    }
    ImGui::EndDisabled();

    if (!hasOhlcv) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "Load OHLCV data first.");
    }

    if (!m_validationStatus.empty()) {
        ImVec4 color = m_lastValidationSuccess
            ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f)
            : ImVec4(0.95f, 0.55f, 0.25f, 1.0f);
        ImGui::TextColored(color, "%s", m_validationStatus.c_str());
    }
}

void IndicatorBuilderWindow::DrawResultsSection() {
    if (m_buildState == BuildState::Computing) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Building indicators...");
        return;
    }

    if (!HasResults()) {
        ImGui::TextUnformatted("No indicator results yet.");
        ImGui::BulletText("Load OHLCV data in the Candlestick window.");
        ImGui::BulletText("Paste a script, validate, then click Build Indicators.");
        return;
    }

    ImGui::Text("Indicator Table");
    ImGui::BeginChild("##IndicatorTable", ImVec2(-FLT_MIN, m_tableHeight), true, ImGuiWindowFlags_NoScrollbar);
    DrawDataTable();
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::Text("Indicator Plot");
    ImGui::BeginChild("##IndicatorPlot", ImVec2(-FLT_MIN, m_plotHeight), true, ImGuiWindowFlags_NoScrollbar);
    DrawPlotArea();
    ImGui::EndChild();
}

void IndicatorBuilderWindow::DrawStatusBar() {
    ImGui::Separator();
    if (!m_statusMessage.empty()) {
        ImVec4 color = m_statusIsError
            ? ImVec4(0.95f, 0.45f, 0.45f, 1.0f)
            : ImVec4(0.7f, 0.8f, 0.9f, 1.0f);
        ImGui::TextColored(color, "%s", m_statusMessage.c_str());
    }
}

void IndicatorBuilderWindow::DrawDataTable() {
    if (m_columnHeaders.empty() || m_displayCache.empty()) {
        ImGui::TextUnformatted("No data to display");
        return;
    }

    const int numColumns = static_cast<int>(m_columnHeaders.size());
    if (ImGui::BeginTable("IndicatorBuilderTable", numColumns, m_tableFlags)) {
        for (int i = 0; i < numColumns; ++i) {
            ImGui::TableSetupColumn(m_columnHeaders[i].c_str());
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (ImGui::TableGetSortSpecs() && ImGui::TableGetSortSpecs()->SpecsDirty) {
            const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
            if (specs->SpecsCount > 0) {
                int clickedColumn = specs->Specs[0].ColumnIndex;
                if (clickedColumn >= kMetadataColumns && clickedColumn < numColumns) {
                    m_selectedColumnIndex = clickedColumn;
                    m_selectedIndicator = m_columnHeaders[clickedColumn];
                    UpdatePlotCache();
                }
            }
            ImGui::TableGetSortSpecs()->SpecsDirty = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_displayCache.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow();
                for (int col = 0; col < numColumns; ++col) {
                    ImGui::TableSetColumnIndex(col);
                    ImGui::TextUnformatted(m_displayCache[row][col].c_str());
                    if (col == m_selectedColumnIndex) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
                    }
                }
            }
        }

        if (GetRowCount() > kMaxDisplayRows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("... (%zu more rows)", GetRowCount() - kMaxDisplayRows);
        }

        ImGui::EndTable();
    }
}

void IndicatorBuilderWindow::DrawPlotArea() {
    if (m_selectedIndicator.empty()) {
        ImGui::TextUnformatted("Select an indicator column to plot.");
        return;
    }

    if (m_plotTimes.empty() || m_plotValues.empty()) {
        ImGui::TextUnformatted("No samples available for the selected indicator.");
        return;
    }

    ImGui::Checkbox("Auto-fit", &m_autoFitPlot);

    if (ImPlot::BeginPlot("##IndicatorPlotArea", ImVec2(-1, -1))) {
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%Y-%m-%d");

        if (m_autoFitPlot) {
            auto minmax = std::minmax_element(m_plotValues.begin(), m_plotValues.end());
            double minVal = *minmax.first;
            double maxVal = *minmax.second;
            double range = maxVal - minVal;
            double padding = (range == 0.0) ? std::max(1.0, std::abs(minVal) * 0.1) : range * 0.05;
            ImPlot::SetupAxisLimits(ImAxis_Y1, minVal - padding, maxVal + padding, ImGuiCond_Always);
        }

        ImPlot::PlotLine(m_selectedIndicator.c_str(),
                         m_plotTimes.data(),
                         m_plotValues.data(),
                         static_cast<int>(m_plotTimes.size()));
        ImPlot::EndPlot();
    }
}

bool IndicatorBuilderWindow::ValidateScript(std::string* errorMessage) {
    std::istringstream stream(m_scriptText);
    std::string line;
    int lineNumber = 0;
    std::vector<tssb::IndicatorDefinition> definitions;

    while (std::getline(stream, line)) {
        ++lineNumber;
        auto trimmed = Trim(line);
        if (trimmed.empty() || IsCommentLine(trimmed)) {
            continue;
        }

        auto parsed = tssb::IndicatorConfigParser::parse_line(trimmed, lineNumber);
        if (!parsed.has_value()) {
            if (errorMessage) {
                *errorMessage = "Parse error on line " + std::to_string(lineNumber);
            }
            m_lastValidationSuccess = false;
            return false;
        }

        std::string validationError;
        if (!tssb::IndicatorConfigParser::validate_definition(parsed.value(), validationError)) {
            if (errorMessage) {
                *errorMessage = "Line " + std::to_string(lineNumber) + ": " + validationError;
            }
            m_lastValidationSuccess = false;
            return false;
        }

        definitions.push_back(parsed.value());
    }

    if (definitions.empty()) {
        if (errorMessage) {
            *errorMessage = "Script does not define any indicators.";
        }
        m_lastValidationSuccess = false;
        return false;
    }

    m_parsedDefinitions = definitions;
    m_lastValidatedScript = m_scriptText;
    m_lastValidationSuccess = true;

    if (errorMessage) {
        *errorMessage = "Validated " + std::to_string(definitions.size()) + " indicator(s).";
    }
    return true;
}

bool IndicatorBuilderWindow::EnsureDefinitionsReady(std::string* errorMessage) {
    if (!m_lastValidationSuccess || m_lastValidatedScript != m_scriptText || m_parsedDefinitions.empty()) {
        return ValidateScript(errorMessage);
    }
    return true;
}

std::optional<IndicatorBuilderWindow::SeriesExtraction> IndicatorBuilderWindow::ExtractSeriesFromOhlcv(
    std::string* errorMessage) const {
    if (!m_candlestickChart) {
        if (errorMessage) {
            *errorMessage = "Candlestick window is not available.";
        }
        return std::nullopt;
    }

    const OhlcvData& ohlcv = m_candlestickChart->GetOhlcvData();
    const auto& raw = ohlcv.getRawData();
    if (raw.empty()) {
        if (errorMessage) {
            *errorMessage = "Load OHLCV data before building indicators.";
        }
        return std::nullopt;
    }

    SeriesExtraction extraction;
    extraction.series.open.reserve(raw.size());
    extraction.series.high.reserve(raw.size());
    extraction.series.low.reserve(raw.size());
    extraction.series.close.reserve(raw.size());
    extraction.series.volume.reserve(raw.size());
    extraction.timestampsMs.reserve(raw.size());

    for (const auto& bar : raw) {
        extraction.series.open.push_back(bar.open);
        extraction.series.high.push_back(bar.high);
        extraction.series.low.push_back(bar.low);
        extraction.series.close.push_back(bar.close);
        extraction.series.volume.push_back(bar.volume);
        extraction.timestampsMs.push_back(static_cast<int64_t>(bar.time) * 1000);
    }

    return extraction;
}

void IndicatorBuilderWindow::BuildTimestampCaches(const std::vector<int64_t>& timestampsMs) {
    m_timestampMs = timestampsMs;
    m_timestampSeconds.resize(timestampsMs.size());
    for (size_t i = 0; i < timestampsMs.size(); ++i) {
        m_timestampSeconds[i] = static_cast<double>(timestampsMs[i]) / 1000.0;
    }
    m_currentRowCount = timestampsMs.size();
}

void IndicatorBuilderWindow::BeginBuild() {
    if (m_buildState == BuildState::Computing) {
        return;
    }

    std::string validationError;
    if (!EnsureDefinitionsReady(&validationError)) {
        HandleBuildFailure(validationError);
        return;
    }

    auto extraction = ExtractSeriesFromOhlcv(&validationError);
    if (!extraction.has_value()) {
        HandleBuildFailure(validationError);
        return;
    }

    BuildTimestampCaches(extraction->timestampsMs);
    ClearResults();

    auto definitions = m_parsedDefinitions;
    auto series = std::move(extraction->series);

    m_buildState = BuildState::Computing;
    m_statusIsError = false;
    m_statusMessage = "Computing indicators...";
    m_buildStartTime = std::chrono::steady_clock::now();

    m_buildFuture = std::async(std::launch::async, [definitions, series = std::move(series)]() mutable {
        return RunBuildJob(definitions, std::move(series));
    });
}

void IndicatorBuilderWindow::PollBuildFuture() {
    if (!m_buildFuture.valid()) {
        return;
    }

    auto status = m_buildFuture.wait_for(std::chrono::milliseconds(0));
    if (status != std::future_status::ready) {
        return;
    }

    BuildJobResult result = m_buildFuture.get();
    if (!result.success) {
        HandleBuildFailure(result.errorMessage);
        return;
    }

    HandleBuildSuccess(std::move(result));
}

void IndicatorBuilderWindow::ClearResults() {
    m_indicatorNames.clear();
    m_indicatorValues.clear();
    m_columnHeaders.clear();
    m_displayCache.clear();
    m_selectedColumnIndex = -1;
    m_selectedIndicator.clear();
    m_plotValues.clear();
    m_plotTimes.clear();
}

void IndicatorBuilderWindow::HandleBuildSuccess(BuildJobResult&& result) {
    m_indicatorNames = std::move(result.indicatorNames);
    m_indicatorValues = std::move(result.indicatorValues);

    size_t alignedRows = std::min(m_timestampMs.size(), result.rowCount);
    m_timestampMs.resize(alignedRows);
    m_timestampSeconds.resize(alignedRows);
    for (size_t i = 0; i < alignedRows; ++i) {
        m_timestampSeconds[i] = static_cast<double>(m_timestampMs[i]) / 1000.0;
    }

    for (auto& column : m_indicatorValues) {
        if (column.size() < alignedRows) {
            column.resize(alignedRows, std::numeric_limits<double>::quiet_NaN());
        } else if (column.size() > alignedRows) {
            column.resize(alignedRows);
        }
    }

    m_currentRowCount = alignedRows;
    UpdateDisplayCache();
    m_selectedIndicator.clear();
    m_selectedColumnIndex = -1;
    m_plotValues.clear();
    m_plotTimes.clear();

    m_buildState = BuildState::Ready;
    m_statusIsError = false;
    auto elapsed = std::chrono::steady_clock::now() - m_buildStartTime;
    m_lastBuildDurationMs = std::chrono::duration<double, std::milli>(elapsed).count();
    std::ostringstream oss;
    oss << "Built " << m_indicatorNames.size() << " indicator(s) across " << alignedRows
        << " rows in " << std::fixed << std::setprecision(1) << m_lastBuildDurationMs << " ms.";
    m_statusMessage = oss.str();
}

void IndicatorBuilderWindow::HandleBuildFailure(const std::string& message) {
    m_buildState = BuildState::Error;
    m_statusIsError = true;
    m_statusMessage = message.empty() ? "Indicator build failed." : message;
    m_buildFuture = std::future<BuildJobResult>();
}

void IndicatorBuilderWindow::UpdateDisplayCache() {
    m_displayCache.clear();
    m_columnHeaders.clear();

    if (!HasResults() || m_timestampMs.empty()) {
        return;
    }

    m_columnHeaders = {"Date", "Time", "timestamp_unix"};
    m_columnHeaders.insert(m_columnHeaders.end(), m_indicatorNames.begin(), m_indicatorNames.end());

    const size_t numRows = std::min(static_cast<size_t>(kMaxDisplayRows), m_currentRowCount);
    const int numColumns = static_cast<int>(m_columnHeaders.size());
    m_displayCache.resize(numRows, std::vector<std::string>(numColumns));

    for (size_t row = 0; row < numRows; ++row) {
        const auto timestampMs = m_timestampMs[row];
        auto& cacheRow = m_displayCache[row];
        cacheRow[0] = FormatDate(timestampMs);
        cacheRow[1] = FormatTime(timestampMs);
        cacheRow[2] = std::to_string(timestampMs);

        for (size_t col = 0; col < m_indicatorNames.size(); ++col) {
            size_t targetIndex = kMetadataColumns + col;
            if (row < m_indicatorValues[col].size()) {
                cacheRow[targetIndex] = FormatNumeric(m_indicatorValues[col][row]);
            } else {
                cacheRow[targetIndex] = "N/A";
            }
        }
    }
}

void IndicatorBuilderWindow::UpdatePlotCache() {
    m_plotValues.clear();
    m_plotTimes.clear();

    if (m_selectedColumnIndex < kMetadataColumns) {
        return;
    }

    size_t indicatorIndex = static_cast<size_t>(m_selectedColumnIndex - kMetadataColumns);
    if (indicatorIndex >= m_indicatorValues.size()) {
        return;
    }

    const auto& values = m_indicatorValues[indicatorIndex];
    size_t count = std::min(values.size(), m_timestampSeconds.size());
    if (count == 0) {
        return;
    }

    m_plotValues.assign(values.begin(), values.begin() + count);
    m_plotTimes.assign(m_timestampSeconds.begin(), m_timestampSeconds.begin() + count);
}

std::string IndicatorBuilderWindow::Trim(const std::string& value) {
    return TrimCopy(value);
}

bool IndicatorBuilderWindow::IsCommentLine(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    char first = value[0];
    return first == ';' || first == '#';
}

std::string IndicatorBuilderWindow::FormatDate(int64_t timestampMs) {
    auto tm = ToUtcTm(timestampMs / 1000);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return buffer;
}

std::string IndicatorBuilderWindow::FormatTime(int64_t timestampMs) {
    auto tm = ToUtcTm(timestampMs / 1000);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm);
    return buffer;
}

std::string IndicatorBuilderWindow::FormatNumeric(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (!std::isfinite(value)) {
        return value > 0 ? "Inf" : "-Inf";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

IndicatorBuilderWindow::BuildJobResult IndicatorBuilderWindow::RunBuildJob(
    std::vector<tssb::IndicatorDefinition> definitions,
    tssb::SingleMarketSeries series) {
    BuildJobResult result;
    try {
        auto tasks = tssb::BatchIndicatorComputer::compute_from_series(series, definitions, true);
        if (tasks.empty()) {
            result.errorMessage = "Indicator engine returned no results.";
            return result;
        }

        result.rowCount = series.size();
        result.indicatorNames.reserve(tasks.size());
        result.indicatorValues.reserve(tasks.size());
        for (const auto& task : tasks) {
            result.indicatorNames.push_back(task.variable_name);
            result.indicatorValues.push_back(task.result.values);
        }
        result.success = true;
    } catch (const std::exception& ex) {
        result.errorMessage = ex.what();
    } catch (...) {
        result.errorMessage = "Unknown error during indicator computation.";
    }
    return result;
}

bool IndicatorBuilderWindow::HasOhlcvData() const {
    if (!m_candlestickChart) {
        return false;
    }
    const auto& raw = m_candlestickChart->GetOhlcvData().getRawData();
    return !raw.empty();
}

#include "TimeSeriesWindow.h"
#include "HistogramWindow.h"
#include "BivarAnalysisWidget.h"
#include "ESSWindow.h"
#include "LFSWindow.h"
#include "HMMTargetWindow.h"
#include "HMMMemoryWindow.h"
#include "StationarityWindow.h"
#include "FSCAWindow.h"
#include "stage1_metadata_writer.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <cstddef>
#include <memory>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <optional>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#ifdef _WIN32
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#ifdef left
#undef left
#endif
#ifdef right
#undef right
#endif
#endif
#include <arrow/array.h>
#include <arrow/compute/api.h>
#include <arrow/compute/registry.h>
#include <arrow/scalar.h>
#include <arrow/status.h>
#include <arrow/type.h>

#include "QuestDbDataFrameGateway.h"

namespace {
std::string SanitizeSlug(const std::string& value) {
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

std::string ParseSymbolFromName(const std::string& measurement) {
    std::string symbol;
    for (char ch : measurement) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            symbol.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        } else {
            break;
        }
    }
    return symbol;
}

std::string ParseGranularity(const std::string& measurement) {
    auto pos = measurement.find("_1m");
    if (pos != std::string::npos) return "1m";
    pos = measurement.find("_5m");
    if (pos != std::string::npos) return "5m";
    pos = measurement.find("_15m");
    if (pos != std::string::npos) return "15m";
    pos = measurement.find("_30m");
    if (pos != std::string::npos) return "30m";
    pos = measurement.find("_1h");
    if (pos != std::string::npos) return "1h";
    pos = measurement.find("_4h");
    if (pos != std::string::npos) return "4h";
    pos = measurement.find("_1d");
    if (pos != std::string::npos) return "1d";
    return "unknown";
}


std::optional<int64_t> GetValueAt(const std::shared_ptr<arrow::Array>& array, int64_t index) {
    if (!array || index < 0 || index >= array->length()) {
        return std::nullopt;
    }
    using arrow::Type;
    switch (array->type_id()) {
        case Type::INT64: {
            auto typed = std::static_pointer_cast<arrow::Int64Array>(array);
            if (typed->IsValid(index)) {
                return typed->Value(index);
            }
            break;
        }
        case Type::INT32: {
            auto typed = std::static_pointer_cast<arrow::Int32Array>(array);
            if (typed->IsValid(index)) {
                return static_cast<int64_t>(typed->Value(index));
            }
            break;
        }
        case Type::DOUBLE: {
            auto typed = std::static_pointer_cast<arrow::DoubleArray>(array);
            if (typed->IsValid(index)) {
                return static_cast<int64_t>(std::llround(typed->Value(index)));
            }
            break;
        }
        case Type::FLOAT: {
            auto typed = std::static_pointer_cast<arrow::FloatArray>(array);
            if (typed->IsValid(index)) {
                return static_cast<int64_t>(std::llround(typed->Value(index)));
            }
            break;
        }
        default:
            break;
    }
    return std::nullopt;
}
}

// Static initialization to ensure Arrow compute functions are initialized only once
static bool InitializeArrowCompute() {
    // Initialize Arrow compute functions including all function registries
    auto init_status = arrow::compute::Initialize();
    if (!init_status.ok()) {
        std::cerr << "[TimeSeriesWindow] Failed to initialize Arrow compute functions: " 
                  << init_status.ToString() << std::endl;
        return false;
    }
    
    // Register additional function registries that may contain modulo
    auto registry = arrow::compute::GetFunctionRegistry();
    if (!registry) {
        std::cerr << "[TimeSeriesWindow] Failed to get Arrow function registry" << std::endl;
        return false;
    }
    
    return true;
}

TimeSeriesWindow::TimeSeriesWindow()
    : m_isVisible(false)
    , m_dataFrame(nullptr)
    , m_isLoading(false)
    , m_hasError(false)
    , m_selectedColumnIndex(-1)
    , m_tableHeight(DEFAULT_TABLE_HEIGHT)
    , m_autoFitPlot(true)
    , m_plotHeight(DEFAULT_PLOT_HEIGHT)
    , m_plotDataDirty(true)
    , m_histogramWindow(nullptr)
    , m_bivarAnalysisWidget(nullptr)
    , m_essWindow(nullptr)
    , m_lfsWindow(nullptr)
    , m_hmmTargetWindow(nullptr)
    , m_hmmMemoryWindow(nullptr)
    , m_stationarityWindow(nullptr)
    , m_fscaWindow(nullptr)
    , m_detectedTimeFormat(chronosflow::TimeFormat::NONE)
{
    // Ensure Arrow compute functions are initialized (called only once)
    static bool initialized = InitializeArrowCompute();
    
    // Initialize file path buffer
    memset(m_filePathBuffer, 0, sizeof(m_filePathBuffer));
    memset(m_tableNameBuffer, 0, sizeof(m_tableNameBuffer));
    m_isExporting = false;
    m_lastExportSuccess = false;
    m_exportStatusMessage.clear();
    memset(m_importTableBuffer, 0, sizeof(m_importTableBuffer));
    m_isQuestDBFetching = false;
    m_lastQuestDBFetchSuccess = false;
    m_questdbStatusMessage.clear();


    // Column headers are now loaded dynamically.
    
    // Set up table flags - simplified for proper header display and column sizing
    m_tableFlags = ImGuiTableFlags_Borders |
                   ImGuiTableFlags_RowBg |
                   ImGuiTableFlags_ScrollY |
                   ImGuiTableFlags_ScrollX |
                   ImGuiTableFlags_Resizable |
                   ImGuiTableFlags_Sortable;
    
    ResetUIState();
}

void TimeSeriesWindow::LoadDatasetFromMetadata(const DatasetMetadata& metadata) {
    m_activeDataset = metadata;
    std::string measurement = metadata.indicator_measurement.empty()
        ? metadata.dataset_slug
        : metadata.indicator_measurement;
    if (measurement.empty()) {
        measurement = metadata.dataset_id;
    }
    if (!measurement.empty()) {
        m_lastQuestDbMeasurement = measurement;
        LoadQuestDBTable(measurement);
    }
}

void TimeSeriesWindow::ClearActiveDataset() {
    m_activeDataset.reset();
}

bool TimeSeriesWindow::ExportDataset(const std::string& measurement,
                                     std::string* statusMessage,
                                     DatasetMetadata* metadataOut,
                                     bool recordMetadata) {
    std::string sanitized = SanitizeSlug(measurement);
    if (sanitized.empty()) {
        if (statusMessage) {
            *statusMessage = "Invalid dataset name.";
        }
        return false;
    }

    std::string status;
    bool success = ExportToQuestDB(sanitized, status);
    if (statusMessage) {
        *statusMessage = status;
    }
    if (!success) {
        return false;
    }
    m_lastQuestDbMeasurement = sanitized;
    DatasetMetadata metadata;
    metadata.dataset_slug = sanitized;
    metadata.indicator_measurement = sanitized;
    metadata.ohlcv_measurement.clear();
    metadata.dataset_id = Stage1MetadataWriter::MakeDeterministicUuid(measurement);
    metadata.indicator_rows = static_cast<int64_t>(GetRowCount());
    m_activeDataset = metadata;
    if (metadataOut) {
        *metadataOut = metadata;
    }
    if (recordMetadata) {
        Stage1MetadataWriter::DatasetRecord record;
        record.dataset_id = metadata.dataset_id;
        record.dataset_slug = metadata.dataset_slug.empty() ? sanitized : metadata.dataset_slug;
        record.symbol = ParseSymbolFromName(sanitized);
        record.granularity = ParseGranularity(sanitized);
        record.source = "laptop_imgui";
        record.indicator_measurement = sanitized;
        record.ohlcv_measurement = metadata.ohlcv_measurement.empty() ? sanitized : metadata.ohlcv_measurement;
        record.indicator_row_count = static_cast<int64_t>(GetRowCount());
        record.ohlcv_row_count = metadata.ohlcv_rows;
        auto bounds = GetTimestampBounds();
        record.indicator_first_timestamp_unix = bounds.first;
        record.indicator_last_timestamp_unix = bounds.second;
        record.ohlcv_first_timestamp_unix = std::nullopt;
        record.ohlcv_last_timestamp_unix = std::nullopt;
        record.created_at = std::chrono::system_clock::now();
        Stage1MetadataWriter::Instance().RecordDatasetExport(record);
    }
    return true;
}

void TimeSeriesWindow::SetActiveDatasetMetadata(const DatasetMetadata& metadata) {
    m_activeDataset = metadata;
}

std::pair<std::optional<int64_t>, std::optional<int64_t>> TimeSeriesWindow::GetTimestampBounds() const {
    if (!m_dataFrame) {
        return {};
    }
    return ExtractTimestampBounds(m_dataFrame->get_cpu_table());
}

void TimeSeriesWindow::Draw() {
    if (!m_isVisible) {
        return;
    }
    
    // Check if a loading operation has completed
    if (m_isLoading && m_loadingFuture.valid() &&
        m_loadingFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        
        auto result = m_loadingFuture.get();
        bool wasQuestDBFetch = m_isQuestDBFetching;
        auto finishLoading = [&]() {
            m_isLoading = false;
            m_isQuestDBFetching = false;
        };

        if (result.ok()) {
            auto df = std::make_unique<chronosflow::AnalyticsDataFrame>(std::move(result).ValueOrDie());

            const auto& columns = df->column_names();
            auto find_case_insensitive = [](const std::vector<std::string>& cols, const std::string& name_to_find) {
                return std::find_if(cols.begin(), cols.end(), [&](const std::string& col_name) {
                    std::string lower_name = col_name;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                    std::string target_name = name_to_find;
                    std::transform(target_name.begin(), target_name.end(), target_name.begin(), ::tolower);
                    return lower_name == target_name;
                });
            };

            auto date_it = find_case_insensitive(columns, "Date");
            auto time_it = find_case_insensitive(columns, "Time");

            if (date_it == columns.end()) {
                m_hasError = true;
                m_errorMessage = "Required 'Date' column not found in file.";
                if (wasQuestDBFetch) {
                    m_lastQuestDBFetchSuccess = false;
                    m_questdbStatusMessage = m_errorMessage;
                }
                finishLoading();
                return;
            }

            df->set_tssb_metadata(*date_it, (time_it != columns.end()) ? *time_it : "");

            m_detectedTimeFormat = chronosflow::TimeFormat::NONE;
            if (time_it != columns.end()) {
                auto time_col = df->get_cpu_table()->GetColumnByName(*time_it);
                for (int64_t i = 0; i < time_col->length(); ++i) {
                    auto scalar_res = time_col->GetScalar(i);
                    if (scalar_res.ok() && scalar_res.ValueOrDie()->is_valid) {
                        int64_t time_val = std::static_pointer_cast<arrow::Int64Scalar>(scalar_res.ValueOrDie())->value;
                        if (std::to_string(time_val).length() > 4) {
                            m_detectedTimeFormat = chronosflow::TimeFormat::HHMMSS;
                        } else {
                            m_detectedTimeFormat = chronosflow::TimeFormat::HHMM;
                        }
                        break;
                    }
                }
            }

            if (df->has_tssb_metadata()) {
                auto ts_result = df->with_unix_timestamp("timestamp_unix", m_detectedTimeFormat);
                if (ts_result.ok()) {
                    *df = std::move(ts_result).ValueOrDie();
                } else {
                    m_hasError = true;
                    m_errorMessage = "Failed to create Unix timestamps: " + ts_result.status().ToString();
                    if (wasQuestDBFetch) {
                        m_lastQuestDBFetchSuccess = false;
                        m_questdbStatusMessage = m_errorMessage;
                    }
                    finishLoading();
                    return;
                }
            }

            m_dataFrame = std::move(df);
            m_columnHeaders = m_dataFrame->column_names();

            UpdateDisplayCache();

            m_hasError = false;
            m_errorMessage.clear();

            if (wasQuestDBFetch) {
                m_lastQuestDBFetchSuccess = true;
                std::string tableName = m_loadedFilePath;
                auto pos = tableName.find(':');
                if (pos != std::string::npos) {
                    tableName = tableName.substr(pos + 1);
                }
                m_questdbStatusMessage = "Loaded QuestDB table '" + tableName + "'.";
            }

            std::cout << "[TimeSeriesWindow] Loaded " << m_dataFrame->num_rows()
                      << " rows with " << m_dataFrame->num_columns() << " columns" << std::endl;
        } else {
            m_hasError = true;
            m_errorMessage = result.status().ToString();
            if (wasQuestDBFetch) {
                m_lastQuestDBFetchSuccess = false;
                m_questdbStatusMessage = m_errorMessage;
            }
            std::cout << "[TimeSeriesWindow] Failed to load: " << m_errorMessage << std::endl;
        }
        finishLoading();
    }
    
    ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Time Series Window", &m_isVisible)) {
        DrawFileControls();
        ImGui::Separator();
        
        if (m_hasError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::Text("Error: %s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        
        if (HasData()) {
            // Calculate available height for table and plot
            float availableHeight = ImGui::GetContentRegionAvail().y - STATUS_BAR_HEIGHT - 10.0f;
            
            // Draw data table (resizable) - simplified container without conflicting scrollbars
            ImGui::BeginChild("TableSection", ImVec2(0, m_tableHeight), true);
            DrawDataTable();
            ImGui::EndChild();
            
            // Resize handle
            ImGui::InvisibleButton("##resize", ImVec2(-1, 8));
            if (ImGui::IsItemActive()) {
                m_tableHeight += ImGui::GetIO().MouseDelta.y;
                m_tableHeight = std::max(150.0f, std::min(m_tableHeight, availableHeight - 200.0f));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            }
            
            ImGui::Separator();
            
            // Draw plot area
            float plotAreaHeight = availableHeight - m_tableHeight - 20.0f;
            ImGui::BeginChild("PlotSection", ImVec2(0, plotAreaHeight), true);
            DrawPlotArea();
            ImGui::EndChild();
        } else if (!m_isLoading) {
            ImGui::Text("No data loaded. Select a CSV file and click 'Load' to begin.");
        }
        
        DrawStatusBar();
    }
    ImGui::End();
}

void TimeSeriesWindow::DrawFileControls() {
    ImGui::Text("CSV File:");
    ImGui::SameLine();
    
    // File path input
    ImGui::SetNextItemWidth(400.0f);
    ImGui::InputText("##filepath", m_filePathBuffer, sizeof(m_filePathBuffer));
    
    ImGui::SameLine();
    
    // Browse button (placeholder - would need platform-specific file dialog)
    if (ImGui::Button("Browse")) {
        // TODO: Implement file dialog
        std::cout << "File dialog not implemented yet" << std::endl;
    }
    
    ImGui::SameLine();
    
    // Load button
    if (ImGui::Button("Load") && !m_isLoading && strlen(m_filePathBuffer) > 0) {
        LoadCSVFile(std::string(m_filePathBuffer));
    }
    
    ImGui::SameLine();
    
    // Clear button
    if (ImGui::Button("Clear") && !m_isLoading) {
        ClearData();
    }
    
    ImGui::SameLine();
    
    // Bivariate Analysis button
    if (ImGui::Button("Bivariate Analysis") && HasData()) {
        if (m_bivarAnalysisWidget) {
            m_bivarAnalysisWidget->SetDataSource(this);
            m_bivarAnalysisWidget->UpdateColumnList();
            m_bivarAnalysisWidget->SetVisible(true);
        }
    }
    
    ImGui::SameLine();
    
    // ESS (Enhanced Stepwise Selection) button
    if (ImGui::Button("ESS") && HasData()) {
        if (m_essWindow) {
            m_essWindow->SetDataSource(this);
            m_essWindow->UpdateColumnList();
            m_essWindow->SetVisible(true);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enhanced Stepwise Selection - Feature selection algorithm");
    }
    
    ImGui::SameLine();
    
    // LFS (Local Feature Selection) button
    if (ImGui::Button("LFS") && HasData()) {
        if (m_lfsWindow) {
            m_lfsWindow->SetDataSource(this);
            m_lfsWindow->UpdateColumnList();
            m_lfsWindow->SetVisible(true);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Local Feature Selection - Advanced feature selection with CUDA support");
    }

    ImGui::SameLine();

    if (ImGui::Button("HMM Target") && HasData()) {
        if (m_hmmTargetWindow) {
            m_hmmTargetWindow->SetDataSource(this);
            m_hmmTargetWindow->UpdateColumnList();
            m_hmmTargetWindow->SetVisible(true);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hidden Markov Models with target correlation assessment");
    }

    ImGui::SameLine();

    if (ImGui::Button("HMM Memory") && HasData()) {
        if (m_hmmMemoryWindow) {
            m_hmmMemoryWindow->SetDataSource(this);
            m_hmmMemoryWindow->UpdateColumnList();
            m_hmmMemoryWindow->SetVisible(true);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Assess HMM memory and temporal structure");
    }

    ImGui::SameLine();

    if (ImGui::Button("Stationarity") && HasData()) {
        if (m_stationarityWindow) {
            m_stationarityWindow->SetDataSource(this);
            m_stationarityWindow->UpdateColumnList();
            m_stationarityWindow->SetVisible(true);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Test for break in mean (stationarity)");
    }

    ImGui::SameLine();

    if (ImGui::Button("FSCA") && HasData()) {
        if (m_fscaWindow) {
            m_fscaWindow->SetDataSource(this);
            m_fscaWindow->UpdateColumnList();
            m_fscaWindow->SetVisible(true);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Forward Selection Component Analysis");
    }
    
    // Loading indicator
    if (m_isLoading) {
        ImGui::SameLine();
        ImGui::Text("Loading...");
    }
    
    // File info
    if (!m_loadedFilePath.empty()) {
        ImGui::Text("Loaded: %s", m_loadedFilePath.c_str());
    }

    DrawExportControls();
    DrawQuestDBImportControls();
}

void TimeSeriesWindow::DrawExportControls() {
    if (!HasData()) {
        return;
    }

    ImGui::Separator();
    ImGui::Text("QuestDB Export");

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Table Name", m_tableNameBuffer, sizeof(m_tableNameBuffer));
    ImGui::SameLine();

    bool readyForExport = !m_isLoading && !m_isExporting && (std::strlen(m_tableNameBuffer) > 0);
    ImGui::BeginDisabled(!readyForExport);
    if (ImGui::Button("Export to QuestDB")) {
        TriggerQuestDBExport();
    }
    ImGui::EndDisabled();

    if (m_isExporting) {
        ImGui::SameLine();
        ImGui::Text("Exporting...");
    }

    if (!m_exportStatusMessage.empty()) {
        ImVec4 color = m_lastExportSuccess ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f)
                                           : ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%s", m_exportStatusMessage.c_str());
    }
}

void TimeSeriesWindow::DrawQuestDBImportControls() {
    ImGui::Separator();
    ImGui::Text("QuestDB Import");

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Table", m_importTableBuffer, sizeof(m_importTableBuffer));
    ImGui::SameLine();

    bool ready = !m_isLoading && !m_isQuestDBFetching && std::strlen(m_importTableBuffer) > 0;
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Load from QuestDB")) {
        ClearActiveDataset();
        LoadQuestDBTable(m_importTableBuffer);
    }
    ImGui::EndDisabled();

    if (m_isQuestDBFetching) {
        ImGui::SameLine();
        ImGui::Text("Loading...");
    }

    if (!m_questdbStatusMessage.empty()) {
        ImVec4 color = m_lastQuestDBFetchSuccess ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f)
                                                 : ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%s", m_questdbStatusMessage.c_str());
    }
}

void TimeSeriesWindow::DrawDataTable() {
    if (m_columnHeaders.empty() || m_displayCache.empty()) return;

    const size_t numRows = m_displayCache.size();
    const int numColumns = static_cast<int>(m_columnHeaders.size());

    if (ImGui::BeginTable("TimeSeriesTable", numColumns, m_tableFlags)) {
        // Setup columns
        for (int i = 0; i < numColumns; ++i) {
            ImGui::TableSetupColumn(m_columnHeaders[i].c_str());
        }
        ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
        ImGui::TableHeadersRow();

        // Handle column header clicks for selection
        if (ImGui::TableGetSortSpecs() && ImGui::TableGetSortSpecs()->SpecsDirty) {
            const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
            if (sortSpecs->SpecsCount > 0) {
                int clickedColumn = sortSpecs->Specs[0].ColumnIndex;
                if (clickedColumn >= 2) { // Skip date/time columns (first two)
                    m_selectedColumnIndex = clickedColumn;
                    m_selectedIndicator = m_columnHeaders[clickedColumn];
                    m_plotDataDirty = true;
                    
                    // Notify histogram window of column selection
                    NotifyColumnSelection(m_selectedIndicator, clickedColumn);
                }
            }
            ImGui::TableGetSortSpecs()->SpecsDirty = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(numRows);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow();
                for (int col = 0; col < numColumns; ++col) {
                    ImGui::TableSetColumnIndex(col);
                    // --- PERFORMANCE FIX: Read from cache ---
                    ImGui::TextUnformatted(m_displayCache[row][col].c_str());

                    if (col == m_selectedColumnIndex) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
                    }
                }
            }
        }
        
        // Show pagination info if data is truncated
        if (m_dataFrame && m_dataFrame->num_rows() > MAX_DISPLAY_ROWS) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("... (%lld more rows)", m_dataFrame->num_rows() - MAX_DISPLAY_ROWS);
        }
        
        ImGui::EndTable();
    }
}

void TimeSeriesWindow::DrawPlotArea() {
    if (!HasData()) {
        ImGui::Text("No data to plot");
        return;
    }
    
    // Indicator selection info
    if (m_selectedIndicator.empty()) {
        ImGui::Text("Click on a column header in the table above to select an indicator to plot");
        return;
    }
    
    ImGui::Text("Selected Indicator: %s", m_selectedIndicator.c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Auto-fit", &m_autoFitPlot);
    
    // Update cached plot data if needed
    if (m_plotDataDirty || m_cachedIndicatorName != m_selectedIndicator) {
        UpdatePlotData();
    }
    
    if (m_cachedPlotTimes.empty()) {
        ImGui::Text("No data available for selected indicator");
        return;
    }
    
    // Create plot using cached data
    if (ImPlot::BeginPlot(m_selectedIndicator.c_str(), ImVec2(-1, -1))) {
        // Setup time axis
        ImPlot::SetupAxisFormat(ImAxis_X1, "%Y-%m-%d");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        
        if (m_autoFitPlot && !m_cachedPlotValues.empty()) {
            // Calculate actual data range for Y-axis
            auto minmax = std::minmax_element(m_cachedPlotValues.begin(), m_cachedPlotValues.end());
            double minVal = *minmax.first;
            double maxVal = *minmax.second;
            
            // Add some padding (5% on each side) for better visualization
            double range = maxVal - minVal;
            double padding = range * 0.05;
            if (range == 0.0) {
                // Handle case where all values are the same
                padding = std::abs(minVal) * 0.1;
                if (padding == 0.0) padding = 1.0; // Fallback for zero values
            }
            
            ImPlot::SetupAxisLimits(ImAxis_Y1, minVal - padding, maxVal + padding, ImGuiCond_Always);
        }
        
        if (!m_cachedPlotTimes.empty()) {
            ImPlot::PlotLine(m_selectedIndicator.c_str(),
                           m_cachedPlotTimes.data(), m_cachedPlotValues.data(),
                           static_cast<int>(m_cachedPlotTimes.size()));
        } else {
            ImPlot::PlotText("No valid data points to plot", 0.5, 0.5);
        }
        
        ImPlot::EndPlot();
    }
}

void TimeSeriesWindow::DrawStatusBar() {
    ImGui::Separator();
    
    if (HasData()) {
        ImGui::Text("Ready | %lld rows | %lld columns", 
                   m_dataFrame->num_rows(), m_dataFrame->num_columns());
        
        if (!m_selectedIndicator.empty()) {
            ImGui::SameLine();
            ImGui::Text("| %s selected", m_selectedIndicator.c_str());
        }
    } else if (m_isLoading) {
        ImGui::Text("Loading...");
    } else {
        ImGui::Text("No data loaded");
    }
}

void TimeSeriesWindow::LoadCSVFile(const std::string& filepath) {
    if (m_isLoading) return; // Already loading
    ClearActiveDataset();
    m_isLoading = true;
    m_hasError = false;
    m_errorMessage.clear();
    m_loadedFilePath = filepath; // Store path immediately

    // Launch the loading task in the background
    m_loadingFuture = std::async(std::launch::async, [filepath]() {
        chronosflow::TSSBReadOptions options;
        options.auto_detect_delimiter = true;
        // Tell the loader that the file DEFINITELY has a header.
        options.has_header = true; 
        
        // Let the generic loader run. We will find the date/time columns later.
        return chronosflow::DataFrameIO::read_tssb(filepath, options);
    });
}

void TimeSeriesWindow::TriggerQuestDBExport() {
    if (m_isExporting || !HasData()) {
        return;
    }

    std::string tableName = m_tableNameBuffer;
    auto trim = [](std::string& value) {
        auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    };
    trim(tableName);

    if (tableName.empty()) {
        m_lastExportSuccess = false;
        m_exportStatusMessage = "Table name is required.";
        return;
    }

    m_isExporting = true;
    m_exportStatusMessage.clear();

    std::string statusMessage;
    bool success = ExportDataset(tableName, &statusMessage, nullptr);

    m_isExporting = false;
    m_lastExportSuccess = success;
    m_exportStatusMessage = std::move(statusMessage);
}

void TimeSeriesWindow::LoadQuestDBTable(const std::string& tableName) {
    if (m_isLoading) {
        return;
    }

    std::string trimmedName = tableName;
    auto trim = [](std::string& value) {
        auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    };
    trim(trimmedName);

    if (trimmedName.empty()) {
        m_lastQuestDBFetchSuccess = false;
        m_questdbStatusMessage = "Table name is required.";
        return;
    }

    m_isLoading = true;
    m_isQuestDBFetching = true;
    m_hasError = false;
    m_errorMessage.clear();
    m_questdbStatusMessage.clear();
    m_lastQuestDBFetchSuccess = false;
    m_loadedFilePath = "QuestDB:" + trimmedName;

    m_loadingFuture = std::async(std::launch::async, [trimmedName]() {
        questdb::DataFrameGateway gateway;
        return gateway.Import(trimmedName);
    });
}

bool TimeSeriesWindow::ExportToQuestDB(const std::string& tableName, std::string& statusMessage) {
    if (!m_dataFrame) {
        statusMessage = "No data loaded.";
        return false;
    }

    questdb::DataFrameGateway gateway;
    questdb::ExportSpec spec;
    spec.measurement = tableName;
    const std::string datasetId = Stage1MetadataWriter::MakeDeterministicUuid(tableName);
    spec.static_tags["dataset_id"] = datasetId;

    questdb::ExportResult exportResult;
    std::string error;
    if (!gateway.Export(*m_dataFrame, spec, &exportResult, &error)) {
        statusMessage = error.empty() ? "QuestDB export failed." : error;
        return false;
    }

    auto table = m_dataFrame->get_cpu_table();
    if (!table) {
        statusMessage = "Export succeeded but CPU table unavailable for metadata.";
        return false;
    }

    m_lastQuestDbMeasurement = tableName;

    statusMessage = "Exported " + std::to_string(exportResult.rows_serialized) + " rows to QuestDB table '" + tableName + "'.";
    return true;
}



std::pair<std::optional<int64_t>, std::optional<int64_t>> TimeSeriesWindow::ExtractTimestampBounds(const std::shared_ptr<arrow::Table>& table) const {
    std::optional<int64_t> first;
    std::optional<int64_t> last;
    if (!table) {
        return {first, last};
    }

    static const std::vector<std::string> candidateNames = {
        "timestamp_unix",
        "timestamp",
        "timestamp_seconds",
        "timestamp_unix_s"
    };

    for (const auto& name : candidateNames) {
        auto column = table->GetColumnByName(name);
        if (!column) {
            continue;
        }

        // First value
        for (int chunk_idx = 0; chunk_idx < column->num_chunks() && !first.has_value(); ++chunk_idx) {
            auto chunk = column->chunk(chunk_idx);
            for (int64_t i = 0; i < chunk->length(); ++i) {
                auto value = GetValueAt(chunk, i);
                if (value.has_value()) {
                    first = value;
                    break;
                }
            }
        }

        // Last value
        for (int chunk_idx = column->num_chunks() - 1; chunk_idx >= 0 && !last.has_value(); --chunk_idx) {
            auto chunk = column->chunk(chunk_idx);
            for (int64_t i = chunk->length() - 1; i >= 0; --i) {
                auto value = GetValueAt(chunk, i);
                if (value.has_value()) {
                    last = value;
                    break;
                }
            }
        }

        if (first.has_value() && last.has_value()) {
            break;
        }
    }

    return {first, last};
}


void TimeSeriesWindow::ClearData() {
    m_dataFrame.reset();
    m_activeDataset.reset();
    m_displayCache.clear(); // <<<--- ADD THIS CALL HERE
    m_loadedFilePath.clear();
    m_columnHeaders.clear(); // Clear the headers.
    m_isExporting = false;
    m_lastExportSuccess = false;
    m_exportStatusMessage.clear();
    m_isQuestDBFetching = false;
    m_lastQuestDBFetchSuccess = false;
    m_questdbStatusMessage.clear();

    // Clear histogram window if connected
    if (m_histogramWindow) {
        m_histogramWindow->ClearHistogram();
        m_histogramWindow->SetVisible(false);
    }
    
    ResetUIState();
    std::cout << "[TimeSeriesWindow] Data cleared" << std::endl;
}

void TimeSeriesWindow::ResetUIState() {
    m_selectedColumnIndex = -1;
    m_selectedIndicator.clear();
    m_hasError = false;
    m_errorMessage.clear();
    m_tableHeight = DEFAULT_TABLE_HEIGHT;
    m_plotHeight = DEFAULT_PLOT_HEIGHT;
    m_plotDataDirty = true;
    m_cachedPlotTimes.clear();
    m_cachedPlotValues.clear();
    m_cachedIndicatorName.clear();
    m_isExporting = false;
    m_lastExportSuccess = false;
    m_exportStatusMessage.clear();
    m_isQuestDBFetching = false;
    m_lastQuestDBFetchSuccess = false;
    m_questdbStatusMessage.clear();
}

void TimeSeriesWindow::UpdatePlotData() {
    m_cachedPlotTimes.clear();
    m_cachedPlotValues.clear();

    if (!m_dataFrame || m_selectedIndicator.empty() || m_cachedIndicatorName == m_selectedIndicator) {
        m_plotDataDirty = false;
        return;
    }

    // No more calls to with_unix_timestamp! 
    // The 'timestamp_unix' column is already guaranteed to exist.
    
    auto table = m_dataFrame->get_cpu_table();
    auto values_col_res = m_dataFrame->get_column_view<double>(m_selectedIndicator);
    auto times_col_res = m_dataFrame->get_column_view<int64_t>("timestamp_unix");

    if (!values_col_res.ok() || !times_col_res.ok()) {
        m_hasError = true;
        m_errorMessage = "Could not get column views for plotting.";
        m_plotDataDirty = false;
        return;
    }
    
    auto values_view = std::move(values_col_res).ValueOrDie();
    auto times_view = std::move(times_col_res).ValueOrDie();

    const double* values_data = values_view.data();
    const int64_t* times_data = times_view.data();
    size_t num_rows = values_view.size();
    
    m_cachedPlotValues.reserve(num_rows);
    m_cachedPlotTimes.reserve(num_rows);

    for (size_t i = 0; i < num_rows; ++i) {
        // Here we assume ColumnView gives valid data. You might add isnan checks.
        m_cachedPlotValues.push_back(values_data[i]);
        m_cachedPlotTimes.push_back(static_cast<double>(times_data[i]));
    }
    
    m_cachedIndicatorName = m_selectedIndicator;
    m_plotDataDirty = false;
    
    std::cout << "[TimeSeriesWindow] Updated plot cache for " << m_selectedIndicator << std::endl;
}


// Add the implementation for UpdateDisplayCache
void TimeSeriesWindow::UpdateDisplayCache() {
    m_displayCache.clear();
    if (!m_dataFrame) return;

    auto table = m_dataFrame->get_cpu_table();
    if (!table) return;

    const size_t numRows = std::min(static_cast<size_t>(MAX_DISPLAY_ROWS), static_cast<size_t>(table->num_rows()));
    const int numColumns = table->num_columns();
    
    m_displayCache.resize(numRows);
    for (size_t row = 0; row < numRows; ++row) {
        m_displayCache[row].resize(numColumns);
        for (int col = 0; col < numColumns; ++col) {
            auto column_data = table->column(col);
            auto scalar_result = column_data->GetScalar(row);
            if (scalar_result.ok()) {
                auto scalar = scalar_result.ValueOrDie();
                if (scalar->is_valid) {
                    m_displayCache[row][col] = scalar->ToString();
                } else {
                    m_displayCache[row][col] = "N/A";
                }
            } else {
                m_displayCache[row][col] = "[Error]";
            }
        }
    }
}

void TimeSeriesWindow::NotifyColumnSelection(const std::string& indicatorName, size_t columnIndex) {
    // Check if histogram window is connected and we have valid data
    if (m_histogramWindow && HasData()) {
        // Set the data source if not already set
        m_histogramWindow->SetDataSource(this);
        
        // Update the histogram with the selected column
        m_histogramWindow->UpdateHistogram(indicatorName, columnIndex);
        
        // Make the histogram window visible
        m_histogramWindow->SetVisible(true);
        
        std::cout << "[TimeSeriesWindow] Notified histogram window of column selection: "
                  << indicatorName << " (index: " << columnIndex << ")" << std::endl;
    }
}

void TimeSeriesWindow::SelectIndicator(const std::string& indicatorName, size_t columnIndex) {
    if (!m_dataFrame || indicatorName.empty()) {
        return;
    }
    
    // If columnIndex not provided (SIZE_MAX), find it
    if (columnIndex == SIZE_MAX) {
        columnIndex = GetColumnIndex(indicatorName);
        if (columnIndex == SIZE_MAX) {
            std::cerr << "[TimeSeriesWindow] Column not found: " << indicatorName << std::endl;
            return;
        }
    }
    
    // Validate column index
    if (columnIndex >= m_columnHeaders.size()) {
        std::cerr << "[TimeSeriesWindow] Invalid column index: " << columnIndex << std::endl;
        return;
    }
    
    // Update selected indicator for plotting
    m_selectedColumnIndex = static_cast<int>(columnIndex);
    m_selectedIndicator = indicatorName;
    m_plotDataDirty = true;
    
    // Notify histogram window
    NotifyColumnSelection(indicatorName, columnIndex);
    
    // Make this window visible if it's hidden
    SetVisible(true);
    
    std::cout << "[TimeSeriesWindow] Selected indicator: " << indicatorName 
              << " (index: " << columnIndex << ")" << std::endl;
}

void TimeSeriesWindow::SelectIndicatorByIndex(size_t columnIndex) {
    if (!m_dataFrame || columnIndex >= m_columnHeaders.size()) {
        return;
    }
    
    // Get column name and call main selection method
    const std::string& columnName = m_columnHeaders[columnIndex];
    SelectIndicator(columnName, columnIndex);
}

size_t TimeSeriesWindow::GetColumnIndex(const std::string& columnName) const {
    if (!m_dataFrame) {
        return SIZE_MAX;
    }
    
    // Search in cached column headers first (faster)
    for (size_t i = 0; i < m_columnHeaders.size(); ++i) {
        if (m_columnHeaders[i] == columnName) {
            return i;
        }
    }
    
    // Fallback to dataframe column names
    auto columnNames = m_dataFrame->column_names();
    for (size_t i = 0; i < columnNames.size(); ++i) {
        if (columnNames[i] == columnName) {
            return i;
        }
    }
    
    return SIZE_MAX; // Not found
}

std::string TimeSeriesWindow::GetFileDialogPath() {
    // TODO: Implement platform-specific file dialog
    // For now, return empty string
    return "";
}

int64_t TimeSeriesWindow::GetTimestamp(size_t row_index) const {
    if (!m_dataFrame || row_index >= m_dataFrame->num_rows()) {
        return 0;
    }
    
    // Get actual timestamp from the timestamp_unix column
    auto times_col_res = m_dataFrame->get_column_view<int64_t>("timestamp_unix");
    if (!times_col_res.ok()) {
        // Fallback to generated timestamps if column doesn't exist
        int64_t base_timestamp = 1609459200000;  // 2021-01-01 00:00:00 UTC in milliseconds
        int64_t hour_in_ms = 3600000;  // 1 hour in milliseconds
        return base_timestamp + (row_index * hour_in_ms);
    }
    
    auto timestamps_view = std::move(times_col_res).ValueOrDie();
    const int64_t* timestamps_data = timestamps_view.data();
    
    if (row_index < timestamps_view.size() && timestamps_data) {
        int64_t timestamp = timestamps_data[row_index];

        // Arrow timestamps produced by chronosflow are in seconds; align to milliseconds to
        // match the OHLCV cache used by the trade simulator. Keep existing millisecond data
        // (for example, fallback generations) by only scaling values that still look like seconds.
        if (timestamp != 0 && std::llabs(timestamp) < 4'000'000'000LL) {
            timestamp *= 1000;
        }

        return timestamp;
    }

    return 0;
}

std::string TimeSeriesWindow::GetSuggestedDatasetId() const {
    if (m_activeDataset && !m_activeDataset->dataset_slug.empty()) {
        return m_activeDataset->dataset_slug;
    }
    if (!m_lastQuestDbMeasurement.empty()) {
        return m_lastQuestDbMeasurement;
    }
    if (m_loadedFilePath.empty()) {
        return {};
    }
    constexpr const char* kQuestDbPrefix = "QuestDB:";
    if (m_loadedFilePath.rfind(kQuestDbPrefix, 0) == 0) {
        return SanitizeSlug(m_loadedFilePath.substr(std::strlen(kQuestDbPrefix)));
    }
    std::error_code ec;
    std::filesystem::path p(m_loadedFilePath);
    auto stem = p.stem().string();
    if (stem.empty()) {
        return {};
    }
    return SanitizeSlug(stem);
}

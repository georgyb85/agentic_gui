#pragma once

#include "imgui.h"
#include "implot.h"
#include "analytics_dataframe.h"
#include "dataframe_io.h"
#include "TimeSeries.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <bitset>
#include <cstdint>
#include <future>
#include <chrono>
#include <optional>
#include <utility>

// Forward declarations to avoid circular dependency
class HistogramWindow;
class BivarAnalysisWidget;
class ESSWindow;
class LFSWindow;
class HMMTargetWindow;
class HMMMemoryWindow;
class StationarityWindow;
class FSCAWindow;

namespace arrow {
    class Table;
}

// Forward declare the enum from chronosflow namespace
namespace chronosflow {
    enum class TimeFormat;
}

struct DatasetMetadata {
    std::string dataset_id;
    std::string dataset_slug;
    std::string indicator_measurement;
    std::string ohlcv_measurement;
    std::string symbol;
    std::string granularity;
    int64_t indicator_rows = 0;
    int64_t ohlcv_rows = 0;
};

class TimeSeriesWindow {
public:
    TimeSeriesWindow();
    ~TimeSeriesWindow() = default;
    
    // Core window interface (following NewsWindow pattern)
    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }
    
    // Data access
    const chronosflow::AnalyticsDataFrame* GetDataFrame() const { return m_dataFrame.get(); }
    
    // Legacy compatibility method for HistogramWindow (returns nullptr - histogram disabled)
    const TimeSeries* GetTimeSeries() const { return nullptr; }
    
    bool HasData() const { return m_dataFrame && m_dataFrame->num_rows() > 0; }
    size_t GetRowCount() const { return m_dataFrame ? m_dataFrame->num_rows() : 0; }
    int64_t GetTimestamp(size_t row_index) const;  // Get Unix timestamp at row
    std::string GetLastQuestDBMeasurement() const { return m_lastQuestDbMeasurement; }
    std::string GetSuggestedDatasetId() const;
    std::optional<DatasetMetadata> GetActiveDataset() const { return m_activeDataset; }
    void LoadDatasetFromMetadata(const DatasetMetadata& metadata);
    void ClearActiveDataset();
    bool ExportDataset(const std::string& measurement,
                       std::string* statusMessage,
                       DatasetMetadata* metadataOut,
                       bool recordMetadata = true);
    void SetActiveDatasetMetadata(const DatasetMetadata& metadata);
    std::pair<std::optional<int64_t>, std::optional<int64_t>> GetTimestampBounds() const;
    
    // Histogram window integration
    void SetHistogramWindow(HistogramWindow* histogramWindow) { m_histogramWindow = histogramWindow; }
    void NotifyColumnSelection(const std::string& indicatorName, size_t columnIndex);
    
    // Bivariate analysis widget integration
    void SetBivarAnalysisWidget(BivarAnalysisWidget* bivarWidget) { m_bivarAnalysisWidget = bivarWidget; }
    
    // ESS (Enhanced Stepwise Selection) window integration
    void SetESSWindow(ESSWindow* essWindow) { m_essWindow = essWindow; }
    
    // LFS (Local Feature Selection) window integration
    void SetLFSWindow(LFSWindow* lfsWindow) { m_lfsWindow = lfsWindow; }

    void SetHMMTargetWindow(HMMTargetWindow* window) { m_hmmTargetWindow = window; }
    void SetHMMMemoryWindow(HMMMemoryWindow* window) { m_hmmMemoryWindow = window; }
    void SetStationarityWindow(StationarityWindow* window) { m_stationarityWindow = window; }
    void SetFSCAWindow(FSCAWindow* window) { m_fscaWindow = window; }
    
    // Universal column selection API for any widget/window
    // Can be called with just name (will find index) or with both name and index
    void SelectIndicator(const std::string& indicatorName, size_t columnIndex = SIZE_MAX);
    void SelectIndicatorByIndex(size_t columnIndex);
    size_t GetColumnIndex(const std::string& columnName) const;
    
private:
    // UI rendering methods
    void DrawFileControls();
    void DrawExportControls();
    void DrawQuestDBImportControls();
    void DrawDataTable();
    void DrawPlotArea();
    void DrawStatusBar();
    
    // File operations
    void LoadCSVFile(const std::string& filepath);
    void TriggerQuestDBExport();
    void LoadQuestDBTable(const std::string& tableName);
    bool ExportToQuestDB(const std::string& tableName, std::string& statusMessage);
    std::pair<std::optional<int64_t>, std::optional<int64_t>> ExtractTimestampBounds(const std::shared_ptr<arrow::Table>& table) const;
    void ClearData();
    
    // UI state management
    void ResetUIState();
    void UpdatePlotData();
    void UpdateDisplayCache(); // Add this helper function
    
    // Helper methods
    std::string GetFileDialogPath();
    
    // Window state
    bool m_isVisible;
    
    // Data storage
    std::unique_ptr<chronosflow::AnalyticsDataFrame> m_dataFrame;
    
    // File management
    char m_filePathBuffer[512];
    bool m_isLoading;
    bool m_hasError;
    std::string m_errorMessage;
    std::string m_loadedFilePath;
    std::string m_lastQuestDbMeasurement;
    char m_tableNameBuffer[128];
    bool m_isExporting;
    bool m_lastExportSuccess;
    std::string m_exportStatusMessage;
    char m_importTableBuffer[128];
    bool m_isQuestDBFetching;
    bool m_lastQuestDBFetchSuccess;
    std::string m_questdbStatusMessage;
    std::optional<DatasetMetadata> m_activeDataset;
    
    // Table display state
    int m_selectedColumnIndex;
    float m_tableHeight;
    ImGuiTableFlags m_tableFlags;
    std::vector<std::string> m_columnHeaders;
    
    // Plot state
    std::string m_selectedIndicator;
    bool m_autoFitPlot;
    float m_plotHeight;
    
    // Cached plot data for performance
    std::vector<double> m_cachedPlotTimes;
    std::vector<double> m_cachedPlotValues;
    std::string m_cachedIndicatorName;
    bool m_plotDataDirty;
    
    // High-performance display cache for the table
    std::vector<std::vector<std::string>> m_displayCache;
    
    // UI layout constants
    static constexpr float FILE_CONTROLS_HEIGHT = 80.0f;
    static constexpr float STATUS_BAR_HEIGHT = 25.0f;
    static constexpr float DEFAULT_TABLE_HEIGHT = 200.0f;
    static constexpr float DEFAULT_PLOT_HEIGHT = 300.0f;
    static constexpr int MAX_DISPLAY_ROWS = 250;  // Reduced from 1000 for better performance
    
    
    // Histogram window integration
    HistogramWindow* m_histogramWindow;
    
    // Bivariate analysis widget integration
    BivarAnalysisWidget* m_bivarAnalysisWidget;
    
    // ESS window integration
    ESSWindow* m_essWindow;
    
    // LFS window integration
    LFSWindow* m_lfsWindow;
    HMMTargetWindow* m_hmmTargetWindow;
    HMMMemoryWindow* m_hmmMemoryWindow;
    StationarityWindow* m_stationarityWindow;
    FSCAWindow* m_fscaWindow;
    
    // Asynchronous loading support
    std::future<arrow::Result<chronosflow::AnalyticsDataFrame>> m_loadingFuture;
    
    // Detected time format for timestamp conversion
    chronosflow::TimeFormat m_detectedTimeFormat;
};

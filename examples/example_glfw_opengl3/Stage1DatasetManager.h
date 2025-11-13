#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>

class CandlestickChart;

class TimeSeriesWindow;
namespace chronosflow {
class AnalyticsDataFrame;
}
struct OHLCVData;

class Stage1DatasetManager {
public:
    Stage1DatasetManager();
    void SetTimeSeriesWindow(TimeSeriesWindow* window) { m_timeSeriesWindow = window; }
    void SetCandlestickChart(CandlestickChart* chart) { m_candlestickChart = chart; }
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }
    void Draw();

private:
    struct DatasetRow {
        std::string dataset_id;
        std::string dataset_slug;
        std::string symbol;
        std::string granularity;
        std::string ohlcv_measurement;
        std::string indicator_measurement;
        int64_t ohlcv_rows = 0;
        int64_t indicator_rows = 0;
        std::string ohlcv_first_ts;
        std::string ohlcv_last_ts;
        std::string indicator_first_ts;
        std::string indicator_last_ts;
        int64_t run_count = 0;
        int64_t simulation_count = 0;
        std::string updated_at;
        bool local_only = false;
    };

    void RefreshRows();
    void LoadSelectedDataset();
    void ExportCurrentDataset();
    bool HasIndicatorData() const;
    bool HasOhlcvData() const;
    static std::string SanitizeSlug(const std::string& value);
    std::vector<DatasetRow> LoadLocalDatasetRows() const;
    static std::string FormatTimestampMs(int64_t timestamp_ms);
    static std::string MakeRowKey(const DatasetRow& row);
    bool UploadOhlcvRowsToStage1(const std::string& datasetId, std::string* error);
    bool UploadIndicatorRowsToStage1(const std::string& datasetId,
                                     const std::string& timestampColumn,
                                     std::string* error);
    bool EnsureStage1DatasetReady(const std::string& preferredId,
                                  const std::string& slug,
                                  std::string* resolvedId,
                                  std::string* error) const;

    bool m_visible = false;
    TimeSeriesWindow* m_timeSeriesWindow = nullptr;
    CandlestickChart* m_candlestickChart = nullptr;
    std::vector<DatasetRow> m_rows;
    int m_selectedIndex = -1;
    bool m_refreshPending = true;
    char m_datasetSlug[128];
    char m_indicatorMeasurementBuffer[128];
    char m_ohlcvMeasurementBuffer[128];
    std::string m_statusMessage;
    bool m_statusSuccess = true;
    std::vector<char> m_statusBuffer;

    // Async export support
    std::thread m_exportThread;
    std::atomic<bool> m_exportInProgress{false};
    std::mutex m_statusMutex;
    void ExportCurrentDatasetAsync();
    void UpdateStatus(const std::string& message, bool success);
};

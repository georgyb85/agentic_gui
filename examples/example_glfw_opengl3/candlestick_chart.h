#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <future> // For std::future
#include <memory>
#include <optional>
#include <utility>

#include "imgui.h"
#include "implot.h"
#include "utils.h"      // For OHLCVData and fetchOHLCVData declaration
#include "ohlcv_data.h" // For OhlcvData class
#include "implot_custom_plotters.h" // For MyImPlot::PlotCandlestick
#include "TickerSelector.h" // Added for ticker selection UI

namespace chronosflow {
class AnalyticsDataFrame;
}

class CandlestickChart {
public:
    CandlestickChart(const std::string& symbol, time_t from_time, time_t to_time);
    void Render(); // Main rendering function for the chart window
    
    // File loading support
    bool LoadFromFile(const std::string& filepath);
    void ClearFileData();
    bool HasFileData() const { return use_file_data_ && data_loaded_; }
    bool HasAnyData() const { return data_loaded_; }
    const OhlcvData& GetOhlcvData() const { return ohlcv_data_; }
    bool LoadFromQuestDb(const std::string& measurement, std::string* statusMessage = nullptr);
    const chronosflow::AnalyticsDataFrame* GetAnalyticsDataFrame() const { return ohlcv_dataframe_.get(); }
    std::pair<std::optional<int64_t>, std::optional<int64_t>> GetTimestampBoundsMs() const;
    // News events time series
    struct NewsEvent {
        time_t Timestamp;
        std::string Text;
    };
    std::vector<NewsEvent> NewsSeries;
    void SetNewsSeries(const std::vector<NewsEvent>& news);

private:
    // Configuration
    TickerSelector ticker_selector_; // Added for ticker selection UI
    std::string symbol_;
    time_t from_time_;
    time_t to_time_;

    // UI State & Controls
    std::string current_timeframe_str_;
    int current_timeframe_idx_;
    const char* timeframes_[5] = {"1m", "15m", "1h", "4h", "1d"};
    bool hide_empty_candles_;
    bool show_tooltip_;
    bool show_ohlcv_window_; // To control the window visibility itself

    // Data
    OhlcvData ohlcv_data_; // Manages OHLCV data storage and processing
    bool data_loaded_;
    bool is_loading_data_;
    std::future<std::vector<OHLCVData>> data_future_; // Fetches raw data
    std::string data_loading_error_;
    
    // File loading support
    bool use_file_data_;
    std::string loaded_file_path_;
    char file_path_buffer_[512];

    // Interaction & Synchronization State for linked plots
    int hovered_idx_;
    double hovered_x_plot_val_; // The X value (time or index) of the hovered bar's center in plot units
    bool is_tooltip_active_;    // True if a tooltip should be shown for the hovered bar

    double shared_x_min_; // Shared X-axis minimum for linked plots
    double shared_x_max_; // Shared X-axis maximum for linked plots
    bool fit_x_axis_on_next_draw_ = true; // NEW: True if X-axis should be fitted to data on the next draw
    // bool candlestick_plot_modified_; // True if candlestick plot's X-axis was changed by user -- REMOVED
    // bool volume_plot_modified_;      // True if volume plot's X-axis was changed by user -- REMOVED
    // last_hide_empty_candles_state_ removed, OhlcvData handles its own processing state,
    // and first_time_init_ handles UI plot reset logic.
    double prev_x_min_;    // Cached previous X-axis minimum
    double prev_x_max_;    // Cached previous X-axis maximum
    int    visible_start_idx_; // Index of first visible data point
    int    visible_end_idx_;   // Index of last visible data point

    // Helper Methods
    void RequestLoadData();
    void CheckAndProcessLoadedData();
    // PreparePlotData() removed, functionality moved to OhlcvData class and Render() method
    void RenderControls();
    void RenderCandlestickPlotPane();
    void RenderVolumePlotPane();
    void RenderUnifiedTooltip();
    void DrawFileLoadControls();
    bool LoadOhlcvFromFile(const std::string& filepath);
    int FindHoveredBarIndex(double mouse_x_plot, const double* x_values, int num_values, double item_full_width_plot_units, bool is_time_scale);

    // User data struct for VolumeTimeAxisFormatter
    // Needs to be defined before its use as a member or in static function signature
    struct VolumeFormatterUserData {
        std::string timeframe;
        bool hide_gaps;
        const std::vector<double>* original_times_ptr; // Pointer to original_times_ from OhlcvData instance
    };
    VolumeFormatterUserData volume_formatter_user_data_; // Member to hold this data for the formatter

    // Static formatters for ImPlot axes
    // The 'user_data' for these will point to an instance of this CandlestickChart class or a specific struct.
    // For VolumeTimeAxisFormatter, it will point to volume_formatter_user_data_
    static int CandlestickTimeAxisFormatter(double value, char* buff, int size, void* user_data); // user_data = current_timeframe_str_
    static int VolumeTimeAxisFormatter(double value, char* buff, int size, void* user_data);    // user_data = VolumeFormatterUserData*

    bool UpdateAnalyticsDataFrameFromRaw(const std::vector<OHLCVData>& raw, std::string* error);
    bool PopulateFromDataFrame(chronosflow::AnalyticsDataFrame&& df, std::string* statusMessage);

    std::unique_ptr<chronosflow::AnalyticsDataFrame> ohlcv_dataframe_;
    std::string last_questdb_measurement_;
};

#pragma once

#include "imgui.h"
#include "implot.h"
#include "chronosflow.h"
#include "dataframe_io.h"
#include <string>
#include <vector>
#include <memory>
#include <future>

// Simple OHLCV data structure with different name to avoid conflicts
struct SimpleOhlcvData {
    std::vector<double> timestamps;  // Unix timestamps in milliseconds
    std::vector<float> open;
    std::vector<float> high;
    std::vector<float> low;
    std::vector<float> close;
    std::vector<float> volume;
    
    size_t size() const { return timestamps.size(); }
    bool empty() const { return timestamps.empty(); }
    void clear() {
        timestamps.clear();
        open.clear();
        high.clear();
        low.clear();
        close.clear();
        volume.clear();
    }
};

class SimpleOhlcvWindow {
public:
    SimpleOhlcvWindow();
    ~SimpleOhlcvWindow() = default;
    
    // Window management
    void Draw();
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }
    
    // Data loading using ChronosFlow - headerless CSV: date,time,open,high,low,close,volume  
    void LoadFromFile(const std::string& filepath);
    void ClearData();
    
    // Data access
    const SimpleOhlcvData& GetData() const { return m_data; }
    const chronosflow::AnalyticsDataFrame* GetDataFrame() const { return m_dataframe.get(); }
    bool HasData() const { return !m_data.empty(); }
    size_t GetDataSize() const { return m_data.size(); }
    
    // Get price at specific timestamp (for trade execution)
    float GetPriceAt(double timestamp, const std::string& price_type = "close") const;
    
    // Enable chart display
    void SetShowChart(bool show) { m_show_chart = show; }
    bool GetShowChart() const { return m_show_chart; }
    
private:
    // UI sections
    void DrawFileControls();
    void DrawDataInfo();
    void DrawChart();
    
    // Helper functions
    void ProcessLoadedDataFrame();
    size_t FindTimestampIndex(double timestamp) const;
    
    // Data
    SimpleOhlcvData m_data;
    std::unique_ptr<chronosflow::AnalyticsDataFrame> m_dataframe;
    std::string m_current_file;
    
    // UI state
    bool m_visible = false;
    char m_file_path_buffer[512] = "";
    std::string m_last_error;
    bool m_show_chart = true;
    
    // Loading state
    bool m_isLoading = false;
    std::future<arrow::Result<chronosflow::AnalyticsDataFrame>> m_loadingFuture;
};
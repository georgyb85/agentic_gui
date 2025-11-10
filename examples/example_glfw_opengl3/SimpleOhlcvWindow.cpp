#include "SimpleOhlcvWindow.h"
#include "implot_custom_plotters.h"
#include <iostream>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <arrow/compute/api.h>

#ifdef _WIN32
#define localtime_r(timep, result) localtime_s(result, timep)
#endif

SimpleOhlcvWindow::SimpleOhlcvWindow() {
}

void SimpleOhlcvWindow::Draw() {
    if (!m_visible) return;
    
    // Check if loading operation has completed
    if (m_isLoading && m_loadingFuture.valid() &&
        m_loadingFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        
        auto result = m_loadingFuture.get();
        if (result.ok()) {
            m_dataframe = std::make_unique<chronosflow::AnalyticsDataFrame>(std::move(result).ValueOrDie());
            ProcessLoadedDataFrame();
            m_last_error.clear();
        } else {
            m_last_error = "Failed to load: " + result.status().ToString();
            std::cerr << "[SimpleOhlcvWindow] " << m_last_error << std::endl;
        }
        m_isLoading = false;
    }
    
    if (ImGui::Begin("OHLCV Data Loader", &m_visible, ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Chart", nullptr, &m_show_chart);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        DrawFileControls();
        
        if (HasData()) {
            ImGui::Separator();
            DrawDataInfo();
            
            if (m_show_chart) {
                ImGui::Separator();
                DrawChart();
            }
        }
    }
    ImGui::End();
}

void SimpleOhlcvWindow::DrawFileControls() {
    ImGui::InputText("File Path", m_file_path_buffer, sizeof(m_file_path_buffer));
    ImGui::SameLine();
    if (ImGui::Button("Load") && !m_isLoading) {
        LoadFromFile(m_file_path_buffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear") && !m_isLoading) {
        ClearData();
    }
    
    // Loading indicator
    if (m_isLoading) {
        ImGui::SameLine();
        ImGui::Text("Loading...");
    }
    
    if (!m_last_error.empty()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", m_last_error.c_str());
    }
    
    if (HasData()) {
        ImGui::Text("Loaded: %s (%zu bars)", m_current_file.c_str(), GetDataSize());
    }
}

void SimpleOhlcvWindow::DrawDataInfo() {
    // Show basic info about loaded data
    if (m_dataframe) {
        ImGui::Text("DataFrame Info:");
        ImGui::Text("  Rows: %lld", m_dataframe->num_rows());
        ImGui::Text("  Columns: %lld", m_dataframe->num_columns());
        
        if (!m_data.empty()) {
            // Show time range
            auto first_ts = m_data.timestamps.front();
            auto last_ts = m_data.timestamps.back();
            
            std::tm first_tm = {};
            std::tm last_tm = {};
            time_t first_t = (time_t)(first_ts / 1000.0);
            time_t last_t = (time_t)(last_ts / 1000.0);
            localtime_r(&first_t, &first_tm);
            localtime_r(&last_t, &last_tm);
            
            char first_buf[32], last_buf[32];
            strftime(first_buf, sizeof(first_buf), "%Y-%m-%d %H:%M:%S", &first_tm);
            strftime(last_buf, sizeof(last_buf), "%Y-%m-%d %H:%M:%S", &last_tm);
            
            ImGui::Text("  Time Range: %s to %s", first_buf, last_buf);
            
            // Show price range
            auto [min_low, max_low] = std::minmax_element(m_data.low.begin(), m_data.low.end());
            auto [min_high, max_high] = std::minmax_element(m_data.high.begin(), m_data.high.end());
            ImGui::Text("  Price Range: %.2f - %.2f", *min_low, *max_high);
        }
    }
}

void SimpleOhlcvWindow::LoadFromFile(const std::string& filepath) {
    if (m_isLoading) return;  // Already loading
    
    m_isLoading = true;
    m_last_error.clear();
    m_current_file = filepath;
    
    // Launch loading task in background using ChronosFlow
    m_loadingFuture = std::async(std::launch::async, [filepath]() {
        chronosflow::TSSBReadOptions options;
        options.auto_detect_delimiter = true;
        // CRITICAL: The OHLCV file has NO header
        options.has_header = false;
        
        // ChronosFlow will auto-generate column names (f0, f1, f2, etc.)
        // We'll map them in ProcessLoadedDataFrame
        
        return chronosflow::DataFrameIO::read_tssb(filepath, options);
    });
}

void SimpleOhlcvWindow::ProcessLoadedDataFrame() {
    if (!m_dataframe) return;
    
    // Clear existing data
    m_data.clear();
    
    // Get table from dataframe
    auto table = m_dataframe->get_cpu_table();
    if (!table) {
        m_last_error = "Failed to get table from dataframe";
        return;
    }
    
    const int64_t num_rows = table->num_rows();
    const int num_cols = table->num_columns();
    
    // Verify we have exactly 7 columns (date,time,open,high,low,close,volume)
    if (num_cols != 7) {
        m_last_error = "Expected 7 columns (date,time,open,high,low,close,volume), got " + std::to_string(num_cols);
        return;
    }
    
    // Reserve space for efficiency
    m_data.timestamps.reserve(num_rows);
    m_data.open.reserve(num_rows);
    m_data.high.reserve(num_rows);
    m_data.low.reserve(num_rows);
    m_data.close.reserve(num_rows);
    m_data.volume.reserve(num_rows);
    
    // For headerless files, ChronosFlow auto-generates column names (f0, f1, f2, etc.)
    // Our format is: date,time,open,high,low,close,volume
    // So: column(0)=date, column(1)=time, column(2)=open, column(3)=high, column(4)=low, column(5)=close, column(6)=volume
    auto date_col = table->column(0);    // date
    auto time_col = table->column(1);    // time  
    auto open_col = table->column(2);    // open
    auto high_col = table->column(3);    // high
    auto low_col = table->column(4);    // low
    auto close_col = table->column(5);    // close
    auto volume_col = table->column(6);  // volume
    
    if (!date_col || !time_col || !open_col || !high_col || !low_col || !close_col || !volume_col) {
        m_last_error = "Failed to get required columns from dataframe";
        return;
    }
    
    // Process each row
    for (int64_t i = 0; i < num_rows; ++i) {
        // Get date and time values
        auto date_scalar = date_col->GetScalar(i);
        auto time_scalar = time_col->GetScalar(i);
        
        if (!date_scalar.ok() || !time_scalar.ok()) continue;
        
        // Convert date/time to timestamp
        int64_t date_val = std::static_pointer_cast<arrow::Int64Scalar>(date_scalar.ValueOrDie())->value;
        int64_t time_val = std::static_pointer_cast<arrow::Int64Scalar>(time_scalar.ValueOrDie())->value;
        
        // Parse date (YYYYMMDD format)
        int year = date_val / 10000;
        int month = (date_val / 100) % 100;
        int day = date_val % 100;
        
        // Parse time (HHMMSS or HHMM format)
        int hour = 0, minute = 0, second = 0;
        if (time_val > 9999) {
            // HHMMSS format
            hour = time_val / 10000;
            minute = (time_val / 100) % 100;
            second = time_val % 100;
        } else {
            // HHMM format
            hour = time_val / 100;
            minute = time_val % 100;
        }
        
        // Convert to Unix timestamp
        std::tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        
        time_t t = std::mktime(&tm);
        double timestamp = (double)t * 1000.0;  // Convert to milliseconds
        
        // Get OHLCV values
        auto open_scalar = open_col->GetScalar(i);
        auto high_scalar = high_col->GetScalar(i);
        auto low_scalar = low_col->GetScalar(i);
        auto close_scalar = close_col->GetScalar(i);
        auto volume_scalar = volume_col->GetScalar(i);
        
        if (!open_scalar.ok() || !high_scalar.ok() || !low_scalar.ok() || 
            !close_scalar.ok() || !volume_scalar.ok()) continue;
        
        // Add to data vectors
        m_data.timestamps.push_back(timestamp);
        m_data.open.push_back(std::static_pointer_cast<arrow::DoubleScalar>(open_scalar.ValueOrDie())->value);
        m_data.high.push_back(std::static_pointer_cast<arrow::DoubleScalar>(high_scalar.ValueOrDie())->value);
        m_data.low.push_back(std::static_pointer_cast<arrow::DoubleScalar>(low_scalar.ValueOrDie())->value);
        m_data.close.push_back(std::static_pointer_cast<arrow::DoubleScalar>(close_scalar.ValueOrDie())->value);
        m_data.volume.push_back(std::static_pointer_cast<arrow::DoubleScalar>(volume_scalar.ValueOrDie())->value);
    }
    
    std::cout << "[SimpleOhlcvWindow] Loaded " << m_data.size() << " OHLCV bars from " 
              << m_current_file << std::endl;
}

void SimpleOhlcvWindow::ClearData() {
    m_data.clear();
    m_dataframe.reset();
    m_current_file.clear();
}

size_t SimpleOhlcvWindow::FindTimestampIndex(double timestamp) const {
    if (m_data.empty()) return 0;
    
    // Binary search
    auto it = std::lower_bound(m_data.timestamps.begin(), m_data.timestamps.end(), timestamp);
    if (it != m_data.timestamps.end()) {
        return std::distance(m_data.timestamps.begin(), it);
    }
    return m_data.timestamps.size() - 1;
}

float SimpleOhlcvWindow::GetPriceAt(double timestamp, const std::string& price_type) const {
    size_t idx = FindTimestampIndex(timestamp);
    if (idx >= m_data.size()) return -1;
    
    if (price_type == "open") {
        return m_data.open[idx];
    } else if (price_type == "high") {
        return m_data.high[idx];
    } else if (price_type == "low") {
        return m_data.low[idx];
    } else if (price_type == "close") {
        return m_data.close[idx];
    } else if (price_type == "volume") {
        return m_data.volume[idx];
    }
    
    return m_data.close[idx];  // Default to close
}

void SimpleOhlcvWindow::DrawChart() {
    if (m_data.empty()) return;
    
    // Convert timestamps to seconds and all float data to double for ImPlot
    std::vector<double> times_sec;
    std::vector<double> open_d, high_d, low_d, close_d, volume_d;
    
    times_sec.reserve(m_data.timestamps.size());
    open_d.reserve(m_data.open.size());
    high_d.reserve(m_data.high.size());
    low_d.reserve(m_data.low.size());
    close_d.reserve(m_data.close.size());
    volume_d.reserve(m_data.volume.size());
    
    for (size_t i = 0; i < m_data.timestamps.size(); ++i) {
        times_sec.push_back(m_data.timestamps[i] / 1000.0);  // Convert ms to seconds
        open_d.push_back(static_cast<double>(m_data.open[i]));
        high_d.push_back(static_cast<double>(m_data.high[i]));
        low_d.push_back(static_cast<double>(m_data.low[i]));
        close_d.push_back(static_cast<double>(m_data.close[i]));
        volume_d.push_back(static_cast<double>(m_data.volume[i]));
    }
    
    // Draw candlestick chart
    if (ImPlot::BeginPlot("OHLCV Chart", ImVec2(-1, 400))) {
        // Set up time axis
        ImPlot::SetupAxisFormat(ImAxis_X1, "%Y-%m-%d %H:%M");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        
        // Plot candlesticks using the custom plotter
        MyImPlot::PlotCandlestick("OHLCV", 
                                  times_sec.data(), 
                                  open_d.data(), 
                                  high_d.data(), 
                                  low_d.data(), 
                                  close_d.data(), 
                                  (int)m_data.size(), 
                                  0.67f, // width_percent
                                  ImVec4(0.0f, 1.0f, 0.0f, 1.0f), // bullish color (green)
                                  ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); // bearish color (red)
        
        ImPlot::EndPlot();
    }
    
    // Draw volume chart
    if (ImPlot::BeginPlot("Volume", ImVec2(-1, 150))) {
        ImPlot::SetupAxisFormat(ImAxis_X1, "%Y-%m-%d %H:%M");
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        
        ImPlot::PlotBars("Volume", times_sec.data(), volume_d.data(), (int)m_data.size(), 0.67);
        
        ImPlot::EndPlot();
    }
}
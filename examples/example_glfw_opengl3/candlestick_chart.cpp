#include "candlestick_chart.h"
#include <vector>
#include <string>
#include <ctime>
#include <map>       // For std::map
#include "imgui.h"
#include "implot.h"
#include <algorithm> // For std::min, std::max
#include <cmath>     // For fabs, round
#include <cfloat>    // For DBL_MAX
#include <chrono>    // For std::chrono in RequestLoadData
#include <iostream>  // For debugging output (optional)
#include <iomanip>   // For std::fixed and std::setprecision
#include <memory>
#include <sstream>
#include <arrow/compute/api.h>
#include <arrow/api.h>
#include "QuestDbDataFrameGateway.h"
#include "analytics_dataframe.h"
#include "dataframe_io.h"
#include "Stage1RestClient.h"
#include <json/json.h>

namespace {

bool ReportStatus(const arrow::Status& status, const char* context, std::string* error) {
    if (status.ok()) {
        return true;
    }
    if (error) {
        std::ostringstream oss;
        oss << "Failed to build OHLCV column (" << context << "): " << status.ToString();
        *error = oss.str();
    }
    return false;
}

std::unique_ptr<chronosflow::AnalyticsDataFrame> BuildAnalyticsFrameFromRaw(
    const std::vector<OHLCVData>& data,
    std::string* error) {
    arrow::Int64Builder timestampBuilder;
    arrow::DoubleBuilder openBuilder;
    arrow::DoubleBuilder highBuilder;
    arrow::DoubleBuilder lowBuilder;
    arrow::DoubleBuilder closeBuilder;
    arrow::DoubleBuilder volumeBuilder;

    for (const auto& candle : data) {
        if (!ReportStatus(timestampBuilder.Append(static_cast<int64_t>(candle.time) * 1000LL), "timestamp", error) ||
            !ReportStatus(openBuilder.Append(candle.open), "open", error) ||
            !ReportStatus(highBuilder.Append(candle.high), "high", error) ||
            !ReportStatus(lowBuilder.Append(candle.low), "low", error) ||
            !ReportStatus(closeBuilder.Append(candle.close), "close", error) ||
            !ReportStatus(volumeBuilder.Append(candle.volume), "volume", error)) {
            return nullptr;
        }
    }

    std::shared_ptr<arrow::Array> tsArray;
    std::shared_ptr<arrow::Array> openArray;
    std::shared_ptr<arrow::Array> highArray;
    std::shared_ptr<arrow::Array> lowArray;
    std::shared_ptr<arrow::Array> closeArray;
    std::shared_ptr<arrow::Array> volumeArray;

    if (!ReportStatus(timestampBuilder.Finish(&tsArray), "timestamp_finish", error) ||
        !ReportStatus(openBuilder.Finish(&openArray), "open_finish", error) ||
        !ReportStatus(highBuilder.Finish(&highArray), "high_finish", error) ||
        !ReportStatus(lowBuilder.Finish(&lowArray), "low_finish", error) ||
        !ReportStatus(closeBuilder.Finish(&closeArray), "close_finish", error) ||
        !ReportStatus(volumeBuilder.Finish(&volumeArray), "volume_finish", error)) {
        return nullptr;
    }

    auto schema = arrow::schema({
        arrow::field("timestamp_unix", arrow::int64()),
        arrow::field("open", arrow::float64()),
        arrow::field("high", arrow::float64()),
        arrow::field("low", arrow::float64()),
        arrow::field("close", arrow::float64()),
        arrow::field("volume", arrow::float64())
    });

    auto table = arrow::Table::Make(
        schema,
        {tsArray, openArray, highArray, lowArray, closeArray, volumeArray});

    return std::make_unique<chronosflow::AnalyticsDataFrame>(table);
}

time_t ToUtcTimeT(std::tm* tm) {
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
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
        fractionMillis = std::stoi(fraction);
    }

    std::tm tm = {};
    tm.tm_year = *year - 1900;
    tm.tm_mon = *month - 1;
    tm.tm_mday = *day;
    tm.tm_hour = *hour;
    tm.tm_min = *minute;
    tm.tm_sec = *second;
    time_t seconds = ToUtcTimeT(&tm);
    if (seconds == static_cast<time_t>(-1)) {
        return std::nullopt;
    }
    return static_cast<int64_t>(seconds) * 1000LL + fractionMillis;
}

std::optional<int64_t> ScalarToMillis(const std::shared_ptr<arrow::Scalar>& scalar) {
    if (!scalar || !scalar->is_valid) {
        return std::nullopt;
    }
    switch (scalar->type->id()) {
        case arrow::Type::INT64:
            return std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value;
        case arrow::Type::DOUBLE:
            return static_cast<int64_t>(std::llround(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value));
        case arrow::Type::FLOAT:
            return static_cast<int64_t>(std::llround(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value));
        case arrow::Type::STRING:
        case arrow::Type::LARGE_STRING: {
            auto value = scalar->ToString();
            return ParseIsoToMillis(value);
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

} // namespace
// Setter for news events series
void CandlestickChart::SetNewsSeries(const std::vector<CandlestickChart::NewsEvent>& news) {
    NewsSeries = news;
}

bool CandlestickChart::LoadFromFile(const std::string& filepath) {
    // Use ChronosFlow to load headerless OHLCV file
    chronosflow::TSSBReadOptions options;
    options.auto_detect_delimiter = true;
    options.has_header = false;  // No header in OHLCV files
    // Don't set date_column/time_column - just access by index
    
    auto result = chronosflow::DataFrameIO::read_tssb(filepath, options);
    if (!result.ok()) {
        data_loading_error_ = "Failed to load file: " + result.status().ToString();
        return false;
    }
    
    auto df = std::move(result).ValueOrDie();
    auto table = df.get_cpu_table();
    
    // Expect 7 columns: date,time,open,high,low,close,volume
    if (table->num_columns() != 7) {
        data_loading_error_ = "Invalid file format. Expected 7 columns (date,time,open,high,low,close,volume)";
        return false;
    }
    
    // Get column arrays directly by index (like SimpleOhlcvWindow does)
    auto date_col = table->column(0);    // date
    auto time_col = table->column(1);    // time
    auto open_col = table->column(2);    // open
    auto high_col = table->column(3);    // high
    auto low_col = table->column(4);    // low
    auto close_col = table->column(5);    // close
    auto volume_col = table->column(6);  // volume
    
    if (!date_col || !time_col || !open_col || !high_col || !low_col || !close_col || !volume_col) {
        data_loading_error_ = "Failed to get required columns from dataframe";
        return false;
    }
    
    // Convert to OHLCVData format
    std::vector<OHLCVData> ohlcv_vector;
    const int64_t num_rows = table->num_rows();
    ohlcv_vector.reserve(num_rows);
    
    for (int64_t i = 0; i < num_rows; ++i) {
        // Get date and time values
        auto date_scalar = date_col->GetScalar(i);
        auto time_scalar = time_col->GetScalar(i);
        
        if (!date_scalar.ok() || !time_scalar.ok()) continue;
        
        OHLCVData point;
        
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
        
        std::tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        
        point.time = std::mktime(&tm);
        
        // Get OHLCV values
        auto open_scalar = open_col->GetScalar(i);
        auto high_scalar = high_col->GetScalar(i);
        auto low_scalar = low_col->GetScalar(i);
        auto close_scalar = close_col->GetScalar(i);
        auto volume_scalar = volume_col->GetScalar(i);
        
        if (!open_scalar.ok() || !high_scalar.ok() || !low_scalar.ok() || 
            !close_scalar.ok() || !volume_scalar.ok()) continue;
        
        point.open = std::static_pointer_cast<arrow::DoubleScalar>(open_scalar.ValueOrDie())->value;
        point.high = std::static_pointer_cast<arrow::DoubleScalar>(high_scalar.ValueOrDie())->value;
        point.low = std::static_pointer_cast<arrow::DoubleScalar>(low_scalar.ValueOrDie())->value;
        point.close = std::static_pointer_cast<arrow::DoubleScalar>(close_scalar.ValueOrDie())->value;
        point.volume = std::static_pointer_cast<arrow::DoubleScalar>(volume_scalar.ValueOrDie())->value;
        
        ohlcv_vector.push_back(point);
    }
    
    // Set the data
    ohlcv_data_.setData(ohlcv_vector);
    ohlcv_data_.processData(false); // Process the data for initial use
    data_loaded_ = true;
    loaded_file_path_ = filepath;
    use_file_data_ = true;
    fit_x_axis_on_next_draw_ = true;
    data_loading_error_.clear();
    std::string frameError;
    if (!UpdateAnalyticsDataFrameFromRaw(ohlcv_vector, &frameError)) {
        if (!frameError.empty()) {
            data_loading_error_ = frameError;
        }
    } else {
        last_questdb_measurement_.clear();
    }
    
    return true;
}

bool CandlestickChart::LoadFromQuestDb(const std::string& measurement, std::string* statusMessage) {
    if (measurement.empty()) {
        if (statusMessage) {
            *statusMessage = "Measurement name cannot be empty.";
        }
        return false;
    }

    questdb::DataFrameGateway gateway;
    auto result = gateway.Import(measurement);
    if (!result.ok()) {
        if (statusMessage) {
            *statusMessage = result.status().ToString();
        }
        return false;
    }

    auto df = std::move(result).ValueOrDie();
    if (!PopulateFromDataFrame(std::move(df), statusMessage)) {
        return false;
    }

    last_questdb_measurement_ = measurement;
    use_file_data_ = false;
    data_loaded_ = true;
    fit_x_axis_on_next_draw_ = true;
    data_loading_error_.clear();
    return true;
}

bool CandlestickChart::LoadFromStage1(const std::string& datasetId, std::string* statusMessage) {
    if (datasetId.empty()) {
        if (statusMessage) {
            *statusMessage = "Dataset ID cannot be empty.";
        }
        return false;
    }

    Json::Value rows;
    std::string error;
    if (!stage1::RestClient::Instance().FetchDatasetOhlcv(datasetId, &rows, &error)) {
        if (statusMessage) {
            *statusMessage = "Failed to fetch OHLCV: " + error;
        }
        return false;
    }

    if (!rows.isArray() || rows.empty()) {
        if (statusMessage) {
            *statusMessage = "No OHLCV data returned from Stage1.";
        }
        return false;
    }

    // Helper to parse ISO8601 timestamp to milliseconds
    auto parseTimestamp = [](const Json::Value& val) -> int64_t {
        if (val.isInt64()) {
            return val.asInt64();
        } else if (val.isDouble()) {
            return static_cast<int64_t>(val.asDouble());
        } else if (val.isString()) {
            std::string str = val.asString();
            // Try parsing as ISO8601: "2024-11-28T22:00:00.000000Z"
            if (str.size() >= 19 && str.find('T') != std::string::npos) {
                std::tm tm = {};
                int year, month, day, hour, minute, second;
                if (sscanf(str.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
                    tm.tm_year = year - 1900;
                    tm.tm_mon = month - 1;
                    tm.tm_mday = day;
                    tm.tm_hour = hour;
                    tm.tm_min = minute;
                    tm.tm_sec = second;
                    #ifdef _WIN32
                    time_t t = _mkgmtime(&tm);
                    #else
                    time_t t = timegm(&tm);
                    #endif
                    return static_cast<int64_t>(t) * 1000; // Convert to milliseconds
                }
            }
            // Try parsing as numeric string
            try {
                return std::stoll(str);
            } catch (...) {
                return 0;
            }
        }
        return 0;
    };

    // Convert JSON rows to OHLCV format
    std::vector<OHLCVData> candles;
    candles.reserve(rows.size());

    size_t skipped = 0;
    for (const auto& row : rows) {
        if (!row.isObject()) {
            skipped++;
            continue;
        }

        OHLCVData candle;
        int64_t timestamp_ms = 0;
        if (row.isMember("timestamp_ms")) {
            timestamp_ms = parseTimestamp(row["timestamp_ms"]);
        } else if (row.isMember("timestamp")) {
            timestamp_ms = parseTimestamp(row["timestamp"]);
        }

        if (timestamp_ms == 0) {
            skipped++;
            continue;
        }

        candle.time = static_cast<time_t>(timestamp_ms / 1000); // Convert milliseconds to seconds

        // Parse OHLCV values - they might be strings or numbers
        auto getDoubleValue = [](const Json::Value& val) -> double {
            if (val.isDouble()) return val.asDouble();
            if (val.isInt()) return static_cast<double>(val.asInt64());
            if (val.isString()) return std::stod(val.asString());
            return 0.0;
        };

        candle.open = getDoubleValue(row.get("open", 0.0));
        candle.high = getDoubleValue(row.get("high", 0.0));
        candle.low = getDoubleValue(row.get("low", 0.0));
        candle.close = getDoubleValue(row.get("close", 0.0));
        candle.volume = getDoubleValue(row.get("volume", 0.0));

        candles.push_back(candle);
    }

    if (candles.empty()) {
        if (statusMessage) {
            *statusMessage = "No valid candles found in response.";
        }
        return false;
    }

    ohlcv_data_.setData(candles);
    use_file_data_ = false;
    data_loaded_ = true;
    fit_x_axis_on_next_draw_ = true;
    data_loading_error_.clear();
    last_questdb_measurement_.clear();

    return true;
}

void CandlestickChart::ClearFileData() {
    ohlcv_data_.setData({});
    data_loaded_ = false;
    loaded_file_path_.clear();
    data_loading_error_.clear();
    ohlcv_dataframe_.reset();
    last_questdb_measurement_.clear();
}

// Static frame counter for debugging
static unsigned long long frame_count_global_debug = 0;

// Constructor
CandlestickChart::CandlestickChart(const std::string& symbol, time_t from_time, time_t to_time)
    : symbol_(symbol),
      from_time_(from_time),
      to_time_(to_time),
      current_timeframe_str_("1m"),
      current_timeframe_idx_(0), // Index for "1m" in timeframes_
      hide_empty_candles_(false),
      show_tooltip_(true),
      use_file_data_(false),
      show_ohlcv_window_(true), // Window is visible by default
      data_loaded_(false),
      is_loading_data_(false),
      hovered_idx_(-1),
      hovered_x_plot_val_(0.0),
      is_tooltip_active_(false),
      shared_x_min_(0),
      shared_x_max_(0),
      fit_x_axis_on_next_draw_(true), // Initialize new flag
      prev_x_min_(DBL_MAX),
      prev_x_max_(-DBL_MAX),
      visible_start_idx_(0),
      visible_end_idx_(-1) { // Initialize new flag
    memset(file_path_buffer_, 0, sizeof(file_path_buffer_));
      // Initialize volume_formatter_user_data_
      volume_formatter_user_data_.timeframe = current_timeframe_str_;
      volume_formatter_user_data_.hide_gaps = hide_empty_candles_;
      // original_times_ptr will be set dynamically in RenderVolumePlotPane
      // as it now comes from ohlcv_data_ which might not be populated yet.
      volume_formatter_user_data_.original_times_ptr = nullptr;
  
      RequestLoadData(); // Initial data load
  }
  
  // --- Private Helper Methods ---
  
  void CandlestickChart::RequestLoadData() {
      if (is_loading_data_) return; // Prevent multiple simultaneous loads
  
      is_loading_data_ = true;
      data_loaded_ = false;
      ohlcv_data_.setData({}); // Clear existing data in OhlcvData object
      data_loading_error_.clear();
      // Launch asynchronous data fetching
      data_future_ = std::async(std::launch::async, fetchOHLCVData, symbol_, from_time_, to_time_, current_timeframe_str_);
      fit_x_axis_on_next_draw_ = true; // Set flag to fit data when it's loaded

      // Reset previous axis limits to force full-range initial display
      prev_x_min_ = DBL_MAX;
      prev_x_max_ = -DBL_MAX;
      visible_start_idx_ = 0;
      visible_end_idx_ = -1;
}

void CandlestickChart::CheckAndProcessLoadedData() {
    if (is_loading_data_ && data_future_.valid()) {
        if (data_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                std::vector<OHLCVData> raw_data = data_future_.get();
                ohlcv_data_.setData(raw_data); // Set data in OhlcvData object
                data_loaded_ = !ohlcv_data_.isRawDataEmpty();
                if (data_loaded_) {

                    data_loading_error_.clear();
                    std::string frameError;
                    if (!UpdateAnalyticsDataFrameFromRaw(raw_data, &frameError)) {
                        if (!frameError.empty()) {
                            data_loading_error_ = frameError;
                        }
                    }
                } else {
                    data_loading_error_ = "Failed to load data or data was empty for " + symbol_ + ".";
                }
            } catch (const std::exception& e) {
                data_loaded_ = false;
                ohlcv_data_.setData({}); // Clear data in OhlcvData object
                data_loading_error_ = std::string("Error loading data for ") + symbol_ + ": " + e.what();
                // std::cerr << "[CandlestickChart] " << data_loading_error_ << std::endl;
            } catch (...) {
                data_loaded_ = false;
                ohlcv_data_.setData({}); // Clear data in OhlcvData object
                data_loading_error_ = "An unknown error occurred while loading data for " + symbol_ + ".";
                // std::cerr << "[CandlestickChart] " << data_loading_error_ << std::endl;
            }
            is_loading_data_ = false;
        }
    }
}

void CandlestickChart::RenderControls() {
    // Data source selection
    if (ImGui::RadioButton("From Server", !use_file_data_)) {
        use_file_data_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("From File", use_file_data_)) {
        use_file_data_ = true;
    }
    
    if (use_file_data_) {
        // File loading controls
        ImGui::InputText("File Path", file_path_buffer_, sizeof(file_path_buffer_));
        ImGui::SameLine();
        if (ImGui::Button("Load File")) {
            LoadFromFile(file_path_buffer_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            ClearFileData();
        }
        
        if (!loaded_file_path_.empty()) {
            ImGui::Text("Loaded: %s", loaded_file_path_.c_str());
        }
    } else {
        // Server controls - existing ticker selection
        ImGui::PushItemWidth(ImGui::CalcTextSize("MMMMMMMM").x + ImGui::GetStyle().FramePadding.x * 2.0f); // Approx 8 chars wide + padding
        ticker_selector_.Draw();
        ImGui::PopItemWidth();
        ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();

        // Ticker selection logic
        std::string hint_selected_ticker_str;
        // Call IsHintClickedAndPendingDataFetch once and store its result and the ticker.
        // This method also resets the flag internally.
        bool hint_was_clicked = ticker_selector_.IsHintClickedAndPendingDataFetch(hint_selected_ticker_str);

        if (hint_was_clicked) {
            // If a hint was clicked, prioritize this action.
            if (symbol_ != hint_selected_ticker_str) {
                symbol_ = hint_selected_ticker_str;
                RequestLoadData(); // Request data for the new symbol from hint
            }
        } else {
            // If no hint was clicked, check if the ticker was changed by pressing Enter.
            const char* ticker_from_input_or_enter = ticker_selector_.GetSelectedTicker();
            // GetSelectedTicker() returns m_selectedTicker, which is updated by both
            // hint selection and Enter press. Since hint_was_clicked is false,
            // any change here must be from an Enter press on a typed or previously selected ticker.
            if (ticker_from_input_or_enter && symbol_ != ticker_from_input_or_enter) {
                symbol_ = ticker_from_input_or_enter;
                RequestLoadData(); // Request data for the new symbol from Enter press
            }
        }
    }

    // Timeframe selection
    ImGui::Text("Timeframe:");
    ImGui::SameLine();
    bool timeframe_changed_ui = false;
    for (int i = 0; i < 5; ++i) {
        if (ImGui::RadioButton(timeframes_[i], &current_timeframe_idx_, i)) {
            current_timeframe_str_ = timeframes_[i];
            timeframe_changed_ui = true;
        }
        if (i < 4) ImGui::SameLine();
    }

    if (timeframe_changed_ui) {
        RequestLoadData();
    }

    ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();

    // Hide empty candles checkbox
    if (ImGui::Checkbox("Hide empty candles", &hide_empty_candles_)) {
        fit_x_axis_on_next_draw_ = true; // Signal data reprocessing needs a fit
        prev_x_min_ = DBL_MAX;   // Reset cache on toggle
        prev_x_max_ = -DBL_MAX;  // Reset cache on toggle
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Remove time gaps between trading sessions.\n"
                          "Shows continuous data without weekends/holidays gaps.\n"
                          "Note: Backend only provides data for active market times.");
    }

    ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();

    // Show tooltip checkbox
    ImGui::Checkbox("Show Tooltip", &show_tooltip_);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show detailed OHLCV and Volume tooltip when hovering over bars.");
    }
}

int CandlestickChart::FindHoveredBarIndex(double mouse_x_plot, const double* x_values, int num_values, double item_full_width_plot_units, bool is_time_scale) {
    if (num_values == 0) {
        return -1;
    }

    int potential_idx = -1;

    if (!is_time_scale) { // Index-based (hide_empty_candles_ is true)

        int candidate_idx = static_cast<int>(round(mouse_x_plot));
        if (candidate_idx >= 0 && candidate_idx < num_values) {
            double bar_center_x = x_values[candidate_idx]; // This will be (double)candidate_idx
            double dist_to_center = fabs(mouse_x_plot - bar_center_x);
            // item_full_width_plot_units is 1.0 for index-based scales
            if (dist_to_center < item_full_width_plot_units * 0.75) {
                potential_idx = candidate_idx;
            }
        }
    } else { // Time-based
        double closest_dist_satisfying_width = DBL_MAX;

        // Find the iterator to the first element not less than mouse_x_plot
        const double* it_at_or_after = std::lower_bound(x_values, x_values + num_values, mouse_x_plot);
        int idx_at_or_after = std::distance(x_values, it_at_or_after);

        // Candidate 1: Element at or after mouse_x_plot
        if (idx_at_or_after < num_values) {
            double bar_center_x = x_values[idx_at_or_after];
            double dist = fabs(mouse_x_plot - bar_center_x);
            if (dist < item_full_width_plot_units * 0.75) { // Check width condition
                // This candidate satisfies the width condition.
                // Is it the closest so far that satisfies the condition?
                if (dist < closest_dist_satisfying_width) {
                    closest_dist_satisfying_width = dist;
                    potential_idx = idx_at_or_after;
                }
            }
        }

        // Candidate 2: Element before the one found by lower_bound (if it exists)
        if (idx_at_or_after > 0) {
            int idx_before = idx_at_or_after - 1;

            double bar_center_x = x_values[idx_before];
            double dist = fabs(mouse_x_plot - bar_center_x);
            if (dist < item_full_width_plot_units * 0.75) { // Check width condition
                // This candidate also satisfies the width condition.
                // Is it closer than the current best candidate (potential_idx)?
                if (dist < closest_dist_satisfying_width) {
                    // closest_dist_satisfying_width = dist; // Update the closest distance found
                    potential_idx = idx_before;
                }
            }
        }
    }
    return potential_idx;
}


void CandlestickChart::RenderCandlestickPlotPane() {
    if (ohlcv_data_.getProcessedDataCount() == 0) return;

    const auto& times = ohlcv_data_.getTimes();
    const auto& opens = ohlcv_data_.getOpens();
    const auto& closes = ohlcv_data_.getCloses();
    const auto& lows = ohlcv_data_.getLows();
    const auto& highs = ohlcv_data_.getHighs();

    if (ImPlot::BeginPlot((symbol_ + " Candlestick Chart").c_str(), ImVec2(-1, 400), ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("", "Price ($)"); // Y-axis label
        // X-axis is intentionally kept without labels/ticks here, volume chart will have them.
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoHighlight);

        if (hide_empty_candles_) {
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
        } else {
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
            ImPlot::SetupAxisFormat(ImAxis_X1, CandlestickTimeAxisFormatter, (void*)&current_timeframe_str_);
        }

        ImPlot::SetupAxisLinks(ImAxis_X1, &shared_x_min_, &shared_x_max_);
        // Apply these limits only if fit_x_axis_on_next_draw_ is true, otherwise let ImPlot manage them.
        if (fit_x_axis_on_next_draw_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, shared_x_min_, shared_x_max_, ImPlotCond_Always);
        }

        double min_price = DBL_MAX;
        double max_price = -DBL_MAX;
        int start_idx = 0;
        int end_idx = static_cast<int>(ohlcv_data_.getProcessedDataCount()) -1;

        if (hide_empty_candles_){
            start_idx = static_cast<int>(std::max(0.0, floor(shared_x_min_)));
            end_idx = static_cast<int>(std::min(ceil(shared_x_max_), (double)ohlcv_data_.getProcessedDataCount() - 1.0));
        } else {
            // Time-based: Use binary search to find the range
            const size_t count = ohlcv_data_.getProcessedDataCount();
            if (count > 0) {
                const double* times_data = times.data();

                // Find raw start index (first element >= shared_x_min_)
                auto it_s = std::lower_bound(times_data, times_data + count, shared_x_min_);
                int s_idx_val = static_cast<int>(std::distance(times_data, it_s));

                // Find raw end index (last element <= shared_x_max_)
                auto it_e = std::upper_bound(times_data, times_data + count, shared_x_max_);
                int e_idx_val = static_cast<int>(std::distance(times_data, it_e)) - 1;

                // Apply buffer: -1 for start, +1 for end
                start_idx = s_idx_val - 1;
                end_idx = e_idx_val + 1;

                // Clamp to valid range [0, count-1] for start_idx and end_idx
                // Ensure start_idx is at most count-1 (or 0 if count is 0)
                start_idx = std::max(0, start_idx);
                if (count > 0) { // Avoid count-1 if count is 0
                    start_idx = std::min(start_idx, static_cast<int>(count - 1));
                } else {
                    start_idx = 0; // Should be covered by outer else, but for safety
                }
                

                // Ensure end_idx is at least start_idx-1 (to allow empty loop)
                // and at most count-1
                end_idx = std::max(start_idx - 1, end_idx);
                if (count > 0) { // Avoid count-1 if count is 0
                    end_idx = std::min(end_idx, static_cast<int>(count - 1));
                } else {
                     end_idx = -1; // Should be covered by outer else
                }

            } else { // count == 0
                start_idx = 0;
                end_idx = -1;
            }
        }


        for (int i = start_idx; i <= end_idx && i < static_cast<int>(ohlcv_data_.getProcessedDataCount()); ++i) {
            min_price = std::min(min_price, lows[i]);
            max_price = std::max(max_price, highs[i]);
        }

        if (min_price != DBL_MAX && max_price != -DBL_MAX) {
            double padding = (max_price - min_price) * 0.05;
            ImPlot::SetupAxisLimits(ImAxis_Y1, min_price - padding, max_price + padding, ImPlotCond_Always);
        } else if (!ohlcv_data_.isRawDataEmpty()) { // Fallback if no data in current view but data exists
             const auto& raw_data_vec = ohlcv_data_.getRawData();
             min_price = raw_data_vec[0].low; max_price = raw_data_vec[0].high;
             for(const auto& d : raw_data_vec) {
                 min_price = std::min(min_price, d.low);
                 max_price = std::max(max_price, d.high);
             }
             double padding = (max_price - min_price) * 0.05;
             ImPlot::SetupAxisLimits(ImAxis_Y1, min_price - padding, max_price + padding, ImPlotCond_Once);
        }


        ImVec4 bullCol = ImVec4(0.000f, 1.000f, 0.441f, 1.000f);
        ImVec4 bearCol = ImVec4(0.853f, 0.050f, 0.310f, 1.000f);
        float candle_width_percent = 0.25f; // Default width

        // Calculate candle width using the same logic as volume bars
        double candle_width_plot_units;
        if (hide_empty_candles_) {
            candle_width_plot_units = candle_width_percent * 1.0; // 1.0 unit for index-based
        } else {
            candle_width_plot_units = candle_width_percent * ((ohlcv_data_.getProcessedDataCount() > 1 && (times[1] - times[0] > 0)) ? (times[1] - times[0]) : (86400.0 / 24));
        }

        {
            int plot_start_idx = start_idx;
            int plot_end_idx = std::min(end_idx, static_cast<int>(ohlcv_data_.getProcessedDataCount()) - 1);
            int plot_count = (plot_end_idx >= plot_start_idx) ? (plot_end_idx - plot_start_idx + 1) : 0;
            MyImPlot::PlotCandlestick(symbol_.c_str(),
                                      times.data() + plot_start_idx,
                                      opens.data() + plot_start_idx,
                                      closes.data() + plot_start_idx,
                                      lows.data() + plot_start_idx,
                                      highs.data() + plot_start_idx,
                                      plot_count,
                                      candle_width_percent,
                                      bullCol,
                                      bearCol,
                                      candle_width_plot_units);
        }

        // Draw news event markers above candles
        if (!NewsSeries.empty()) {
            ImDrawList* draw_list = ImPlot::GetPlotDrawList();
            double half_width_plot_units;
            if (hide_empty_candles_) {
                half_width_plot_units = candle_width_percent * 0.5 * 1.0;
            } else {
                half_width_plot_units = candle_width_percent * 0.5 * ((ohlcv_data_.getProcessedDataCount() > 1 && (times[1] - times[0] > 0)) ? (times[1] - times[0]) : (86400.0 / 24));
            }
            
            // Pre-calculate triangle constants for performance
            constexpr float side = 8.0f;
            constexpr float half_side = side * 0.5f;
            constexpr float height = side * 0.8660254f; // sqrt(3)/2 * side
            constexpr float v1_y_offset = (2.0f/3.0f) * height;
            constexpr float v23_y_offset = height / 3.0f;
            
            // Get original times for proper news matching when hide_empty_candles_ is true
            const auto& original_times = ohlcv_data_.getOriginalTimes();
            
            // Efficiently find news events within visible time range (performance optimization)
            double visible_start_time, visible_end_time;
            if (hide_empty_candles_) {
                // When hiding gaps, use original timestamps for range checking
                visible_start_time = (start_idx < static_cast<int>(original_times.size())) ? original_times[start_idx] : times[start_idx];
                visible_end_time = (end_idx < static_cast<int>(original_times.size())) ? original_times[end_idx] : times[end_idx];
            } else {
                // When not hiding gaps, use plot times directly
                visible_start_time = times[start_idx];
                visible_end_time = times[end_idx];
            }
            
            // Calculate time interval for candle timeframe
            double time_interval = (ohlcv_data_.getProcessedDataCount() > 1 && (times[1] - times[0] > 0)) ? (times[1] - times[0]) : (86400.0 / 24);
            
            // Expand search range to cover entire visible timeframe
            double search_start = visible_start_time - time_interval;
            double search_end = visible_end_time + time_interval;
            
            // Use binary search to find news events within visible range (O(log n) instead of O(n))
            // Assuming NewsSeries is sorted by Timestamp (if not, this optimization won't work)
            auto news_start_it = std::lower_bound(NewsSeries.begin(), NewsSeries.end(), search_start,
                [](const NewsEvent& event, double time) { return event.Timestamp < time; });
            auto news_end_it = std::upper_bound(NewsSeries.begin(), NewsSeries.end(), search_end,
                [](double time, const NewsEvent& event) { return time < event.Timestamp; });
            
            // Group news events by candle (aggregate multiple news per candle)
            // Use vector of pairs instead of map to avoid include issues
            std::vector<std::pair<int, std::vector<std::string>>> candle_news_list;
            
            // Process only the news events within visible range
            for (auto news_it = news_start_it; news_it != news_end_it; ++news_it) {
                const auto& news_event = *news_it;
                
                // Find the candle that contains this news event's timeframe
                int target_candle_idx = -1;
                
                for (int idx = start_idx; idx <= end_idx && idx < static_cast<int>(ohlcv_data_.getProcessedDataCount()); ++idx) {
                    // For news matching, we need to use the correct time reference
                    double candle_time;
                    if (hide_empty_candles_) {
                        // When hiding gaps, use original timestamp for news matching
                        candle_time = (idx < static_cast<int>(original_times.size())) ? original_times[idx] : times[idx];
                    } else {
                        // When not hiding gaps, use the plot time directly
                        candle_time = times[idx];
                    }
                    
                    // Check if news event falls within this candle's timeframe
                    double candle_start = candle_time - time_interval * 0.5;
                    double candle_end = candle_time + time_interval * 0.5;
                    
                    if (news_event.Timestamp >= candle_start && news_event.Timestamp <= candle_end) {
                        target_candle_idx = idx;
                        break;
                    }
                }
                
                // If no exact match, find the closest candle
                if (target_candle_idx == -1) {
                    double min_distance = DBL_MAX;
                    for (int idx = start_idx; idx <= end_idx && idx < static_cast<int>(ohlcv_data_.getProcessedDataCount()); ++idx) {
                        double candle_time;
                        if (hide_empty_candles_) {
                            candle_time = (idx < static_cast<int>(original_times.size())) ? original_times[idx] : times[idx];
                        } else {
                            candle_time = times[idx];
                        }
                        
                        double distance = fabs(news_event.Timestamp - candle_time);
                        if (distance < min_distance) {
                            min_distance = distance;
                            target_candle_idx = idx;
                        }
                    }
                }
                
                // Add news to the target candle's news list
                if (target_candle_idx != -1) {
                    // Find existing entry or create new one
                    bool found = false;
                    for (auto& entry : candle_news_list) {
                        if (entry.first == target_candle_idx) {
                            entry.second.push_back(news_event.Text);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        candle_news_list.push_back({target_candle_idx, {news_event.Text}});
                    }
                }
            }
            
            // Draw triangles for candles that have news
            for (const auto& candle_news_pair : candle_news_list) {
                int candle_idx = candle_news_pair.first;
                const auto& news_texts = candle_news_pair.second;
                
                double x_mid = times[candle_idx]; // Use plot coordinate for positioning
                double y_top = highs[candle_idx];
                // Compute a small vertical offset in plot units (2% of current price range)
                double y_offset_plot = (max_price - min_price) * 0.02;
                // Convert to screen coordinates at the offset price
                ImVec2 p_mid = ImPlot::PlotToPixels(x_mid, y_top + y_offset_plot);
                
                // Equilateral triangle centered at p_mid, pointing upwards
                ImVec2 v1(p_mid.x, p_mid.y - v1_y_offset);
                ImVec2 v2(p_mid.x - half_side, p_mid.y + v23_y_offset);
                ImVec2 v3(p_mid.x + half_side, p_mid.y + v23_y_offset);
                
                draw_list->AddTriangleFilled(v1, v2, v3, IM_COL32(255, 255, 0, 255));
                
                // Tooltip on hover using triangle bounding rect - show all aggregated news
                ImVec2 tri_min(p_mid.x - half_side, p_mid.y - v1_y_offset);
                ImVec2 tri_max(p_mid.x + half_side, p_mid.y + v23_y_offset);
                if (ImGui::IsMouseHoveringRect(tri_min, tri_max)) {
                    ImGui::BeginTooltip();
                    for (size_t i = 0; i < news_texts.size(); ++i) {
                        ImGui::TextUnformatted(news_texts[i].c_str());
                        if (i < news_texts.size() - 1) {
                            ImGui::Separator(); // Add separator between multiple news items
                        }
                    }
                    ImGui::EndTooltip();
                }
            }
        }

        if (show_tooltip_ && ImPlot::IsPlotHovered()) {
            ImPlotPoint mouse_plot_coords = ImPlot::GetPlotMousePos(ImAxis_X1);
            double item_full_width_plot_units;
             if (hide_empty_candles_) { // Index based
                item_full_width_plot_units = 1.0; // Each bar is 1 unit wide
            } else { // Time based
                item_full_width_plot_units = (ohlcv_data_.getProcessedDataCount() > 1 && (times[1] - times[0] > 0)) ? (times[1] - times[0]) : (86400.0 / 24); // Default to 1 hour if interval is 0 or single point
            }
            int current_hover_idx = FindHoveredBarIndex(mouse_plot_coords.x, times.data(), static_cast<int>(ohlcv_data_.getProcessedDataCount()), item_full_width_plot_units, !hide_empty_candles_);
            if (current_hover_idx != -1) {
                hovered_idx_ = current_hover_idx;
                hovered_x_plot_val_ = times[hovered_idx_];
                is_tooltip_active_ = true;
            }
        }

        if (is_tooltip_active_ && hovered_idx_ != -1 && hovered_idx_ < static_cast<int>(ohlcv_data_.getProcessedDataCount())) {
            ImPlotRect plot_limits_px = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
             if (hovered_x_plot_val_ >= plot_limits_px.X.Min && hovered_x_plot_val_ <= plot_limits_px.X.Max) {
                ImDrawList* draw_list = ImPlot::GetPlotDrawList();
                double half_visual_width_plot_units;
                 if (hide_empty_candles_) { // Index based
                    half_visual_width_plot_units = candle_width_percent * 0.5 * 1.0; // 1.0 is (times[1]-times[0])
                } else { // Time based
                     half_visual_width_plot_units = candle_width_percent * 0.5 * ((ohlcv_data_.getProcessedDataCount() > 1 && (times[1] - times[0] > 0)) ? (times[1] - times[0]) : (86400.0/24));
                }

                float hl_l = ImPlot::PlotToPixels(hovered_x_plot_val_ - half_visual_width_plot_units, 0).x;
                float hl_r = ImPlot::PlotToPixels(hovered_x_plot_val_ + half_visual_width_plot_units, 0).x;
                float hl_t = ImPlot::GetPlotPos().y;
                float hl_b = hl_t + ImPlot::GetPlotSize().y;
                ImPlot::PushPlotClipRect();
                draw_list->AddRectFilled(ImVec2(hl_l, hl_t), ImVec2(hl_r, hl_b), IM_COL32(128, 128, 128, 64));
                ImPlot::PopPlotClipRect();
            }
        }

        ImPlot::EndPlot();
    }
}

void CandlestickChart::RenderVolumePlotPane() {
    if (ohlcv_data_.getProcessedDataCount() == 0) return;

    const auto& times = ohlcv_data_.getTimes();
    const auto& volumes = ohlcv_data_.getVolumes();
    const auto& original_times_for_formatter = ohlcv_data_.getOriginalTimes(); // Get for formatter

    if (ImPlot::BeginPlot("##VolumePlot", ImVec2(-1, 200), ImPlotFlags_NoTitle )) { // Removed NoMenus, NoBoxSelect for volume
        ImPlot::SetupAxes("Date", "Volume"); // X-axis label for volume chart

        volume_formatter_user_data_.timeframe = current_timeframe_str_;
        volume_formatter_user_data_.hide_gaps = hide_empty_candles_;
        volume_formatter_user_data_.original_times_ptr = &original_times_for_formatter; // Update pointer

        ImPlot::SetupAxisFormat(ImAxis_X1, VolumeTimeAxisFormatter, (void*)&volume_formatter_user_data_);
        ImPlot::SetupAxisScale(ImAxis_X1, hide_empty_candles_ ? ImPlotScale_Linear : ImPlotScale_Time);

        ImPlot::SetupAxisLinks(ImAxis_X1, &shared_x_min_, &shared_x_max_);
        // shared_x_min_ and shared_x_max_ now correctly hold either the "fit" values
        // or the user's current pan/zoom state.
        // Apply these limits only if fit_x_axis_on_next_draw_ is true, otherwise let ImPlot manage them.
        if (fit_x_axis_on_next_draw_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, shared_x_min_, shared_x_max_, ImPlotCond_Always);
        }


        int count = static_cast<int>(ohlcv_data_.getProcessedDataCount());
        int start_idx;
        int end_idx;
        if (hide_empty_candles_) {
            start_idx = static_cast<int>(std::max(0.0, floor(shared_x_min_)));
            end_idx = static_cast<int>(std::min(ceil(shared_x_max_), static_cast<double>(count - 1)));
        } else {
            const double* times_data = times.data();
            if (count > 0) {
                auto it_s = std::lower_bound(times_data, times_data + count, shared_x_min_);
                int s_idx_val = static_cast<int>(std::distance(times_data, it_s));
                auto it_e = std::upper_bound(times_data, times_data + count, shared_x_max_);
                int e_idx_val = static_cast<int>(std::distance(times_data, it_e)) - 1;
                int s = s_idx_val - 1;
                int e = e_idx_val + 1;
                s = std::max(0, s);
                e = std::min(e, count - 1);
                start_idx = s;
                end_idx = e;
            } else {
                start_idx = 0;
                end_idx = -1;
            }
        }
        double max_volume = 0;

        for (int i = start_idx; i <= end_idx && i < static_cast<int>(ohlcv_data_.getProcessedDataCount()); ++i) {
            max_volume = std::max(max_volume, volumes[i]);
        }

        if (max_volume > 0) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_volume * 1.1, ImPlotCond_Always);
        } else if (!ohlcv_data_.isRawDataEmpty()) { // Fallback
            const auto& raw_data_vec = ohlcv_data_.getRawData();
            for(const auto& d : raw_data_vec) max_volume = std::max(max_volume, d.volume);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_volume * 1.1, ImPlotCond_Once);
        }


        double bar_width_plot_units;
        if (hide_empty_candles_) {
            bar_width_plot_units = 0.8; // 80% of the 1.0 unit for index-based bars
        } else {
            bar_width_plot_units = (ohlcv_data_.getProcessedDataCount() > 1 && (times[1] - times[0] > 0)) ? (times[1] - times[0]) * 0.8 : (86400.0 * 0.8 / 24) ; // 80% of time interval, default 1hr
        }
        {
            int plot_count = (end_idx >= start_idx) ? (end_idx - start_idx + 1) : 0;
            ImPlot::PlotBars("Volume",
                             times.data() + start_idx,
                             volumes.data() + start_idx,
                             plot_count,
                             bar_width_plot_units);
        }

        if (show_tooltip_ && ImPlot::IsPlotHovered()) {
            ImPlotPoint mouse_plot_coords = ImPlot::GetPlotMousePos(ImAxis_X1);
             double item_full_width_plot_units; // Re-declare for scope
            if (hide_empty_candles_) {
                item_full_width_plot_units = 1.0;
            } else {
                item_full_width_plot_units = (ohlcv_data_.getProcessedDataCount() > 1 && (times[1] - times[0] > 0)) ? (times[1] - times[0]) : (86400.0 / 24);
            }
            int current_hover_idx = FindHoveredBarIndex(mouse_plot_coords.x, times.data(), static_cast<int>(ohlcv_data_.getProcessedDataCount()), item_full_width_plot_units, !hide_empty_candles_);
            if (current_hover_idx != -1) {
                hovered_idx_ = current_hover_idx;
                hovered_x_plot_val_ = times[hovered_idx_];
                is_tooltip_active_ = true;
            }
        }

        if (is_tooltip_active_ && hovered_idx_ != -1 && hovered_idx_ < static_cast<int>(ohlcv_data_.getProcessedDataCount())) {
            ImPlotRect plot_limits_px = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1);
            if (hovered_x_plot_val_ >= plot_limits_px.X.Min && hovered_x_plot_val_ <= plot_limits_px.X.Max) {
                ImDrawList* draw_list = ImPlot::GetPlotDrawList();
                double half_bar_width_plot_units = bar_width_plot_units * 0.5;

                float hl_l = ImPlot::PlotToPixels(hovered_x_plot_val_ - half_bar_width_plot_units, 0).x;
                float hl_r = ImPlot::PlotToPixels(hovered_x_plot_val_ + half_bar_width_plot_units, 0).x;
                float hl_t = ImPlot::GetPlotPos().y;
                float hl_b = hl_t + ImPlot::GetPlotSize().y;
                ImPlot::PushPlotClipRect();
                draw_list->AddRectFilled(ImVec2(hl_l, hl_t), ImVec2(hl_r, hl_b), IM_COL32(128, 128, 128, 64));
                ImPlot::PopPlotClipRect();
            }
        }

        ImPlot::EndPlot();
    }
}

void CandlestickChart::RenderUnifiedTooltip() {
    if (show_tooltip_ && is_tooltip_active_ && hovered_idx_ != -1 && hovered_idx_ < static_cast<int>(ohlcv_data_.getProcessedDataCount())) {
        ImGui::BeginTooltip();
        char date_buff[64];
        
        const auto& times = ohlcv_data_.getTimes();
        const auto& original_times = ohlcv_data_.getOriginalTimes();
        const auto& opens = ohlcv_data_.getOpens();
        const auto& highs = ohlcv_data_.getHighs();
        const auto& lows = ohlcv_data_.getLows();
        const auto& closes = ohlcv_data_.getCloses();
        const auto& volumes = ohlcv_data_.getVolumes();

        double display_time = hide_empty_candles_ ? original_times[hovered_idx_] : times[hovered_idx_];
        time_t time_val_tooltip = static_cast<time_t>(display_time);
        tm* tm_info_tooltip = ImPlot::GetStyle().UseLocalTime ? localtime(&time_val_tooltip) : gmtime(&time_val_tooltip);

        if (tm_info_tooltip) {
            if (current_timeframe_str_ == "1d") {
                strftime(date_buff, sizeof(date_buff), ImPlot::GetStyle().UseISO8601 ? "%Y-%m-%d" : "%d/%m/%Y", tm_info_tooltip);
            } else {
                strftime(date_buff, sizeof(date_buff), ImPlot::GetStyle().Use24HourClock ? "%Y-%m-%d %H:%M" : "%d/%m/%Y %I:%M %p", tm_info_tooltip);
            }
        } else {
            snprintf(date_buff, sizeof(date_buff), "Invalid Time");
        }

        ImGui::Text("Date:   %s", date_buff);
        ImGui::Text("Open:   $%.2f", opens[hovered_idx_]);
        ImGui::Text("High:   $%.2f", highs[hovered_idx_]);
        ImGui::Text("Low:    $%.2f", lows[hovered_idx_]);
        ImGui::Text("Close:  $%.2f", closes[hovered_idx_]);
        ImGui::Text("Volume: %.0f", volumes[hovered_idx_]);
        ImGui::EndTooltip();
    }
}

// --- Static Axis Formatters ---
int CandlestickChart::CandlestickTimeAxisFormatter(double value, char* buff, int size, void* user_data) {
    const std::string& tf_str = *(static_cast<std::string*>(user_data));
    time_t time_val = static_cast<time_t>(value);
    tm* tm_info = ImPlot::GetStyle().UseLocalTime ? localtime(&time_val) : gmtime(&time_val);

    if (!tm_info) return snprintf(buff, size, "Invalid Time");

    if (tf_str == "1d") {
        return strftime(buff, size, ImPlot::GetStyle().UseISO8601 ? "%Y-%m-%d" : "%d/%m/%Y", tm_info);
    } else {
        return strftime(buff, size, ImPlot::GetStyle().Use24HourClock ? "%H:%M" : "%I:%M %p", tm_info);
    }
}

int CandlestickChart::VolumeTimeAxisFormatter(double value, char* buff, int size, void* user_data) {
    VolumeFormatterUserData* ud = static_cast<VolumeFormatterUserData*>(user_data);
    time_t time_val;

    if (ud->hide_gaps) {
        int idx = static_cast<int>(round(value));
        if (ud->original_times_ptr && idx >= 0 && idx < static_cast<int>(ud->original_times_ptr->size())) {
            time_val = static_cast<time_t>((*ud->original_times_ptr)[idx]);
        } else {
            return snprintf(buff, size, " "); // Or some other placeholder for invalid index
        }
    } else {
        time_val = static_cast<time_t>(value);
    }

    tm* tm_info = ImPlot::GetStyle().UseLocalTime ? localtime(&time_val) : gmtime(&time_val);
    if (!tm_info) return snprintf(buff, size, "Invalid Time");

    if (ud->timeframe == "1d") {
        return strftime(buff, size, ImPlot::GetStyle().UseISO8601 ? "%Y-%m-%d" : "%d/%m/%Y", tm_info);
    } else {
        // For intraday, show date as well if the span is large, or just time if small.
        // This is a simple heuristic; more advanced logic could check plot range.
        // For now, always show date for non-1d for clarity in tooltips/axis.
        return strftime(buff, size, ImPlot::GetStyle().Use24HourClock ? "%Y-%m-%d %H:%M" : "%d/%m/%Y %I:%M %p", tm_info);
    }
}

bool CandlestickChart::UpdateAnalyticsDataFrameFromRaw(const std::vector<OHLCVData>& raw, std::string* error) {
    auto frame = BuildAnalyticsFrameFromRaw(raw, error);
    if (!frame) {
        ohlcv_dataframe_.reset();
        return false;
    }
    ohlcv_dataframe_ = std::move(frame);
    return true;
}

bool CandlestickChart::PopulateFromDataFrame(chronosflow::AnalyticsDataFrame&& df, std::string* statusMessage) {
    auto table = df.get_cpu_table();
    if (!table) {
        if (statusMessage) {
            *statusMessage = "QuestDB returned an empty dataset.";
        }
        return false;
    }

    auto schema = table->schema();
    auto requireColumn = [&](const char* name) -> std::shared_ptr<arrow::ChunkedArray> {
        int idx = schema->GetFieldIndex(name);
        if (idx < 0) {
            return nullptr;
        }
        return table->column(idx);
    };

    auto timestampCol = requireColumn("timestamp_unix");
    if (!timestampCol) {
        timestampCol = requireColumn("timestamp");
    }
    auto openCol = requireColumn("open");
    auto highCol = requireColumn("high");
    auto lowCol = requireColumn("low");
    auto closeCol = requireColumn("close");
    auto volumeCol = requireColumn("volume");

    if (!timestampCol || !openCol || !highCol || !lowCol || !closeCol || !volumeCol) {
        if (statusMessage) {
            std::vector<std::string> missing;
            if (!timestampCol) missing.emplace_back("timestamp_unix");
            if (!openCol) missing.emplace_back("open");
            if (!highCol) missing.emplace_back("high");
            if (!lowCol) missing.emplace_back("low");
            if (!closeCol) missing.emplace_back("close");
            if (!volumeCol) missing.emplace_back("volume");
            std::ostringstream oss;
            oss << "Measurement is missing required OHLCV columns: ";
            for (size_t i = 0; i < missing.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << missing[i];
            }
            oss << ". Available columns: ";
            if (schema) {
                for (int i = 0; i < schema->num_fields(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << schema->field(i)->name();
                }
            }
            *statusMessage = oss.str();
        }
        return false;
    }

    const int64_t rows = table->num_rows();
    std::vector<OHLCVData> imported;
    imported.reserve(rows);

    for (int64_t row = 0; row < rows; ++row) {
        auto tsScalar = timestampCol->GetScalar(row);
        auto openScalar = openCol->GetScalar(row);
        auto highScalar = highCol->GetScalar(row);
        auto lowScalar = lowCol->GetScalar(row);
        auto closeScalar = closeCol->GetScalar(row);
        auto volumeScalar = volumeCol->GetScalar(row);

        if (!tsScalar.ok() || !openScalar.ok() || !highScalar.ok() ||
            !lowScalar.ok() || !closeScalar.ok() || !volumeScalar.ok()) {
            continue;
        }

        auto timestampMillis = ScalarToMillis(tsScalar.ValueOrDie());
        if (!timestampMillis) {
            continue;
        }

        OHLCVData point;
        point.time = static_cast<time_t>(*timestampMillis / 1000);
        point.open = std::static_pointer_cast<arrow::DoubleScalar>(openScalar.ValueOrDie())->value;
        point.high = std::static_pointer_cast<arrow::DoubleScalar>(highScalar.ValueOrDie())->value;
        point.low = std::static_pointer_cast<arrow::DoubleScalar>(lowScalar.ValueOrDie())->value;
        point.close = std::static_pointer_cast<arrow::DoubleScalar>(closeScalar.ValueOrDie())->value;
        point.volume = std::static_pointer_cast<arrow::DoubleScalar>(volumeScalar.ValueOrDie())->value;
        imported.push_back(point);
    }

    if (imported.empty()) {
        if (statusMessage) {
            *statusMessage = "OHLCV measurement contained no rows.";
        }
        return false;
    }

    ohlcv_data_.setData(imported);
    ohlcv_data_.processData(false);
    ohlcv_dataframe_ = std::make_unique<chronosflow::AnalyticsDataFrame>(std::move(df));
    return true;
}

std::pair<std::optional<int64_t>, std::optional<int64_t>> CandlestickChart::GetTimestampBoundsMs() const {
    const auto& raw = ohlcv_data_.getRawData();
    if (raw.empty()) {
        return {};
    }
    std::optional<int64_t> first = static_cast<int64_t>(raw.front().time) * 1000LL;
    std::optional<int64_t> last = static_cast<int64_t>(raw.back().time) * 1000LL;
    return {first, last};
}


// --- Public Render Method ---
void CandlestickChart::Render() {
    if (!show_ohlcv_window_) {
        return;
    }

    // Use a stable window ID that doesn't change with ticker symbol to preserve window size/position
    std::string window_title = symbol_ + " OHLCV Chart";
    // Example: Set a default size for the first time the window appears.
    // User can then resize and ImGui will remember it for "OHLCV Chart##CandlestickChartMain".
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    // Optionally, set initial position:
    // ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGui::Begin("OHLCV Chart##CandlestickChartMain", &show_ohlcv_window_);
    
    // Display the current symbol in the window content instead of the title
    ImGui::Text("Symbol: %s", symbol_.c_str());
    ImGui::Separator();

    frame_count_global_debug++;


    is_tooltip_active_ = false; // Reset tooltip active state each frame for this chart instance

    CheckAndProcessLoadedData();
    RenderControls(); // MOVED here, above the plots

    if (is_loading_data_) {
        ImGui::Text("Loading %s data...", symbol_.c_str());
    } else if (!data_loaded_) {
        if (!data_loading_error_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", data_loading_error_.c_str());
        }
        if (ImGui::Button(("Load " + symbol_ + " Data").c_str())) {
            RequestLoadData();
        }
    } else { // Data is loaded
        ohlcv_data_.processData(hide_empty_candles_);


        // Calculate shared_x_min_ and shared_x_max_ based on fit_x_axis_on_next_draw_
        if (data_loaded_ && ohlcv_data_.getProcessedDataCount() > 0) {
            if (fit_x_axis_on_next_draw_) {
                const auto& times = ohlcv_data_.getTimes(); // Assuming ohlcv_data_.processData() has been called
                if (hide_empty_candles_) {
                    shared_x_min_ = 0;
                    shared_x_max_ = static_cast<double>(ohlcv_data_.getProcessedDataCount() - 1);
                    if (ohlcv_data_.getProcessedDataCount() <= 1) { // Handle single data point
                         shared_x_max_ = shared_x_min_ + 1.0; // e.g., 0 to 1
                    }
                } else {
                    shared_x_min_ = times.front();
                    shared_x_max_ = times.back();
                    if (ohlcv_data_.getProcessedDataCount() <= 1) { // Handle single data point
                        shared_x_max_ = shared_x_min_ + 3600; // Default to 1 hour range
                    }
                }
                // Ensure shared_x_max_ is greater than shared_x_min_
                if (shared_x_max_ <= shared_x_min_) {
                    shared_x_max_ = shared_x_min_ + (hide_empty_candles_ ? 1.0 : 3600.0);
                }
            }
            // `shared_x_min_` and `shared_x_max_` now either reflect the new fit
            // or the user's last pan/zoom state (updated by ImPlot via linking).
        } else if (fit_x_axis_on_next_draw_) { // No data, but tried to fit
            // Fallback for empty data after load attempt
            shared_x_min_ = static_cast<double>(from_time_);
            shared_x_max_ = static_cast<double>(to_time_);
            if (shared_x_max_ <= shared_x_min_) {
                shared_x_max_ = shared_x_min_ + 3600.0;
            }
        }
 
        // Update visible range indices based on shared_x_min_/shared_x_max_
        {
            const auto& times = ohlcv_data_.getTimes();
            int N = static_cast<int>(times.size());
            if (N > 0) {
                if (shared_x_min_ < prev_x_min_ || shared_x_max_ > prev_x_max_) {
                    auto lb = std::lower_bound(times.begin(), times.end(), shared_x_min_);
                    int s = static_cast<int>(lb - times.begin()) - 1;
                    visible_start_idx_ = s < 0 ? 0 : s;
                    auto ub = std::upper_bound(times.begin(), times.end(), shared_x_max_);
                    int e = static_cast<int>(ub - times.begin()) + 1;
                    visible_end_idx_ = e >= N ? N - 1 : e;
                    prev_x_min_ = shared_x_min_;
                    prev_x_max_ = shared_x_max_;
                } else {
                    while (visible_start_idx_ < N && times[visible_start_idx_] < shared_x_min_) ++visible_start_idx_;
                    while (visible_start_idx_ > 0 && times[visible_start_idx_ - 1] >= shared_x_min_) --visible_start_idx_;
                    while (visible_end_idx_ >= 0 && times[visible_end_idx_] > shared_x_max_) --visible_end_idx_;
                    while (visible_end_idx_ + 1 < N && times[visible_end_idx_ + 1] <= shared_x_max_) ++visible_end_idx_;
                }
            } else {
                visible_start_idx_ = 0;
                visible_end_idx_ = -1;
            }
        }

        // Push style to minimize spacing between plots
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4)); // Window padding is for the window itself, not between plots

        if (ImPlot::BeginAlignedPlots("OHLCVPlotsLinked", true)) { // Use a unique ID for aligned plots
            RenderCandlestickPlotPane(); // This might set candlestick_plot_modified_
            RenderVolumePlotPane();    // This might set volume_plot_modified_
            ImPlot::EndAlignedPlots();
        }
        if (fit_x_axis_on_next_draw_) {
            fit_x_axis_on_next_draw_ = false; // Reset the flag for the next frame
        }
 
 


        ImGui::PopStyleVar(); // Pop ItemSpacing
        // ImGui::PopStyleVar(); // Pop WindowPadding if it was pushed

        RenderUnifiedTooltip();


    }
    ImGui::End();
    
    // Render ticker selector popup outside of window context to fix z-order issues
    ticker_selector_.RenderPopupOutsideWindow();
}

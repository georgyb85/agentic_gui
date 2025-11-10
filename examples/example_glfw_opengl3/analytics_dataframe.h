#pragma once

#include "tssb_timestamp.h"
#include "column_view.h"
#include <arrow/table.h>
#include <arrow/result.h>
#include <memory>
#include <vector>
#include <string>
#include <optional>

#ifdef WITH_CUDA
#include <cudf/table/table.hpp>
#endif

namespace chronosflow {

// Add this new enum
enum class TimeFormat {
    HHMM,      // e.g., 930, 1415
    HHMMSS,    // e.g., 93000, 141530
    NONE       // No time column present
};

enum class DataLocation {
    CPU,
    GPU
};

class AnalyticsDataFrame {
public:
    AnalyticsDataFrame();
    explicit AnalyticsDataFrame(std::shared_ptr<arrow::Table> cpu_table);
    
    AnalyticsDataFrame(const AnalyticsDataFrame&) = delete;
    AnalyticsDataFrame& operator=(const AnalyticsDataFrame&) = delete;
    
    AnalyticsDataFrame(AnalyticsDataFrame&&) = default;
    AnalyticsDataFrame& operator=(AnalyticsDataFrame&&) = default;

    arrow::Result<AnalyticsDataFrame> to_gpu() const;
    arrow::Result<AnalyticsDataFrame> to_cpu() const;

    arrow::Result<AnalyticsDataFrame> slice_by_row_index(
        int64_t start, int64_t end) const;

    arrow::Result<AnalyticsDataFrame> select_rows_by_timestamp(
        const TSSBTimestamp& start, const TSSBTimestamp& end) const;

    arrow::Result<AnalyticsDataFrame> select_columns(
        const std::vector<std::string>& column_names) const;

    arrow::Result<AnalyticsDataFrame> with_iso_timestamp(
        const std::string& output_column_name,
        TimeFormat time_format) const;

    arrow::Result<AnalyticsDataFrame> with_unix_timestamp(
        const std::string& output_column_name,
        TimeFormat time_format) const;

    arrow::Result<std::vector<AnalyticsDataFrame>> create_rolling_windows(
        int64_t window_size, int64_t step_size = 1) const;

    template<typename T>
    arrow::Result<ColumnView<T>> get_column_view(const std::string& column_name) const;

    void set_tssb_metadata(const std::string& date_column, const std::string& time_column);

    int64_t num_rows() const;
    int64_t num_columns() const;
    std::vector<std::string> column_names() const;
    
    bool is_on_gpu() const;
    bool has_tssb_metadata() const;
    
    // Getter for internal table (needed for I/O operations)
    std::shared_ptr<arrow::Table> get_cpu_table() const { return cpu_table_; }

private:
    std::shared_ptr<arrow::Table> cpu_table_;
    
#ifdef WITH_CUDA
    std::shared_ptr<cudf::table> gpu_table_;
#endif
    
    // Independent schema management to avoid expensive data transfers
    std::shared_ptr<arrow::Schema> schema_;
    
    DataLocation location_ = DataLocation::CPU;
    std::optional<std::string> tssb_date_column_;
    std::optional<std::string> tssb_time_column_;
    
    // Allow FeatureUtils to access private members for GPU operations
    friend class FeatureUtils;

    arrow::Result<AnalyticsDataFrame> create_from_cpu_table(
        std::shared_ptr<arrow::Table> table) const;

#ifdef WITH_CUDA
    arrow::Result<AnalyticsDataFrame> create_from_gpu_table(
        std::shared_ptr<cudf::table> table) const;
#endif
};

template<typename T>
arrow::Result<ColumnView<T>> AnalyticsDataFrame::get_column_view(
    const std::string& column_name) const {
    
#ifdef WITH_CUDA
    if (is_on_gpu() && gpu_table_) {
        // GPU path: need to find column index by name
        if (!schema_) {
            return arrow::Status::Invalid("No schema available");
        }
        
        auto field_index = schema_->GetFieldIndex(column_name);
        if (field_index == -1) {
            return arrow::Status::Invalid("Column not found: ", column_name);
        }
        
        return ColumnView<T>::from_cudf_column(gpu_table_, static_cast<std::size_t>(field_index));
    }
#endif
    
    if (cpu_table_) {
        return ColumnView<T>::from_arrow_column(cpu_table_, column_name);
    }
    
    return arrow::Status::Invalid("No data available");
}

} // namespace chronosflow
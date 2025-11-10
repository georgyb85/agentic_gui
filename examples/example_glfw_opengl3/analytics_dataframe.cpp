#include "analytics_dataframe.h"
#include <arrow/compute/api.h>
#include <arrow/compute/expression.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/datum.h>
#include <arrow/compute/cast.h>
#include <algorithm>
#include <ctime> // Use <ctime> for time_t, tm, etc.

#ifdef WITH_CUDA
#include <cudf/interop.hpp>
#endif

// Platform-independent fix for timegm.
// MSVC uses _mkgmtime, whereas POSIX systems (Linux, macOS) use timegm.
// This preprocessor directive makes `timegm` available on all platforms.
#ifdef _WIN32
#define timegm _mkgmtime
#endif


namespace chronosflow {

    AnalyticsDataFrame::AnalyticsDataFrame() = default;

    AnalyticsDataFrame::AnalyticsDataFrame(std::shared_ptr<arrow::Table> cpu_table)
        : cpu_table_(std::move(cpu_table)), location_(DataLocation::CPU) {
        if (cpu_table_) {
            schema_ = cpu_table_->schema();
        }
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::to_gpu() const {
#ifdef WITH_CUDA
        if (location_ == DataLocation::GPU) {
            return *this;
        }

        if (!cpu_table_) {
            return arrow::Status::Invalid("No CPU data to transfer to GPU");
        }

        auto gpu_table = cudf::from_arrow(*cpu_table_);
        if (!gpu_table.ok()) {
            return arrow::Status::UnknownError("Failed to transfer data to GPU");
        }

        AnalyticsDataFrame result;
        result.cpu_table_ = cpu_table_;
        result.gpu_table_ = std::make_shared<cudf::table>(std::move(gpu_table.value()));
        result.location_ = DataLocation::GPU;
        result.schema_ = schema_;
        result.tssb_date_column_ = tssb_date_column_;
        result.tssb_time_column_ = tssb_time_column_;

        return result;
#else
        return arrow::Status::NotImplemented("CUDA support not enabled");
#endif
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::to_cpu() const {
        if (location_ == DataLocation::CPU) {
            AnalyticsDataFrame result;
            result.cpu_table_ = cpu_table_;
            result.location_ = DataLocation::CPU;
            result.schema_ = schema_;
            result.tssb_date_column_ = tssb_date_column_;
            result.tssb_time_column_ = tssb_time_column_;
            return result;
        }

#ifdef WITH_CUDA
        if (location_ == DataLocation::GPU && gpu_table_) {
            auto cpu_table = cudf::to_arrow(*gpu_table_);
            if (!cpu_table.ok()) {
                return arrow::Status::UnknownError("Failed to transfer data to CPU");
            }

            AnalyticsDataFrame result;
            result.cpu_table_ = cpu_table.value();
            result.location_ = DataLocation::CPU;
            result.schema_ = schema_;
            result.tssb_date_column_ = tssb_date_column_;
            result.tssb_time_column_ = tssb_time_column_;
            return result;
        }
#endif

        return arrow::Status::Invalid("No data available");
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::slice_by_row_index(
        int64_t start, int64_t end) const {

#ifdef WITH_CUDA
        if (location_ == DataLocation::GPU && gpu_table_) {
            if (start < 0 || end > gpu_table_->num_rows() || start >= end) {
                return arrow::Status::Invalid("Invalid row indices");
            }

            // Use cuDF slice operation
            auto sliced_gpu_table = cudf::slice(*gpu_table_, { start, end });
            if (sliced_gpu_table.empty()) {
                return arrow::Status::UnknownError("Failed to slice GPU table");
            }

            AnalyticsDataFrame result;
            result.gpu_table_ = std::make_shared<cudf::table>(std::move(sliced_gpu_table[0]));
            result.location_ = DataLocation::GPU;
            result.tssb_date_column_ = tssb_date_column_;
            result.tssb_time_column_ = tssb_time_column_;

            return result;
        }
#endif

        if (!cpu_table_) {
            return arrow::Status::Invalid("No data available");
        }

        if (start < 0 || end > cpu_table_->num_rows() || start >= end) {
            return arrow::Status::Invalid("Invalid row indices");
        }

        auto sliced_table = cpu_table_->Slice(start, end - start);
        return create_from_cpu_table(sliced_table);
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::select_rows_by_timestamp(
        const TSSBTimestamp& start, const TSSBTimestamp& end) const {

        if (!has_tssb_metadata()) {
            return arrow::Status::Invalid("TSSB metadata not set");
        }

#ifdef WITH_CUDA
        if (location_ == DataLocation::GPU && gpu_table_) {
            if (!cpu_table_) {
                return arrow::Status::Invalid("CPU table needed for column name mapping");
            }

            auto schema = cpu_table_->schema();
            auto date_field_index = schema->GetFieldIndex(*tssb_date_column_);
            auto time_field_index = schema->GetFieldIndex(*tssb_time_column_);

            if (date_field_index == -1 || time_field_index == -1) {
                return arrow::Status::Invalid("TSSB date/time columns not found");
            }

            auto date_column = gpu_table_->get_column(date_field_index);
            auto time_column = gpu_table_->get_column(time_field_index);

            auto multiplier = cudf::make_numeric_scalar<int64_t>(1000000LL);
            auto date_int64 = cudf::cast(date_column, cudf::data_type{ cudf::type_id::INT64 });
            auto date_multiplied = cudf::binary_operation(
                date_int64->view(), *multiplier, cudf::binary_operator::MUL, cudf::data_type{ cudf::type_id::INT64 });

            auto time_int64 = cudf::cast(time_column, cudf::data_type{ cudf::type_id::INT64 });
            auto combined_timestamp = cudf::binary_operation(
                date_multiplied->view(), time_int64->view(), cudf::binary_operator::ADD, cudf::data_type{ cudf::type_id::INT64 });

            auto start_combined = static_cast<int64_t>(start.date()) * 1000000LL + start.time();
            auto end_combined = static_cast<int64_t>(end.date()) * 1000000LL + end.time();

            auto start_scalar = cudf::make_numeric_scalar<int64_t>(start_combined);
            auto end_scalar = cudf::make_numeric_scalar<int64_t>(end_combined);

            auto ge_start = cudf::binary_operation(
                combined_timestamp->view(), *start_scalar, cudf::binary_operator::GREATER_EQUAL, cudf::data_type{ cudf::type_id::BOOL8 });

            auto le_end = cudf::binary_operation(
                combined_timestamp->view(), *end_scalar, cudf::binary_operator::LESS_EQUAL, cudf::data_type{ cudf::type_id::BOOL8 });

            auto final_filter = cudf::binary_operation(
                ge_start->view(), le_end->view(), cudf::binary_operator::LOGICAL_AND, cudf::data_type{ cudf::type_id::BOOL8 });

            auto filtered_gpu_table = cudf::filter(*gpu_table_, final_filter->view());
            AnalyticsDataFrame result;
            result.gpu_table_ = std::make_shared<cudf::table>(std::move(filtered_gpu_table));
            result.location_ = DataLocation::GPU;
            result.tssb_date_column_ = tssb_date_column_;
            result.tssb_time_column_ = tssb_time_column_;

            auto filtered_schema = cpu_table_->schema();
            std::vector<std::shared_ptr<arrow::ChunkedArray>> empty_columns;
            for (int i = 0; i < filtered_schema->num_fields(); ++i) {
                empty_columns.push_back(std::make_shared<arrow::ChunkedArray>(
                    arrow::MakeEmptyArray(filtered_schema->field(i)->type())));
            }
            result.cpu_table_ = arrow::Table::Make(filtered_schema, empty_columns, 0);

            return result;
        }
#endif

        if (!cpu_table_) {
            return arrow::Status::Invalid("No data available");
        }

        auto date_column = cpu_table_->GetColumnByName(*tssb_date_column_);
        auto time_column = cpu_table_->GetColumnByName(*tssb_time_column_);

        if (!date_column || !time_column) {
            return arrow::Status::Invalid("TSSB date/time columns not found");
        }

        arrow::Datum date_datum(date_column);
        arrow::Datum time_datum(time_column);

        arrow::compute::CastOptions cast_opts;
        cast_opts.to_type = arrow::int64();
        ARROW_ASSIGN_OR_RAISE(auto date_int64_res, arrow::compute::Cast(date_datum, cast_opts));
        ARROW_ASSIGN_OR_RAISE(auto time_int64_res, arrow::compute::Cast(time_datum, cast_opts));

        auto multiplier = arrow::MakeScalar(int64_t(1000000));
        ARROW_ASSIGN_OR_RAISE(auto date_multiplied_res, arrow::compute::Multiply(date_int64_res, multiplier));
        ARROW_ASSIGN_OR_RAISE(auto combined_ts_res, arrow::compute::Add(date_multiplied_res, time_int64_res));

        auto start_combined = arrow::MakeScalar(static_cast<int64_t>(start.date()) * 1000000LL + start.time());
        auto end_combined = arrow::MakeScalar(static_cast<int64_t>(end.date()) * 1000000LL + end.time());

        ARROW_ASSIGN_OR_RAISE(auto ge_start_res, arrow::compute::CallFunction("greater_equal", { combined_ts_res, start_combined }));
        ARROW_ASSIGN_OR_RAISE(auto le_end_res, arrow::compute::CallFunction("less_equal", { combined_ts_res, end_combined }));
        ARROW_ASSIGN_OR_RAISE(auto final_filter_res, arrow::compute::CallFunction("and", { ge_start_res, le_end_res }));
        ARROW_ASSIGN_OR_RAISE(auto filtered_result, arrow::compute::Filter(cpu_table_, final_filter_res));

        auto filtered_table = filtered_result.table();
        return create_from_cpu_table(filtered_table);
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::select_columns(
        const std::vector<std::string>& column_names) const {

#ifdef WITH_CUDA
        if (location_ == DataLocation::GPU && gpu_table_) {
            if (!cpu_table_) {
                return arrow::Status::Invalid("CPU table needed for column name mapping");
            }

            std::vector<cudf::size_type> column_indices;
            auto schema = cpu_table_->schema();

            for (const auto& name : column_names) {
                auto index = schema->GetFieldIndex(name);
                if (index == -1) {
                    return arrow::Status::Invalid("Column not found: ", name);
                }
                column_indices.push_back(static_cast<cudf::size_type>(index));
            }

            auto selected_gpu_table = cudf::select(*gpu_table_, column_indices);
            AnalyticsDataFrame result;
            result.gpu_table_ = std::make_shared<cudf::table>(std::move(selected_gpu_table));
            result.location_ = DataLocation::GPU;
            result.tssb_date_column_ = tssb_date_column_;
            result.tssb_time_column_ = tssb_time_column_;

            std::vector<std::shared_ptr<arrow::Field>> selected_fields;
            for (const auto& name : column_names) {
                auto index = schema->GetFieldIndex(name);
                if (index != -1) {
                    selected_fields.push_back(schema->field(index));
                }
            }

            auto selected_schema = arrow::schema(selected_fields);
            std::vector<std::shared_ptr<arrow::ChunkedArray>> empty_columns;
            for (size_t i = 0; i < selected_fields.size(); ++i) {
                empty_columns.push_back(std::make_shared<arrow::ChunkedArray>(arrow::MakeEmptyArray(selected_fields[i]->type())));
            }
            result.cpu_table_ = arrow::Table::Make(selected_schema, empty_columns, 0);

            return result;
        }
#endif

        if (!cpu_table_) {
            return arrow::Status::Invalid("No data available");
        }

        std::vector<int> column_indices;
        auto schema = cpu_table_->schema();

        for (const auto& name : column_names) {
            auto index = schema->GetFieldIndex(name);
            if (index == -1) {
                return arrow::Status::Invalid("Column not found: ", name);
            }
            column_indices.push_back(index);
        }

        ARROW_ASSIGN_OR_RAISE(auto selected_table, cpu_table_->SelectColumns(column_indices));
        return create_from_cpu_table(selected_table);
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::with_iso_timestamp(
        const std::string& output_column_name,
        TimeFormat time_format) const {

        // This function can be simplified now, though it's less critical
        // since the UI will use the Unix timestamp directly.
        ARROW_ASSIGN_OR_RAISE(auto cpu_df, to_cpu());

        ARROW_ASSIGN_OR_RAISE(auto df_with_unix, cpu_df.with_unix_timestamp("timestamp_unix_internal", time_format));

        auto unix_column = df_with_unix.get_cpu_table()->GetColumnByName("timestamp_unix_internal");
        if (!unix_column) {
            return arrow::Status::Invalid("Internal Unix timestamp creation failed.");
        }

        // Cast int64 (Unix seconds) to an Arrow timestamp[s]
        arrow::compute::CastOptions cast_opts_ts;
        cast_opts_ts.to_type = arrow::timestamp(arrow::TimeUnit::SECOND);
        ARROW_ASSIGN_OR_RAISE(auto arrow_ts_datum, arrow::compute::Cast(unix_column, cast_opts_ts));
        
        // Use Arrow's built-in `strftime` compute function for robust conversion
        arrow::compute::StrftimeOptions strftime_options("%Y-%m-%dT%H:%M:%S");
        ARROW_ASSIGN_OR_RAISE(auto iso_datum, arrow::compute::CallFunction("strftime", {arrow_ts_datum}, &strftime_options));
        
        auto iso_column = iso_datum.chunked_array();
        auto iso_field = arrow::field(output_column_name, arrow::utf8());

        // Operate on the local `cpu_df`'s table, not the `const` member `cpu_table_`.
        auto original_table = cpu_df.get_cpu_table();
        ARROW_ASSIGN_OR_RAISE(auto new_table, original_table->AddColumn(original_table->num_columns(), iso_field, iso_column));
        
        // Now, create the final result from the local cpu_df's context.
        return cpu_df.create_from_cpu_table(new_table);
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::with_unix_timestamp(
        const std::string& output_column_name,
        TimeFormat time_format) const {

        if (!has_tssb_metadata()) {
            return arrow::Status::Invalid("TSSB metadata not set");
        }
        
        // Ensure we're working on the CPU table
        ARROW_ASSIGN_OR_RAISE(auto cpu_df, to_cpu());
        auto table = cpu_df.get_cpu_table();

        if (!table) {
            return arrow::Status::Invalid("No data available");
        }

        auto date_column = table->GetColumnByName(*tssb_date_column_);
        if (!date_column) {
            return arrow::Status::Invalid("Date column not found: ", *tssb_date_column_);
        }

        std::shared_ptr<arrow::ChunkedArray> time_column;
        bool has_time = (time_format != TimeFormat::NONE && tssb_time_column_ && !tssb_time_column_->empty());
        if (has_time) {
            time_column = table->GetColumnByName(*tssb_time_column_);
            if (!time_column) {
                return arrow::Status::Invalid("Time column not found: ", *tssb_time_column_);
            }
        }

        // --- FIX: Safely cast input columns to int64 to guarantee the type ---
        arrow::compute::CastOptions cast_opts;
        cast_opts.to_type = arrow::int64();

        ARROW_ASSIGN_OR_RAISE(auto date_i64_datum, arrow::compute::Cast(date_column, cast_opts));
        auto date_i64_col = date_i64_datum.chunked_array();

        std::shared_ptr<arrow::ChunkedArray> time_i64_col;
        if (has_time) {
            ARROW_ASSIGN_OR_RAISE(auto time_i64_datum, arrow::compute::Cast(time_column, cast_opts));
            time_i64_col = time_i64_datum.chunked_array();
        }
        // --- End of Fix ---

        arrow::Int64Builder builder;
        ARROW_RETURN_NOT_OK(builder.Reserve(table->num_rows()));

        // This loop robustly handles tables with multiple chunks
        for (int c = 0; c < date_i64_col->num_chunks(); ++c) {
            // Now the cast to Int64Array is guaranteed to be safe
            auto date_chunk = std::static_pointer_cast<arrow::Int64Array>(date_i64_col->chunk(c));

            std::shared_ptr<arrow::Int64Array> time_chunk;
            if (has_time) {
                // Ensure we have a corresponding time chunk
                if (c < time_i64_col->num_chunks()) {
                    time_chunk = std::static_pointer_cast<arrow::Int64Array>(time_i64_col->chunk(c));
                } else {
                    // This case is unlikely but guards against mismatched chunking
                    return arrow::Status::Invalid("Date and Time columns have different chunking layouts.");
                }
            }

            for (int64_t i = 0; i < date_chunk->length(); ++i) {
                if (date_chunk->IsNull(i) || (has_time && time_chunk && time_chunk->IsNull(i))) {
                    ARROW_RETURN_NOT_OK(builder.AppendNull());
                    continue;
                }

                int64_t date_val = date_chunk->Value(i);

                std::tm tm = {};
                tm.tm_year = static_cast<int>(date_val / 10000) - 1900;
                tm.tm_mon = static_cast<int>((date_val % 10000) / 100) - 1;
                tm.tm_mday = static_cast<int>(date_val % 100);

                if (has_time && time_chunk) {
                    int64_t time_val = time_chunk->Value(i);
                    if (time_format == TimeFormat::HHMM) {
                        tm.tm_hour = static_cast<int>(time_val / 100);
                        tm.tm_min = static_cast<int>(time_val % 100);
                        tm.tm_sec = 0;
                    }
                    else { // HHMMSS
                        tm.tm_hour = static_cast<int>(time_val / 10000);
                        tm.tm_min = static_cast<int>((time_val % 10000) / 100);
                        tm.tm_sec = static_cast<int>(time_val % 100);
                    }
                }

                time_t unix_seconds = timegm(&tm);
                if (unix_seconds == -1) {
                    ARROW_RETURN_NOT_OK(builder.AppendNull());
                }
                else {
                    ARROW_RETURN_NOT_OK(builder.Append(static_cast<int64_t>(unix_seconds)));
                }
            }
        }

        std::shared_ptr<arrow::Array> unix_ts_array;
        ARROW_RETURN_NOT_OK(builder.Finish(&unix_ts_array));

        auto unix_ts_column = std::make_shared<arrow::ChunkedArray>(unix_ts_array);
        auto unix_field = arrow::field(output_column_name, arrow::int64());

        ARROW_ASSIGN_OR_RAISE(auto new_table, table->AddColumn(table->num_columns(), unix_field, unix_ts_column));

        return cpu_df.create_from_cpu_table(new_table);
    }

    arrow::Result<std::vector<AnalyticsDataFrame>> AnalyticsDataFrame::create_rolling_windows(
        int64_t window_size, int64_t step_size) const {

        if (!cpu_table_) {
            return arrow::Status::Invalid("No data available");
        }

        if (window_size <= 0 || step_size <= 0) {
            return arrow::Status::Invalid("Window size and step size must be positive");
        }

        std::vector<AnalyticsDataFrame> windows;
        int64_t num_rows = cpu_table_->num_rows();

        for (int64_t start = 0; start + window_size <= num_rows; start += step_size) {
            auto window_result = slice_by_row_index(start, start + window_size);
            if (!window_result.ok()) {
                return window_result.status();
            }
            windows.push_back(std::move(window_result.ValueOrDie()));
        }

        return windows;
    }

    void AnalyticsDataFrame::set_tssb_metadata(
        const std::string& date_column, const std::string& time_column) {
        tssb_date_column_ = date_column;
        tssb_time_column_ = time_column;
    }

    int64_t AnalyticsDataFrame::num_rows() const {
        return cpu_table_ ? cpu_table_->num_rows() : 0;
    }

    int64_t AnalyticsDataFrame::num_columns() const {
        return cpu_table_ ? cpu_table_->num_columns() : 0;
    }

    std::vector<std::string> AnalyticsDataFrame::column_names() const {
        if (!cpu_table_) {
            return {};
        }

        std::vector<std::string> names;
        auto schema = cpu_table_->schema();
        for (int i = 0; i < schema->num_fields(); ++i) {
            names.push_back(schema->field(i)->name());
        }
        return names;
    }

    bool AnalyticsDataFrame::is_on_gpu() const {
        return location_ == DataLocation::GPU;
    }

    bool AnalyticsDataFrame::has_tssb_metadata() const {
        return tssb_date_column_.has_value() && tssb_time_column_.has_value();
    }

    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::create_from_cpu_table(
        std::shared_ptr<arrow::Table> table) const {
        AnalyticsDataFrame result(table);
        result.schema_ = table->schema();
        result.tssb_date_column_ = tssb_date_column_;
        result.tssb_time_column_ = tssb_time_column_;
        return result;
    }

#ifdef WITH_CUDA
    arrow::Result<AnalyticsDataFrame> AnalyticsDataFrame::create_from_gpu_table(
        std::shared_ptr<cudf::table> table) const {
        AnalyticsDataFrame result;
        result.gpu_table_ = table;
        result.location_ = DataLocation::GPU;
        result.schema_ = schema_;
        result.tssb_date_column_ = tssb_date_column_;
        result.tssb_time_column_ = tssb_time_column_;
        return result;
    }
#endif

} // namespace chronosflow

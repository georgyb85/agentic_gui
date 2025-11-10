#include "feature_utils.h"
#include <arrow/compute/api.h>
#include <arrow/builder.h>
#include <cmath>
#include <algorithm>
#include <numeric>

#ifdef WITH_CUDA
#include <cudf/stats.hpp>
#include <cudf/transform.hpp>
#endif

namespace chronosflow {

arrow::Result<AnalyticsDataFrame> FeatureUtils::select_features_by_correlation(
    const AnalyticsDataFrame& df,
    const std::string& target_column,
    double correlation_threshold,
    bool use_gpu) {

#ifdef WITH_CUDA
    if (use_gpu && df.is_on_gpu()) {
        return select_features_by_correlation_gpu(df, target_column, correlation_threshold);
    }
#endif

    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        return cpu_df_result.status();
    }
    
    const auto& cpu_df = cpu_df_result.ValueOrDie();
    auto column_names = cpu_df.column_names();
    
    auto target_it = std::find(column_names.begin(), column_names.end(), target_column);
    if (target_it == column_names.end()) {
        return arrow::Status::Invalid("Target column not found: ", target_column);
    }
    
    std::vector<std::string> feature_columns;
    for (const auto& name : column_names) {
        if (name != target_column) {
            feature_columns.push_back(name);
        }
    }
    
    auto correlations_result = compute_correlations_cpu(cpu_df, target_column, feature_columns);
    if (!correlations_result.ok()) {
        return correlations_result.status();
    }
    
    auto correlations = correlations_result.ValueOrDie();
    std::vector<std::string> selected_columns;
    selected_columns.push_back(target_column);
    
    for (size_t i = 0; i < feature_columns.size(); ++i) {
        if (std::abs(correlations[i]) >= correlation_threshold) {
            selected_columns.push_back(feature_columns[i]);
        }
    }
    
    return cpu_df.select_columns(selected_columns);
}

arrow::Result<AnalyticsDataFrame> FeatureUtils::normalize_features(
    const AnalyticsDataFrame& df,
    const std::vector<std::string>& feature_columns,
    bool use_gpu) {

#ifdef WITH_CUDA
    if (use_gpu && df.is_on_gpu()) {
        return normalize_features_gpu(df, feature_columns);
    }
#endif

    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        return cpu_df_result.status();
    }
    
    return apply_z_score_normalization_cpu(cpu_df_result.ValueOrDie(), feature_columns);
}

arrow::Result<AnalyticsDataFrame> FeatureUtils::create_rolling_features(
    const AnalyticsDataFrame& df,
    const std::vector<std::string>& feature_columns,
    const std::vector<int>& window_sizes,
    bool use_gpu) {
    
    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        return cpu_df_result.status();
    }
    
    const auto& cpu_df = cpu_df_result.ValueOrDie();
    auto table = cpu_df.get_cpu_table();
    if (!table) {
        return arrow::Status::Invalid("No table data available");
    }
    
    std::vector<std::shared_ptr<arrow::ChunkedArray>> new_columns;
    std::vector<std::shared_ptr<arrow::Field>> new_fields;
    
    auto schema = table->schema();
    for (int i = 0; i < schema->num_fields(); ++i) {
        new_columns.push_back(table->column(i));
        new_fields.push_back(schema->field(i));
    }
    
    // Use Arrow's native rolling window functions
    for (const auto& column_name : feature_columns) {
        auto column_result = table->GetColumnByName(column_name);
        if (!column_result) {
            continue;
        }
        
        auto column = column_result->chunk(0);
        
        for (int window_size : window_sizes) {
            // PERFORMANCE WARNING: The following is a manual, unoptimized implementation
            // of a rolling window due to limitations in Arrow 21.0.0. It has O(N*W)
            // complexity and is extremely slow on large datasets. For production use,
            // upgrade Arrow or implement an optimized sliding-window algorithm.
            {
                arrow::DoubleBuilder mean_builder;
                
                for (int64_t i = 0; i < column->length(); ++i) {
                    if (i < window_size - 1) {
                        ARROW_RETURN_NOT_OK(mean_builder.AppendNull());
                    } else {
                        auto window_slice = column->Slice(i - window_size + 1, window_size);
                        auto mean_result = arrow::compute::CallFunction("mean", std::vector<arrow::Datum>{window_slice});
                        
                        if (mean_result.ok() && mean_result.ValueOrDie().scalar()->is_valid) {
                            auto scalar_val = std::static_pointer_cast<arrow::DoubleScalar>(mean_result.ValueOrDie().scalar());
                            ARROW_RETURN_NOT_OK(mean_builder.Append(scalar_val->value));
                        } else {
                            ARROW_RETURN_NOT_OK(mean_builder.AppendNull());
                        }
                    }
                }
                
                std::shared_ptr<arrow::Array> mean_array;
                ARROW_RETURN_NOT_OK(mean_builder.Finish(&mean_array));
                
                std::string mean_name = column_name + "_rolling_mean_" + std::to_string(window_size);
                new_columns.push_back(std::make_shared<arrow::ChunkedArray>(mean_array));
                new_fields.push_back(arrow::field(mean_name, arrow::float64()));
            }
            
            // DIAGNOSTIC: Skip rolling stddev since rolling functions don't exist in Arrow 21.0.0
        }
    }
    
    auto new_schema = arrow::schema(new_fields);
    auto new_table = arrow::Table::Make(new_schema, new_columns);
    
    return AnalyticsDataFrame(new_table);
}

arrow::Result<std::vector<std::string>> FeatureUtils::select_top_features(
    const AnalyticsDataFrame& df,
    const std::string& target_column,
    int top_k,
    bool use_gpu) {
    
    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        return cpu_df_result.status();
    }
    
    const auto& cpu_df = cpu_df_result.ValueOrDie();
    auto column_names = cpu_df.column_names();
    
    std::vector<std::string> feature_columns;
    for (const auto& name : column_names) {
        if (name != target_column) {
            feature_columns.push_back(name);
        }
    }
    
    auto correlations_result = compute_correlations_cpu(cpu_df, target_column, feature_columns);
    if (!correlations_result.ok()) {
        return correlations_result.status();
    }
    
    auto correlations = correlations_result.ValueOrDie();
    
    std::vector<std::pair<double, std::string>> corr_pairs;
    for (size_t i = 0; i < feature_columns.size(); ++i) {
        corr_pairs.emplace_back(std::abs(correlations[i]), feature_columns[i]);
    }
    
    std::sort(corr_pairs.rbegin(), corr_pairs.rend());
    
    std::vector<std::string> top_features;
    for (int i = 0; i < std::min(top_k, static_cast<int>(corr_pairs.size())); ++i) {
        top_features.push_back(corr_pairs[i].second);
    }
    
    return top_features;
}

arrow::Result<AnalyticsDataFrame> FeatureUtils::remove_outliers(
    const AnalyticsDataFrame& df,
    const std::vector<std::string>& feature_columns,
    double z_score_threshold,
    bool use_gpu) {
    
    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        return cpu_df_result.status();
    }
    
    const auto& cpu_df = cpu_df_result.ValueOrDie();
    auto table = cpu_df.get_cpu_table();
    if (!table) {
        return arrow::Status::Invalid("No table data available");
    }
    
    // Use Arrow Compute for outlier detection
    std::vector<std::shared_ptr<arrow::Array>> filter_arrays;
    
    for (const auto& column_name : feature_columns) {
        auto column_result = table->GetColumnByName(column_name);
        if (!column_result) {
            continue;
        }
        
        auto column = column_result->chunk(0);
        
        // Compute mean and stddev using Arrow Compute
        auto mean_result = arrow::compute::CallFunction("mean", std::vector<arrow::Datum>{column});
        if (!mean_result.ok()) {
            return mean_result.status();
        }
        
        auto stddev_result = arrow::compute::CallFunction("stddev", std::vector<arrow::Datum>{column});
        if (!stddev_result.ok()) {
            return stddev_result.status();
        }
        
        auto mean_scalar = mean_result.ValueOrDie().scalar();
        auto stddev_scalar = stddev_result.ValueOrDie().scalar();
        
        if (!mean_scalar->is_valid || !stddev_scalar->is_valid) {
            continue;
        }
        
        // Compute z-scores using Arrow Compute
        auto mean_broadcast = arrow::compute::CallFunction("subtract", std::vector<arrow::Datum>{column, mean_scalar});
        if (!mean_broadcast.ok()) {
            return mean_broadcast.status();
        }
        
        auto z_scores = arrow::compute::CallFunction("divide", std::vector<arrow::Datum>{mean_broadcast.ValueOrDie().make_array(), stddev_scalar});
        if (!z_scores.ok()) {
            return z_scores.status();
        }
        
        auto abs_z_scores = arrow::compute::CallFunction("abs", std::vector<arrow::Datum>{z_scores.ValueOrDie().make_array()});
        if (!abs_z_scores.ok()) {
            return abs_z_scores.status();
        }
        
        auto threshold_scalar = arrow::MakeScalar(z_score_threshold);
        auto not_outlier = arrow::compute::CallFunction("less_equal", std::vector<arrow::Datum>{abs_z_scores.ValueOrDie().make_array(), threshold_scalar});
        if (!not_outlier.ok()) {
            return not_outlier.status();
        }
        
        filter_arrays.push_back(not_outlier.ValueOrDie().make_array());
    }
    
    // Combine all filter conditions with AND
    std::shared_ptr<arrow::Array> combined_filter = filter_arrays[0];
    for (size_t i = 1; i < filter_arrays.size(); ++i) {
        auto and_result = arrow::compute::CallFunction("and", std::vector<arrow::Datum>{combined_filter, filter_arrays[i]});
        if (!and_result.ok()) {
            return and_result.status();
        }
        combined_filter = and_result.ValueOrDie().make_array();
    }
    
    auto filtered_result = arrow::compute::Filter(table, combined_filter);
    if (!filtered_result.ok()) {
        return filtered_result.status();
    }
    
    auto filtered_table = filtered_result.ValueOrDie().table();
    return AnalyticsDataFrame(filtered_table);
}

#ifdef WITH_CUDA
arrow::Result<AnalyticsDataFrame> FeatureUtils::select_features_by_correlation_gpu(
    const AnalyticsDataFrame& df,
    const std::string& target_column,
    double correlation_threshold) {
    
    if (!df.is_on_gpu()) {
        return arrow::Status::Invalid("DataFrame must be on GPU for GPU correlation");
    }
    
    // For GPU correlation, we would use cuDF's statistical functions
    // This is a simplified implementation - full implementation would require
    // extensive cuDF statistical operations
    
    // Convert to CPU for correlation computation since cuDF correlation
    // functions are more complex to implement directly
    auto cpu_result = df.to_cpu();
    if (!cpu_result.ok()) {
        return cpu_result.status();
    }
    
    const auto& cpu_df = cpu_result.ValueOrDie();
    auto correlation_result = select_features_by_correlation(
        cpu_df, target_column, correlation_threshold, false);
    
    if (!correlation_result.ok()) {
        return correlation_result.status();
    }
    
    // Transfer result back to GPU
    return correlation_result.ValueOrDie().to_gpu();
}

arrow::Result<AnalyticsDataFrame> FeatureUtils::normalize_features_gpu(
    const AnalyticsDataFrame& df,
    const std::vector<std::string>& feature_columns) {
    
    if (!df.is_on_gpu()) {
        return arrow::Status::Invalid("DataFrame must be on GPU for GPU normalization");
    }
    
    auto gpu_table = df.gpu_table_;
    if (!gpu_table || !df.cpu_table_) {
        return arrow::Status::Invalid("GPU table or schema not available");
    }
    
    // Implement GPU normalization using cuDF operations
    std::vector<std::unique_ptr<cudf::column>> normalized_columns;
    auto schema = df.cpu_table_->schema();
    
    for (int i = 0; i < gpu_table->num_columns(); ++i) {
        std::string column_name = schema->field(i)->name();
        
        if (std::find(feature_columns.begin(), feature_columns.end(), column_name) 
            != feature_columns.end()) {
            
            auto column = gpu_table->get_column(i);
            
            // Compute mean and standard deviation using cuDF
            auto mean_result = cudf::reduce(
                column, 
                cudf::make_mean_aggregation<cudf::reduce_aggregation>(),
                cudf::data_type{cudf::type_id::FLOAT64}
            );
            
            auto stddev_result = cudf::reduce(
                column,
                cudf::make_std_aggregation<cudf::reduce_aggregation>(),
                cudf::data_type{cudf::type_id::FLOAT64}
            );
            
            if (!mean_result->is_valid() || !stddev_result->is_valid()) {
                // If statistics computation fails, keep original column
                normalized_columns.push_back(std::make_unique<cudf::column>(column));
                continue;
            }
            
            auto mean_scalar = static_cast<cudf::numeric_scalar<double>*>(mean_result.get());
            auto stddev_scalar = static_cast<cudf::numeric_scalar<double>*>(stddev_result.get());
            
            if (stddev_scalar->value() == 0.0) {
                // Avoid division by zero, keep original column
                normalized_columns.push_back(std::make_unique<cudf::column>(column));
                continue;
            }
            
            // Compute (column - mean) / stddev using cuDF binary operations
            auto column_float = cudf::cast(column, cudf::data_type{cudf::type_id::FLOAT64});
            
            // Subtract mean
            auto centered = cudf::binary_operation(
                column_float->view(),
                *mean_scalar,
                cudf::binary_operator::SUB,
                cudf::data_type{cudf::type_id::FLOAT64}
            );
            
            // Divide by standard deviation
            auto normalized = cudf::binary_operation(
                centered->view(),
                *stddev_scalar,
                cudf::binary_operator::DIV,
                cudf::data_type{cudf::type_id::FLOAT64}
            );
            
            normalized_columns.push_back(std::move(normalized));
        } else {
            // Keep non-feature columns unchanged
            normalized_columns.push_back(std::make_unique<cudf::column>(column));
        }
    }
    
    // Create new GPU table with normalized columns
    auto normalized_gpu_table = std::make_unique<cudf::table>(std::move(normalized_columns));
    
    AnalyticsDataFrame result;
    result.gpu_table_ = std::shared_ptr<cudf::table>(normalized_gpu_table.release());
    result.location_ = DataLocation::GPU;
    result.tssb_date_column_ = df.tssb_date_column_;
    result.tssb_time_column_ = df.tssb_time_column_;
    
    // Create minimal CPU table for schema reference
    std::vector<std::shared_ptr<arrow::ChunkedArray>> empty_columns;
    for (int i = 0; i < schema->num_fields(); ++i) {
        auto field_type = (std::find(feature_columns.begin(), feature_columns.end(), 
                                   schema->field(i)->name()) != feature_columns.end()) 
                         ? arrow::float64() : schema->field(i)->type();
        empty_columns.push_back(std::make_shared<arrow::ChunkedArray>(
            arrow::MakeEmptyArray(field_type)));
    }
    
    std::vector<std::shared_ptr<arrow::Field>> updated_fields;
    for (int i = 0; i < schema->num_fields(); ++i) {
        auto field_type = (std::find(feature_columns.begin(), feature_columns.end(), 
                                   schema->field(i)->name()) != feature_columns.end()) 
                         ? arrow::float64() : schema->field(i)->type();
        updated_fields.push_back(arrow::field(schema->field(i)->name(), field_type));
    }
    
    auto updated_schema = arrow::schema(updated_fields);
    result.cpu_table_ = arrow::Table::Make(updated_schema, empty_columns, 0);
    
    return result;
}
#endif

arrow::Result<std::vector<double>> FeatureUtils::compute_correlations_cpu(
    const AnalyticsDataFrame& df,
    const std::string& target_column,
    const std::vector<std::string>& feature_columns) {
    
    auto table = df.get_cpu_table();
    if (!table) {
        return arrow::Status::Invalid("No table data available");
    }
    
    auto target_column_data = table->GetColumnByName(target_column);
    if (!target_column_data) {
        return arrow::Status::Invalid("Target column not found: ", target_column);
    }
    
    // --- PERFORMANCE OPTIMIZATION ---
    // Pre-compute target mean and centered values once, outside the loop.
    auto target_mean_res = arrow::compute::CallFunction("mean", {arrow::Datum(target_column_data)});
    if (!target_mean_res.ok()) return target_mean_res.status();
    auto target_mean = target_mean_res.ValueOrDie().scalar();

    auto target_centered_res = arrow::compute::CallFunction("subtract", {arrow::Datum(target_column_data), target_mean});
    if (!target_centered_res.ok()) return target_centered_res.status();
    auto target_centered = target_centered_res.ValueOrDie();

    auto target_squared = arrow::compute::CallFunction("multiply", {target_centered, target_centered});
    if (!target_squared.ok()) return target_squared.status();
        
    auto target_variance_res = arrow::compute::CallFunction("mean", {target_squared.ValueOrDie()});
    if (!target_variance_res.ok()) return target_variance_res.status();
    double target_var = std::static_pointer_cast<arrow::DoubleScalar>(target_variance_res.ValueOrDie().scalar())->value;
    // --- END OPTIMIZATION ---

    std::vector<double> correlations;
    for (const auto& feature_name : feature_columns) {
        auto feature_column_data = table->GetColumnByName(feature_name);
        if (!feature_column_data) {
            correlations.push_back(0.0);
            continue;
        }
        
        // Use the pre-computed target values
        auto feature_mean_res = arrow::compute::CallFunction("mean", {arrow::Datum(feature_column_data)});
        if (!feature_mean_res.ok()) {
            correlations.push_back(0.0);
            continue;
        }
        auto feature_mean = feature_mean_res.ValueOrDie().scalar();
        
        auto feature_centered_res = arrow::compute::CallFunction("subtract", {arrow::Datum(feature_column_data), feature_mean});
        if (!feature_centered_res.ok()) {
            correlations.push_back(0.0);
            continue;
        }
        auto feature_centered = feature_centered_res.ValueOrDie();
        
        auto product_res = arrow::compute::CallFunction("multiply", {target_centered, feature_centered});
        if (!product_res.ok()) {
            correlations.push_back(0.0);
            continue;
        }
        
        auto covariance_res = arrow::compute::CallFunction("mean", {product_res.ValueOrDie()});
        if (!covariance_res.ok()) {
            correlations.push_back(0.0);
            continue;
        }
        double cov_val = std::static_pointer_cast<arrow::DoubleScalar>(covariance_res.ValueOrDie().scalar())->value;
        
        auto feature_squared = arrow::compute::CallFunction("multiply", {feature_centered, feature_centered});
        if (!feature_squared.ok()) {
            correlations.push_back(0.0);
            continue;
        }
        auto feature_variance_res = arrow::compute::CallFunction("mean", {feature_squared.ValueOrDie()});
        if (!feature_variance_res.ok()) {
            correlations.push_back(0.0);
            continue;
        }
        double feature_var = std::static_pointer_cast<arrow::DoubleScalar>(feature_variance_res.ValueOrDie().scalar())->value;
        
        double correlation = 0.0;
        if (target_var > 0 && feature_var > 0) {
            correlation = cov_val / std::sqrt(target_var * feature_var);
        }
        correlations.push_back(correlation);
    }
    
    return correlations;
}

arrow::Result<AnalyticsDataFrame> FeatureUtils::apply_z_score_normalization_cpu(
    const AnalyticsDataFrame& df,
    const std::vector<std::string>& feature_columns) {
    
    auto table = df.get_cpu_table();
    if (!table) {
        return arrow::Status::Invalid("No table data available");
    }
    
    std::vector<std::shared_ptr<arrow::ChunkedArray>> new_columns;
    std::vector<std::shared_ptr<arrow::Field>> new_fields;
    
    auto schema = table->schema();
    
    for (int i = 0; i < schema->num_fields(); ++i) {
        std::string column_name = schema->field(i)->name();
        
        if (std::find(feature_columns.begin(), feature_columns.end(), column_name) 
            != feature_columns.end()) {
            
            auto column = table->column(i)->chunk(0);
            
            // Use Arrow Compute for normalization
            auto mean_result = arrow::compute::CallFunction("mean", std::vector<arrow::Datum>{column});
            auto stddev_result = arrow::compute::CallFunction("stddev", std::vector<arrow::Datum>{column});
            
            if (!mean_result.ok() || !stddev_result.ok()) {
                new_columns.push_back(table->column(i));
                new_fields.push_back(schema->field(i));
                continue;
            }
            
            auto mean_scalar = mean_result.ValueOrDie().scalar();
            auto stddev_scalar = stddev_result.ValueOrDie().scalar();
            
            if (!mean_scalar->is_valid || !stddev_scalar->is_valid) {
                new_columns.push_back(table->column(i));
                new_fields.push_back(schema->field(i));
                continue;
            }
            
            auto stddev_val = std::static_pointer_cast<arrow::DoubleScalar>(stddev_scalar)->value;
            if (stddev_val == 0.0) {
                new_columns.push_back(table->column(i));
                new_fields.push_back(schema->field(i));
                continue;
            }
            
            // Compute (x - mean) / stddev using Arrow Compute
            auto centered = arrow::compute::CallFunction("subtract", std::vector<arrow::Datum>{column, mean_scalar});
            if (!centered.ok()) {
                new_columns.push_back(table->column(i));
                new_fields.push_back(schema->field(i));
                continue;
            }
            
            auto normalized = arrow::compute::CallFunction("divide", std::vector<arrow::Datum>{centered.ValueOrDie().make_array(), stddev_scalar});
            if (!normalized.ok()) {
                new_columns.push_back(table->column(i));
                new_fields.push_back(schema->field(i));
                continue;
            }
            
            new_columns.push_back(std::make_shared<arrow::ChunkedArray>(normalized.ValueOrDie().make_array()));
            new_fields.push_back(arrow::field(column_name, arrow::float64()));
        } else {
            new_columns.push_back(table->column(i));
            new_fields.push_back(schema->field(i));
        }
    }
    
    auto new_schema = arrow::schema(new_fields);
    auto new_table = arrow::Table::Make(new_schema, new_columns);
    
    return AnalyticsDataFrame(new_table);
}

arrow::Result<std::vector<double>> FeatureUtils::compute_column_statistics(
    const AnalyticsDataFrame& df,
    const std::string& column_name) {
    
    auto table = df.get_cpu_table();
    if (!table) {
        return arrow::Status::Invalid("No table data available");
    }
    
    auto column_result = table->GetColumnByName(column_name);
    if (!column_result) {
        return arrow::Status::Invalid("Column not found: ", column_name);
    }
    
    auto column = column_result->chunk(0);
    
    // Use Arrow Compute for statistics
    auto mean_result = arrow::compute::CallFunction("mean", std::vector<arrow::Datum>{column});
    auto stddev_result = arrow::compute::CallFunction("stddev", std::vector<arrow::Datum>{column});
    
    if (!mean_result.ok() || !stddev_result.ok()) {
        return std::vector<double>{0.0, 0.0};
    }
    
    auto mean_scalar = mean_result.ValueOrDie().scalar();
    auto stddev_scalar = stddev_result.ValueOrDie().scalar();
    
    if (!mean_scalar->is_valid || !stddev_scalar->is_valid) {
        return std::vector<double>{0.0, 0.0};
    }
    
    double mean = std::static_pointer_cast<arrow::DoubleScalar>(mean_scalar)->value;
    double stddev = std::static_pointer_cast<arrow::DoubleScalar>(stddev_scalar)->value;
    
    return std::vector<double>{mean, stddev};
}

} // namespace chronosflow
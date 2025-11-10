#pragma once

#include "analytics_dataframe.h"
#include <arrow/result.h>
#include <vector>
#include <string>

namespace chronosflow {

class FeatureUtils {
public:
    static arrow::Result<AnalyticsDataFrame> select_features_by_correlation(
        const AnalyticsDataFrame& df,
        const std::string& target_column,
        double correlation_threshold = 0.1,
        bool use_gpu = false);

    static arrow::Result<AnalyticsDataFrame> normalize_features(
        const AnalyticsDataFrame& df,
        const std::vector<std::string>& feature_columns,
        bool use_gpu = false);

    static arrow::Result<AnalyticsDataFrame> create_rolling_features(
        const AnalyticsDataFrame& df,
        const std::vector<std::string>& feature_columns,
        const std::vector<int>& window_sizes = {5, 10, 20},
        bool use_gpu = false);

    static arrow::Result<std::vector<std::string>> select_top_features(
        const AnalyticsDataFrame& df,
        const std::string& target_column,
        int top_k = 10,
        bool use_gpu = false);

    static arrow::Result<AnalyticsDataFrame> remove_outliers(
        const AnalyticsDataFrame& df,
        const std::vector<std::string>& feature_columns,
        double z_score_threshold = 3.0,
        bool use_gpu = false);

#ifdef WITH_CUDA
    static arrow::Result<AnalyticsDataFrame> select_features_by_correlation_gpu(
        const AnalyticsDataFrame& df,
        const std::string& target_column,
        double correlation_threshold = 0.1);

    static arrow::Result<AnalyticsDataFrame> normalize_features_gpu(
        const AnalyticsDataFrame& df,
        const std::vector<std::string>& feature_columns);
#endif

private:
    static arrow::Result<std::vector<double>> compute_correlations_cpu(
        const AnalyticsDataFrame& df,
        const std::string& target_column,
        const std::vector<std::string>& feature_columns);

    static arrow::Result<AnalyticsDataFrame> apply_z_score_normalization_cpu(
        const AnalyticsDataFrame& df,
        const std::vector<std::string>& feature_columns);

    static arrow::Result<std::vector<double>> compute_column_statistics(
        const AnalyticsDataFrame& df,
        const std::string& column_name);
};

} // namespace chronosflow
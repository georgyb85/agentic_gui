#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <json/json.h>

namespace stage1 {

struct DatasetSummary {
    std::string dataset_id;
    std::string dataset_slug;
    std::string symbol;
    std::string granularity;
    std::string source;
    std::string ohlcv_measurement;
    std::string indicator_measurement;
    int64_t bar_interval_ms = 0;
    int64_t lookback_rows = 0;
    int64_t ohlcv_row_count = 0;
    int64_t indicator_row_count = 0;
    int64_t first_ohlcv_ts_ms = 0;
    int64_t first_indicator_ts_ms = 0;
    std::string ohlcv_first_ts;
    std::string ohlcv_last_ts;
    std::string indicator_first_ts;
    std::string indicator_last_ts;
    int64_t run_count = 0;
    int64_t simulation_count = 0;
    std::string updated_at;
};

struct RunSummary {
    std::string run_id;
    std::string dataset_id;
    std::string dataset_slug;
    std::string prediction_measurement;
    std::string status;
    std::string started_at;
    std::string completed_at;
};

struct FoldDetail {
    int fold_number = 0;
    int train_start = 0;
    int train_end = 0;
    int test_start = 0;
    int test_end = 0;
    int samples_train = 0;
    int samples_test = 0;
    double hit_rate = 0.0;
    double short_hit_rate = 0.0;
    double profit_factor_test = 0.0;
    double long_threshold = 0.0;
    double short_threshold = 0.0;
    std::string thresholds_json;
    std::string metrics_json;
};

struct RunDetail {
    std::string run_id;
    std::string dataset_id;
    std::string dataset_slug;
    std::string prediction_measurement;
    std::string target_column;
    std::vector<std::string> feature_columns;
    std::string hyperparameters_json;
    std::string walk_config_json;
    std::string summary_metrics_json;
    std::string status;
    std::string started_at;
    std::string completed_at;
    std::vector<FoldDetail> folds;
};

struct JobStatus {
    std::string job_id;
    std::string job_type;
    std::string status;
    int64_t progress = 0;
    int64_t total = 0;
    std::string message;
    std::string error;
    std::string payload;
    std::string result;
    std::string created_at;
    std::string updated_at;
    std::string started_at;
    std::string completed_at;
};

struct MeasurementInfo {
    std::string name;
    std::string designated_timestamp;
    std::string partition_by;
    int64_t row_count = 0;
    std::string first_ts;
    std::string last_ts;
};

class RestClient {
public:
    enum class AppendTarget {
        Ohlcv,
        Indicators
    };

    static RestClient& Instance();
    ~RestClient();

    void SetBaseUrl(const std::string& url);
    void SetApiToken(const std::string& token);
    const std::string& GetBaseUrl() const { return m_baseUrl; }
    const std::string& GetApiToken() const { return m_apiToken; }

    bool FetchDatasets(int limit,
                       int offset,
                       std::vector<DatasetSummary>* datasets,
                       std::string* error);

    bool FetchDataset(const std::string& datasetId,
                      DatasetSummary* summary,
                      std::string* error);

    bool FetchDatasetRuns(const std::string& datasetId,
                          int limit,
                          int offset,
                          std::vector<RunSummary>* runs,
                          std::string* error);

    bool FetchRunDetail(const std::string& runId,
                        RunDetail* detail,
                        std::string* error);

    bool SubmitQuestDbImport(const std::string& measurement,
                             const std::string& csvData,
                             const std::string& filenameHint,
                             std::string* jobId,
                             std::string* error);

    bool GetJobStatus(const std::string& jobId,
                      JobStatus* status,
                      std::string* error);

    bool PostJson(const std::string& path,
                  const std::string& body,
                  long* httpStatus,
                  std::string* responseBody,
                  std::string* error);

    bool QuestDbQuery(const std::string& sql,
                      std::vector<std::string>* columns,
                      std::vector<std::vector<std::string>>* rows,
                      std::string* error);

    bool AppendDatasetRows(const std::string& datasetId,
                           const Json::Value& payload,
                           AppendTarget target,
                           std::string* error);

    bool CreateOrUpdateDataset(const std::string& datasetId,
                               const std::string& datasetSlug,
                               const std::string& granularity,
                               int64_t barIntervalMs,
                               int64_t lookbackRows,
                               int64_t firstOhlcvTs,
                               int64_t firstIndicatorTs,
                               const std::string& metadataJson,
                               std::string* error);

    bool ListMeasurements(const std::string& prefix,
                          std::vector<MeasurementInfo>* measurements,
                          std::string* error);

    bool FetchJobs(int limit,
                   int offset,
                   std::vector<JobStatus>* jobs,
                   std::string* error);

    bool GetHealth(std::string* payload,
                   std::string* error);

private:
    RestClient();
    bool Execute(const std::string& method,
                 const std::string& path,
                 const std::string& body,
                 const std::vector<std::string>& extraHeaders,
                 long* httpStatus,
                 std::string* responseBody,
                 std::string* error);

    bool ParseDatasets(const std::string& json,
                       std::vector<DatasetSummary>* datasets,
                       std::string* error);
    bool ParseDatasetRuns(const std::string& json,
                          std::vector<RunSummary>* runs,
                          std::string* error);
    bool ParseRunDetail(const std::string& json,
                        RunDetail* detail,
                        std::string* error);
    bool ParseJob(const std::string& json,
                  JobStatus* status,
                  std::string* error);

    std::string m_baseUrl;
    std::string m_apiToken;
};

} // namespace stage1

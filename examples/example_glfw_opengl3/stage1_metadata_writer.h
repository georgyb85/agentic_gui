#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>

#include "TradeSimulator.h"
namespace simulation {
struct SimulationRun;
struct FoldResult;
} // namespace simulation

// Lightweight helper that records Stage 1 metadata inserts so the frontend
// Postgres instance can be hydrated once connectivity is available.
// The implementation appends idempotent INSERT ... ON CONFLICT statements to
// docs/fixtures/stage1_3/pending_postgres_inserts.sql.
class Stage1MetadataWriter {
public:
    enum class PersistMode {
        DatabaseAndFile,
        DatabaseOnly,
        FileOnly
    };

    struct DatasetRecord {
        std::string dataset_id;  // UUID formatted string
        std::string dataset_slug;
        std::string symbol;
        std::string granularity;
        std::string source;
        std::string ohlcv_measurement;
        std::string indicator_measurement;
        std::int64_t ohlcv_row_count = 0;
        std::int64_t indicator_row_count = 0;
        std::optional<std::int64_t> ohlcv_first_timestamp_unix;
        std::optional<std::int64_t> ohlcv_last_timestamp_unix;
        std::optional<std::int64_t> indicator_first_timestamp_unix;
        std::optional<std::int64_t> indicator_last_timestamp_unix;
        std::string metadata_json;
        std::chrono::system_clock::time_point created_at;
    };

    struct WalkforwardFoldRecord {
        int fold_number = 0;
        int train_start = 0;
        int train_end = 0;
        int test_start = 0;
        int test_end = 0;
        int samples_train = 0;
        int samples_test = 0;
        std::optional<int> best_iteration;
        std::optional<float> best_score;
        float hit_rate = 0.0f;
        float profit_factor_test = 0.0f;
        float long_threshold_optimal = 0.0f;
        float short_threshold_optimal = 0.0f;
        float prediction_threshold_scaled = 0.0f;
        float prediction_threshold_original = 0.0f;
        float dynamic_positive_threshold = 0.0f;
        float short_threshold_scaled = 0.0f;
        float short_threshold_original = 0.0f;
        float long_threshold_95th = 0.0f;
        float short_threshold_5th = 0.0f;
        int n_signals = 0;
        int n_short_signals = 0;
        float signal_sum = 0.0f;
        float short_signal_sum = 0.0f;
        float signal_rate = 0.0f;
        float short_signal_rate = 0.0f;
        float avg_return_on_signals = 0.0f;
        float median_return_on_signals = 0.0f;
        float std_return_on_signals = 0.0f;
        float avg_return_on_short_signals = 0.0f;
        float avg_predicted_return_on_signals = 0.0f;
        float short_hit_rate = 0.0f;
        float running_sum = 0.0f;
        float running_sum_short = 0.0f;
        float running_sum_dual = 0.0f;
        float sum_wins = 0.0f;
        float sum_losses = 0.0f;
        float sum_short_wins = 0.0f;
        float sum_short_losses = 0.0f;
        float profit_factor_train = 0.0f;
        float profit_factor_short_train = 0.0f;
        float profit_factor_short_test = 0.0f;
        bool model_learned_nothing = false;
        bool used_cached_model = false;
    };

    struct WalkforwardRecord {
        std::string run_id;      // UUID formatted string
        std::string dataset_id;  // UUID formatted string
        std::string prediction_measurement;
        std::string target_column;
        std::vector<std::string> feature_columns;
        std::string hyperparameters_json;
        std::string walk_config_json;
        std::string summary_metrics_json;
        std::string status;
        std::string requested_by;
        std::chrono::system_clock::time_point started_at;
        std::chrono::system_clock::time_point completed_at;
        std::int64_t duration_ms = 0;
        std::vector<WalkforwardFoldRecord> folds;
    };

    struct SimulationBucketRecord {
        std::string side;
        std::int64_t trade_count = 0;
        std::int64_t win_count = 0;
        double profit_factor = 0.0;
        double avg_return_pct = 0.0;
        double max_drawdown_pct = 0.0;
        std::string notes;
    };

    struct SimulationRecord {
        std::string simulation_id;    // UUID formatted string
        std::string run_id;            // UUID formatted string
        std::string dataset_id;        // UUID formatted string
        std::string input_run_measurement;
        std::string questdb_namespace;
        std::string mode;
        std::string config_json;
        std::string summary_metrics_json;
        std::chrono::system_clock::time_point started_at;
        std::chrono::system_clock::time_point completed_at;
        std::string status;
        std::vector<SimulationBucketRecord> buckets;
    };

    static Stage1MetadataWriter& Instance();

    static std::string MakeDeterministicUuid(const std::string& seed);

    void RecordDatasetExport(const DatasetRecord& record, PersistMode mode = PersistMode::DatabaseAndFile);
    bool RecordWalkforwardRun(const WalkforwardRecord& record,
                              std::string* error = nullptr,
                              PersistMode mode = PersistMode::DatabaseAndFile);
    void RecordSimulationRun(
        const SimulationRecord& record,
        const std::vector<ExecutedTrade>& trades,
        PersistMode mode = PersistMode::DatabaseAndFile);

private:
    Stage1MetadataWriter();
    void AppendSql(const std::string& sql, PersistMode mode);
    static std::string EscapeSql(const std::string& value);
    static std::string Quote(const std::string& value);
    static std::string ToTimestampLiteral(std::int64_t unix_seconds);
    static std::string ToTimestampLiteral(std::chrono::system_clock::time_point tp);
    static std::string JsonEscape(const std::string& value);
    static std::string ToJsonArray(const std::vector<std::string>& values);
    static std::string CurrentUsername();

    std::string m_spoolPath;
};

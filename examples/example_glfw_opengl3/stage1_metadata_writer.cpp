#include "stage1_metadata_writer.h"
#include "Stage1RestClient.h"
#include "TradeSimulator.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <cstdlib>
#include <array>
#include <functional>
#include <cmath>
#include <ctime>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace {
std::mutex g_writerMutex;
const char* kSpoolPath = "docs/fixtures/stage1_3/pending_postgres_inserts.sql";

double SafeDouble(double value) {
    return std::isfinite(value) ? value : 0.0;
}

int64_t SafeTimestampMillis(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int64_t>(std::llround(value));
}

std::string ToIso8601(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

void WriteJsonObjectOrEmpty(rapidjson::Writer<rapidjson::StringBuffer>& writer,
                            const std::string& json) {
    if (json.empty()) {
        writer.StartObject();
        writer.EndObject();
        return;
    }
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || (!doc.IsObject() && !doc.IsArray())) {
        writer.StartObject();
        writer.EndObject();
        return;
    }
    doc.Accept(writer);
}

std::string BuildDatasetJson(const Stage1MetadataWriter::DatasetRecord& record) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    const std::string indicatorMeasurement = record.indicator_measurement.empty()
        ? record.dataset_slug
        : record.indicator_measurement;

    writer.StartObject();
    writer.Key("dataset_id");
    writer.String(record.dataset_id.c_str());
    writer.Key("dataset_slug");
    writer.String(record.dataset_slug.c_str());
    writer.Key("symbol");
    writer.String(record.symbol.c_str());
    writer.Key("granularity");
    writer.String(record.granularity.c_str());
    writer.Key("source");
    writer.String(record.source.c_str());
    writer.Key("indicator_measurement");
    writer.String(indicatorMeasurement.c_str());
    if (!record.ohlcv_measurement.empty()) {
        writer.Key("ohlcv_measurement");
        writer.String(record.ohlcv_measurement.c_str());
    }
    if (record.ohlcv_row_count > 0) {
        writer.Key("ohlcv_row_count");
        writer.Int64(record.ohlcv_row_count);
    }
    if (record.indicator_row_count > 0) {
        writer.Key("indicator_row_count");
        writer.Int64(record.indicator_row_count);
    }
    writer.Key("metadata");
    WriteJsonObjectOrEmpty(writer, record.metadata_json);
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
}

void WriteFoldJson(rapidjson::Writer<rapidjson::StringBuffer>& writer,
                   const Stage1MetadataWriter::WalkforwardFoldRecord& fold,
                   const std::string& run_id) {
    writer.StartObject();
    writer.Key("run_id");
    writer.String(run_id.c_str());
    writer.Key("fold_number");
    writer.Int(fold.fold_number);
    writer.Key("train_start_idx");
    writer.Int64(fold.train_start);
    writer.Key("train_end_idx");
    writer.Int64(fold.train_end);
    writer.Key("test_start_idx");
    writer.Int64(fold.test_start);
    writer.Key("test_end_idx");
    writer.Int64(fold.test_end);
    writer.Key("samples_train");
    writer.Int64(fold.samples_train);
    writer.Key("samples_test");
    writer.Int64(fold.samples_test);
    if (fold.best_iteration.has_value()) {
        writer.Key("best_iteration");
        writer.Int(*fold.best_iteration);
    }
    if (fold.best_score.has_value()) {
        writer.Key("best_score");
        writer.Double(SafeDouble(*fold.best_score));
    }
    writer.Key("thresholds");
    writer.StartObject();
    writer.Key("long_optimal");
    writer.Double(SafeDouble(fold.long_threshold_optimal));
    writer.Key("short_optimal");
    writer.Double(SafeDouble(fold.short_threshold_optimal));
    writer.Key("prediction_scaled");
    writer.Double(SafeDouble(fold.prediction_threshold_scaled));
    writer.Key("prediction_original");
    writer.Double(SafeDouble(fold.prediction_threshold_original));
    writer.Key("dynamic_positive");
    writer.Double(SafeDouble(fold.dynamic_positive_threshold));
    writer.Key("short_scaled");
    writer.Double(SafeDouble(fold.short_threshold_scaled));
    writer.Key("short_original");
    writer.Double(SafeDouble(fold.short_threshold_original));
    writer.Key("long_percentile");
    writer.Double(SafeDouble(fold.long_threshold_95th));
    writer.Key("short_percentile");
    writer.Double(SafeDouble(fold.short_threshold_5th));
    writer.EndObject();

    writer.Key("metrics");
    writer.StartObject();
    writer.Key("hit_rate");
    writer.Double(SafeDouble(fold.hit_rate));
    writer.Key("short_hit_rate");
    writer.Double(SafeDouble(fold.short_hit_rate));
    writer.Key("profit_factor_test");
    writer.Double(SafeDouble(fold.profit_factor_test));
    writer.Key("profit_factor_train");
    writer.Double(SafeDouble(fold.profit_factor_train));
    writer.Key("profit_factor_short_train");
    writer.Double(SafeDouble(fold.profit_factor_short_train));
    writer.Key("profit_factor_short_test");
    writer.Double(SafeDouble(fold.profit_factor_short_test));
    writer.Key("n_signals");
    writer.Int(fold.n_signals);
    writer.Key("n_short_signals");
    writer.Int(fold.n_short_signals);
    writer.Key("signal_sum");
    writer.Double(SafeDouble(fold.signal_sum));
    writer.Key("short_signal_sum");
    writer.Double(SafeDouble(fold.short_signal_sum));
    writer.Key("signal_rate");
    writer.Double(SafeDouble(fold.signal_rate));
    writer.Key("short_signal_rate");
    writer.Double(SafeDouble(fold.short_signal_rate));
    writer.Key("avg_return_on_signals");
    writer.Double(SafeDouble(fold.avg_return_on_signals));
    writer.Key("median_return_on_signals");
    writer.Double(SafeDouble(fold.median_return_on_signals));
    writer.Key("std_return_on_signals");
    writer.Double(SafeDouble(fold.std_return_on_signals));
    writer.Key("avg_return_on_short_signals");
    writer.Double(SafeDouble(fold.avg_return_on_short_signals));
    writer.Key("avg_predicted_return_on_signals");
    writer.Double(SafeDouble(fold.avg_predicted_return_on_signals));
    writer.Key("running_sum");
    writer.Double(SafeDouble(fold.running_sum));
    writer.Key("running_sum_short");
    writer.Double(SafeDouble(fold.running_sum_short));
    writer.Key("running_sum_dual");
    writer.Double(SafeDouble(fold.running_sum_dual));
    writer.Key("sum_wins");
    writer.Double(SafeDouble(fold.sum_wins));
    writer.Key("sum_losses");
    writer.Double(SafeDouble(fold.sum_losses));
    writer.Key("sum_short_wins");
    writer.Double(SafeDouble(fold.sum_short_wins));
    writer.Key("sum_short_losses");
    writer.Double(SafeDouble(fold.sum_short_losses));
    writer.Key("model_learned_nothing");
    writer.Bool(fold.model_learned_nothing);
    writer.Key("used_cached_model");
    writer.Bool(fold.used_cached_model);
    writer.EndObject();
    writer.EndObject();
}

std::string BuildRunJson(const Stage1MetadataWriter::WalkforwardRecord& record,
                         const std::string& requester) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();

    writer.Key("run");
    writer.StartObject();
    writer.Key("run_id");
    writer.String(record.run_id.c_str());
    writer.Key("dataset_id");
    writer.String(record.dataset_id.c_str());
    writer.Key("prediction_measurement");
    writer.String(record.prediction_measurement.c_str());
    writer.Key("target_column");
    writer.String(record.target_column.c_str());
    writer.Key("feature_columns");
    writer.StartArray();
    for (const auto& feature : record.feature_columns) {
        writer.String(feature.c_str());
    }
    writer.EndArray();
    writer.Key("hyperparameters");
    WriteJsonObjectOrEmpty(writer, record.hyperparameters_json);
    writer.Key("walk_config");
    WriteJsonObjectOrEmpty(writer, record.walk_config_json);
    writer.Key("summary_metrics");
    WriteJsonObjectOrEmpty(writer, record.summary_metrics_json);
    writer.Key("status");
    writer.String(record.status.c_str());
    writer.Key("requested_by");
    writer.String(requester.c_str());
    writer.Key("started_at");
    writer.String(ToIso8601(record.started_at).c_str());
    writer.Key("completed_at");
    writer.String(ToIso8601(record.completed_at).c_str());
    writer.Key("duration_ms");
    writer.Int64(record.duration_ms);
    writer.EndObject();

    writer.Key("folds");
    writer.StartArray();
    for (const auto& fold : record.folds) {
        WriteFoldJson(writer, fold, record.run_id);
    }
    writer.EndArray();

    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
}

std::string BuildSimulationJson(const Stage1MetadataWriter::SimulationRecord& record,
                                const std::vector<ExecutedTrade>& trades) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("simulation_id");
    writer.String(record.simulation_id.c_str());
    writer.Key("run_id");
    writer.String(record.run_id.c_str());
    writer.Key("dataset_id");
    writer.String(record.dataset_id.c_str());
    writer.Key("input_run_measurement");
    writer.String(record.input_run_measurement.c_str());
    writer.Key("questdb_namespace");
    writer.String(record.questdb_namespace.c_str());
    writer.Key("mode");
    writer.String(record.mode.c_str());
    writer.Key("status");
    writer.String(record.status.c_str());
    writer.Key("started_at");
    writer.String(ToIso8601(record.started_at).c_str());
    writer.Key("completed_at");
    writer.String(ToIso8601(record.completed_at).c_str());
    writer.Key("config");
    WriteJsonObjectOrEmpty(writer, record.config_json);
    writer.Key("summary_metrics");
    WriteJsonObjectOrEmpty(writer, record.summary_metrics_json);
    writer.Key("buckets");
    writer.StartArray();
    for (const auto& bucket : record.buckets) {
        writer.StartObject();
        writer.Key("side");
        writer.String(bucket.side.c_str());
        writer.Key("trade_count");
        writer.Int64(bucket.trade_count);
        writer.Key("win_count");
        writer.Int64(bucket.win_count);
        writer.Key("profit_factor");
        writer.Double(SafeDouble(bucket.profit_factor));
        writer.Key("avg_return_pct");
        writer.Double(SafeDouble(bucket.avg_return_pct));
        writer.Key("max_drawdown_pct");
        writer.Double(SafeDouble(bucket.max_drawdown_pct));
        writer.Key("notes");
        writer.String(bucket.notes.c_str());
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("trades");
    writer.StartArray();
    for (std::size_t i = 0; i < trades.size(); ++i) {
        const auto& trade = trades[i];
        writer.StartObject();
        std::string tradeId = Stage1MetadataWriter::MakeDeterministicUuid(
            record.simulation_id + ":trade:" + std::to_string(i + 1));
        writer.Key("trade_id");
        writer.String(tradeId.c_str());
        writer.Key("fold_index");
        writer.Int(trade.fold_index);
        writer.Key("side");
        writer.String(trade.is_long ? "long" : "short");
        writer.Key("size");
        writer.Double(SafeDouble(trade.quantity));
        writer.Key("entry_price");
        writer.Double(SafeDouble(trade.entry_price));
        writer.Key("exit_price");
        writer.Double(SafeDouble(trade.exit_price));
        writer.Key("pnl");
        writer.Double(SafeDouble(trade.pnl));
        writer.Key("return_pct");
        writer.Double(SafeDouble(trade.return_pct));
        writer.Key("entry_signal");
        writer.Double(SafeDouble(trade.entry_signal));
        writer.Key("exit_signal");
        writer.Double(SafeDouble(trade.exit_signal));
        writer.Key("entry_timestamp");
        writer.Int64(SafeTimestampMillis(trade.entry_timestamp));
        writer.Key("exit_timestamp");
        writer.Int64(SafeTimestampMillis(trade.exit_timestamp));
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize());
}

bool PostStage1Json(const char* label,
                    const std::string& path,
                    const std::string& payload,
                    std::string* errorOut = nullptr) {
    stage1::RestClient& api = stage1::RestClient::Instance();
    long status = 0;
    std::string response;
    std::string error;
    if (!api.PostJson(path, payload, &status, &response, &error)) {
        std::cerr << "[Stage1MetadataWriter] Failed to POST " << label
                  << " to Stage1 API: " << (error.empty() ? "unknown error" : error) << std::endl;
        if (errorOut) {
            *errorOut = error.empty() ? "Stage1 API request failed" : error;
        }
        return false;
    }
    if (status < 200 || status >= 300) {
        std::cerr << "[Stage1MetadataWriter] Stage1 API returned HTTP " << status
                  << " for " << label;
        if (!response.empty()) {
            std::cerr << " (" << response << ")";
        }
        std::cerr << std::endl;
        if (errorOut) {
            *errorOut = response.empty()
                ? "Stage1 API returned HTTP " + std::to_string(status)
                : response;
        }
        return false;
    }
    return true;
}

std::string DurationMs(std::chrono::system_clock::time_point start,
                        std::chrono::system_clock::time_point end) {
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    return std::to_string(duration.count());
}

std::string FormatDouble(double value) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << std::setprecision(6) << value;
    return oss.str();
}

std::string MakeUuidFromSeed(const std::string& seed) {
    std::array<uint8_t, 16> bytes{};
    const uint64_t h1 = std::hash<std::string>{}(seed);
    const uint64_t h2 = std::hash<std::string>{}(seed + "#stage1");

    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>((h1 >> (8 * i)) & 0xFF);
        bytes[8 + i] = static_cast<uint8_t>((h2 >> (8 * i)) & 0xFF);
    }

    // Set UUID version (4) and variant (RFC 4122)
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);

    auto hex = [](uint8_t byte) {
        const char* digits = "0123456789abcdef";
        std::string out;
        out.push_back(digits[(byte >> 4) & 0x0F]);
        out.push_back(digits[byte & 0x0F]);
        return out;
    };

    std::ostringstream oss;
    int index = 0;
    for (int group = 0; group < 5; ++group) {
        int groupLength = 0;
        switch (group) {
            case 0: groupLength = 4; break;   // 8 hex chars
            case 1: groupLength = 2; break;   // 4 hex chars
            case 2: groupLength = 2; break;
            case 3: groupLength = 2; break;
            case 4: groupLength = 6; break;   // 12 hex chars
        }
        for (int i = 0; i < groupLength; ++i) {
            oss << hex(bytes[index++]);
        }
        if (group < 4) {
            oss << '-';
        }
    }
    return oss.str();
}

} // namespace

namespace {

} // namespace

Stage1MetadataWriter& Stage1MetadataWriter::Instance() {
    static Stage1MetadataWriter instance;
    return instance;
}

Stage1MetadataWriter::Stage1MetadataWriter()
    : m_spoolPath(kSpoolPath) {
    std::lock_guard<std::mutex> lock(g_writerMutex);
    std::filesystem::create_directories(std::filesystem::path(m_spoolPath).parent_path());
    if (!std::filesystem::exists(m_spoolPath)) {
        std::ofstream out(m_spoolPath, std::ios::app);
        out << "-- Auto-generated Stage 1 metadata inserts. Apply with psql once\n"
            << "-- connectivity to 45.85.147.236 is available.\n\n";
    }
}

void Stage1MetadataWriter::RecordDatasetExport(const DatasetRecord& record, PersistMode mode) {
    auto TsLiteral = [](const std::optional<std::int64_t>& value) {
        return value ? ToTimestampLiteral(*value) : std::string("NULL");
    };
    std::string metadataJson = record.metadata_json.empty()
        ? "'{}'::jsonb"
        : Quote(record.metadata_json) + "::jsonb";

    std::ostringstream sql;
    sql << "SELECT upsert_stage1_dataset("
        << Quote(record.dataset_id) << ", "
        << Quote(record.dataset_slug.empty() ? record.dataset_id : record.dataset_slug) << ", "
        << Quote(record.symbol.empty() ? "UNKNOWN" : record.symbol) << ", "
        << Quote(record.granularity.empty() ? "unknown" : record.granularity) << ", "
        << Quote(record.source.empty() ? "laptop_imgui" : record.source) << ", "
        << Quote(record.ohlcv_measurement) << ", "
        << Quote(record.indicator_measurement) << ", "
        << record.ohlcv_row_count << ", "
        << record.indicator_row_count << ", "
        << TsLiteral(record.ohlcv_first_timestamp_unix) << ", "
        << TsLiteral(record.ohlcv_last_timestamp_unix) << ", "
        << TsLiteral(record.indicator_first_timestamp_unix) << ", "
        << TsLiteral(record.indicator_last_timestamp_unix) << ", "
        << metadataJson
        << ");\n";

    sql << "INSERT INTO indicator_datasets (dataset_id, symbol, granularity, source, "
           "questdb_tag, row_count, first_bar_ts, last_bar_ts, created_at)\n"
        << "VALUES ("
        << Quote(record.dataset_id) << ", "
        << Quote(record.symbol.empty() ? "UNKNOWN" : record.symbol) << ", "
        << Quote(record.granularity.empty() ? "unknown" : record.granularity) << ", "
        << Quote(record.source.empty() ? "laptop_imgui" : record.source) << ", "
        << Quote(record.indicator_measurement.empty() ? record.dataset_slug : record.indicator_measurement) << ", "
        << record.indicator_row_count << ", "
        << TsLiteral(record.indicator_first_timestamp_unix) << ", "
        << TsLiteral(record.indicator_last_timestamp_unix) << ", "
        << ToTimestampLiteral(record.created_at) << ")\n"
        << "ON CONFLICT (dataset_id) DO UPDATE SET\n"
        << "  symbol = EXCLUDED.symbol,\n"
        << "  granularity = EXCLUDED.granularity,\n"
        << "  source = EXCLUDED.source,\n"
        << "  questdb_tag = EXCLUDED.questdb_tag,\n"
        << "  row_count = EXCLUDED.row_count,\n"
        << "  first_bar_ts = EXCLUDED.first_bar_ts,\n"
        << "  last_bar_ts = EXCLUDED.last_bar_ts;\n\n";

    const std::string datasetJson = BuildDatasetJson(record);
    PostStage1Json("dataset", "/api/datasets", datasetJson);
    AppendSql(sql.str(), mode);
}

bool Stage1MetadataWriter::RecordWalkforwardRun(const WalkforwardRecord& record,
                                                std::string* error,
                                                PersistMode mode) {
    std::string requester = record.requested_by.empty() ? CurrentUsername() : record.requested_by;
    const std::string runJson = BuildRunJson(record, requester);

    std::ostringstream sql;
    sql << "INSERT INTO walkforward_runs (run_id, dataset_id, prediction_measurement, "
           "target_column, feature_columns, hyperparameters, walk_config, status, requested_by, "
           "started_at, completed_at, duration_ms, summary_metrics, created_at)\n"
        << "VALUES ("
        << Quote(record.run_id) << ", "
        << Quote(record.dataset_id) << ", "
        << Quote(record.prediction_measurement) << ", "
        << Quote(record.target_column) << ", "
        << Quote(ToJsonArray(record.feature_columns)) << "::jsonb, "
        << Quote(record.hyperparameters_json) << "::jsonb, "
        << Quote(record.walk_config_json) << "::jsonb, "
        << Quote(record.status) << ", "
        << Quote(requester) << ", "
        << ToTimestampLiteral(record.started_at) << ", "
        << ToTimestampLiteral(record.completed_at) << ", "
        << record.duration_ms << ", "
        << Quote(record.summary_metrics_json) << "::jsonb, "
        << ToTimestampLiteral(record.started_at) << ")\n"
        << "ON CONFLICT (run_id) DO UPDATE SET\n"
        << "  prediction_measurement = EXCLUDED.prediction_measurement,\n"
        << "  feature_columns = EXCLUDED.feature_columns,\n"
        << "  hyperparameters = EXCLUDED.hyperparameters,\n"
        << "  walk_config = EXCLUDED.walk_config,\n"
        << "  status = EXCLUDED.status,\n"
        << "  requested_by = EXCLUDED.requested_by,\n"
        << "  started_at = EXCLUDED.started_at,\n"
        << "  completed_at = EXCLUDED.completed_at,\n"
        << "  duration_ms = EXCLUDED.duration_ms,\n"
        << "  summary_metrics = EXCLUDED.summary_metrics;\n\n";

    std::string stage1Error;
    bool apiSuccess = PostStage1Json("walkforward run", "/api/runs", runJson, &stage1Error);
    AppendSql(sql.str(), mode);

    for (const auto& fold : record.folds) {
        std::ostringstream foldSql;
        std::ostringstream thresholds;
        thresholds << "{"
                   << "\"long_optimal\":" << FormatDouble(fold.long_threshold_optimal) << ","
                   << "\"short_optimal\":" << FormatDouble(fold.short_threshold_optimal) << ","
                   << "\"prediction_scaled\":" << FormatDouble(fold.prediction_threshold_scaled) << ","
                   << "\"prediction_original\":" << FormatDouble(fold.prediction_threshold_original) << ","
                   << "\"dynamic_positive\":" << FormatDouble(fold.dynamic_positive_threshold) << ","
                   << "\"short_scaled\":" << FormatDouble(fold.short_threshold_scaled) << ","
                   << "\"short_original\":" << FormatDouble(fold.short_threshold_original) << ","
                   << "\"long_percentile\":" << FormatDouble(fold.long_threshold_95th) << ","
                   << "\"short_percentile\":" << FormatDouble(fold.short_threshold_5th)
                   << "}";
        std::ostringstream metrics;
        metrics << "{"
                << "\"hit_rate\":" << FormatDouble(fold.hit_rate) << ","
                << "\"short_hit_rate\":" << FormatDouble(fold.short_hit_rate) << ","
                << "\"profit_factor_test\":" << FormatDouble(fold.profit_factor_test) << ","
                << "\"profit_factor_train\":" << FormatDouble(fold.profit_factor_train) << ","
                << "\"profit_factor_short_train\":" << FormatDouble(fold.profit_factor_short_train) << ","
                << "\"profit_factor_short_test\":" << FormatDouble(fold.profit_factor_short_test) << ","
                << "\"n_signals\":" << fold.n_signals << ","
                << "\"n_short_signals\":" << fold.n_short_signals << ","
                << "\"signal_sum\":" << FormatDouble(fold.signal_sum) << ","
                << "\"short_signal_sum\":" << FormatDouble(fold.short_signal_sum) << ","
                << "\"signal_rate\":" << FormatDouble(fold.signal_rate) << ","
                << "\"short_signal_rate\":" << FormatDouble(fold.short_signal_rate) << ","
                << "\"avg_return_on_signals\":" << FormatDouble(fold.avg_return_on_signals) << ","
                << "\"median_return_on_signals\":" << FormatDouble(fold.median_return_on_signals) << ","
                << "\"std_return_on_signals\":" << FormatDouble(fold.std_return_on_signals) << ","
                << "\"avg_return_on_short_signals\":" << FormatDouble(fold.avg_return_on_short_signals) << ","
                << "\"avg_predicted_return_on_signals\":" << FormatDouble(fold.avg_predicted_return_on_signals) << ","
                << "\"running_sum\":" << FormatDouble(fold.running_sum) << ","
                << "\"running_sum_short\":" << FormatDouble(fold.running_sum_short) << ","
                << "\"running_sum_dual\":" << FormatDouble(fold.running_sum_dual) << ","
                << "\"sum_wins\":" << FormatDouble(fold.sum_wins) << ","
                << "\"sum_losses\":" << FormatDouble(fold.sum_losses) << ","
                << "\"sum_short_wins\":" << FormatDouble(fold.sum_short_wins) << ","
                << "\"sum_short_losses\":" << FormatDouble(fold.sum_short_losses) << ","
                << "\"model_learned_nothing\":" << (fold.model_learned_nothing ? "true" : "false") << ","
                << "\"used_cached_model\":" << (fold.used_cached_model ? "true" : "false")
                << "}";

        foldSql << "INSERT INTO walkforward_folds "
                   "(run_id, fold_number, train_start_idx, train_end_idx, test_start_idx, "
                   "test_end_idx, samples_train, samples_test, best_iteration, best_score, "
                   "thresholds, metrics)\n"
                << "VALUES ("
                << Quote(record.run_id) << ", "
                << fold.fold_number << ", "
                << fold.train_start << ", "
                << fold.train_end << ", "
                << fold.test_start << ", "
                << fold.test_end << ", "
                << fold.samples_train << ", "
                << fold.samples_test << ", "
                << (fold.best_iteration.has_value() ? std::to_string(*fold.best_iteration) : std::string("NULL")) << ", "
                << (fold.best_score.has_value() ? FormatDouble(*fold.best_score) : std::string("NULL")) << ", "
                << Quote(thresholds.str()) << "::jsonb, "
                << Quote(metrics.str()) << "::jsonb)\n"
                << "ON CONFLICT (run_id, fold_number) DO UPDATE SET\n"
                << "  train_start_idx = EXCLUDED.train_start_idx,\n"
                << "  train_end_idx = EXCLUDED.train_end_idx,\n"
                << "  test_start_idx = EXCLUDED.test_start_idx,\n"
                << "  test_end_idx = EXCLUDED.test_end_idx,\n"
                << "  samples_train = EXCLUDED.samples_train,\n"
                << "  samples_test = EXCLUDED.samples_test,\n"
                << "  best_iteration = EXCLUDED.best_iteration,\n"
                << "  best_score = EXCLUDED.best_score,\n"
                << "  thresholds = EXCLUDED.thresholds,\n"
                << "  metrics = EXCLUDED.metrics;\n\n";
        AppendSql(foldSql.str(), mode);
    }

    if (!apiSuccess) {
        if (error) {
            *error = stage1Error;
        }
        return false;
    }
    return true;
}

void Stage1MetadataWriter::RecordSimulationRun(
    const SimulationRecord& record,
    const std::vector<ExecutedTrade>& trades,
    PersistMode mode) {
    const std::string simulationJson = BuildSimulationJson(record, trades);

    std::ostringstream sql;
    sql << "INSERT INTO simulation_runs "
           "(simulation_id, run_id, dataset_id, input_run_measurement, questdb_namespace, "
           "mode, config, status, started_at, completed_at, summary_metrics, created_at)\n"
        << "VALUES ("
        << Quote(record.simulation_id) << ", "
        << Quote(record.run_id) << ", "
        << Quote(record.dataset_id) << ", "
        << Quote(record.input_run_measurement) << ", "
        << Quote(record.questdb_namespace) << ", "
        << Quote(record.mode) << ", "
        << Quote(record.config_json) << "::jsonb, "
        << Quote(record.status) << ", "
        << ToTimestampLiteral(record.started_at) << ", "
        << ToTimestampLiteral(record.completed_at) << ", "
        << Quote(record.summary_metrics_json) << "::jsonb, "
        << ToTimestampLiteral(record.started_at) << ")\n"
        << "ON CONFLICT (simulation_id) DO UPDATE SET\n"
        << "  mode = EXCLUDED.mode,\n"
        << "  config = EXCLUDED.config,\n"
        << "  questdb_namespace = EXCLUDED.questdb_namespace,\n"
        << "  status = EXCLUDED.status,\n"
        << "  started_at = EXCLUDED.started_at,\n"
        << "  completed_at = EXCLUDED.completed_at,\n"
        << "  summary_metrics = EXCLUDED.summary_metrics;\n\n";
    PostStage1Json("simulation run", "/api/simulations", simulationJson);
    AppendSql(sql.str(), mode);

    for (const auto& bucket : record.buckets) {
        std::ostringstream bucketSql;
        bucketSql << "INSERT INTO simulation_trade_buckets "
                     "(simulation_id, side, trade_count, win_count, profit_factor, "
                     "avg_return_pct, max_drawdown_pct, notes)\n"
                  << "VALUES ("
                  << Quote(record.simulation_id) << ", "
                  << Quote(bucket.side) << ", "
                  << bucket.trade_count << ", "
                  << bucket.win_count << ", "
                  << FormatDouble(bucket.profit_factor) << ", "
                  << FormatDouble(bucket.avg_return_pct) << ", "
                  << FormatDouble(bucket.max_drawdown_pct) << ", "
                  << Quote(bucket.notes) << ")\n"
                  << "ON CONFLICT (simulation_id, side) DO UPDATE SET\n"
                  << "  trade_count = EXCLUDED.trade_count,\n"
                  << "  win_count = EXCLUDED.win_count,\n"
                  << "  profit_factor = EXCLUDED.profit_factor,\n"
                  << "  avg_return_pct = EXCLUDED.avg_return_pct,\n"
                  << "  max_drawdown_pct = EXCLUDED.max_drawdown_pct,\n"
                  << "  notes = EXCLUDED.notes;\n\n";
        AppendSql(bucketSql.str(), mode);
    }

    for (std::size_t i = 0; i < trades.size(); ++i) {
        const auto& trade = trades[i];
        std::ostringstream tradeSql;
        std::string trade_id = MakeUuidFromSeed(record.simulation_id + ":trade:" + std::to_string(i + 1));
        tradeSql << "INSERT INTO simulation_trades "
                    "(trade_id, simulation_id, bar_timestamp, side, size, "
                    "entry_price, exit_price, pnl, return_pct, metadata)\n"
                 << "VALUES ("
                 << Quote(trade_id) << ", "
                 << Quote(record.simulation_id) << ", "
                 << ToTimestampLiteral(static_cast<std::int64_t>(trade.entry_timestamp / 1000.0)) << ", "
                 << Quote(trade.is_long ? "long" : "short") << ", "
                 << FormatDouble(trade.quantity) << ", "
                 << FormatDouble(trade.entry_price) << ", "
                 << FormatDouble(trade.exit_price) << ", "
                 << FormatDouble(trade.pnl) << ", "
                 << FormatDouble(trade.return_pct) << ", "
                 << Quote("{\"fold\":" + std::to_string(trade.fold_index) +
                          ",\"entry_signal\":" + FormatDouble(trade.entry_signal) +
                          ",\"exit_signal\":" + FormatDouble(trade.exit_signal) + "}") << ")\n"
                 << "ON CONFLICT (trade_id) DO UPDATE SET\n"
                 << "  bar_timestamp = EXCLUDED.bar_timestamp,\n"
                 << "  side = EXCLUDED.side,\n"
                 << "  size = EXCLUDED.size,\n"
                 << "  entry_price = EXCLUDED.entry_price,\n"
                 << "  exit_price = EXCLUDED.exit_price,\n"
                 << "  pnl = EXCLUDED.pnl,\n"
                 << "  return_pct = EXCLUDED.return_pct,\n"
                 << "  metadata = EXCLUDED.metadata;\n\n";
        AppendSql(tradeSql.str(), mode);
    }
}

void Stage1MetadataWriter::AppendSql(const std::string& sql, PersistMode mode) {
    if (mode == PersistMode::DatabaseOnly) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_writerMutex);
    std::ofstream out(m_spoolPath, std::ios::app);
    if (!out) {
        return;
    }
    out << sql;
}

std::string Stage1MetadataWriter::MakeDeterministicUuid(const std::string& seed) {
    return MakeUuidFromSeed(seed);
}

std::string Stage1MetadataWriter::EscapeSql(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\'') {
            escaped.push_back('\'');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string Stage1MetadataWriter::Quote(const std::string& value) {
    return "'" + EscapeSql(value) + "'";
}

std::string Stage1MetadataWriter::ToTimestampLiteral(std::int64_t unix_seconds) {
    std::ostringstream oss;
    oss << "TO_TIMESTAMP(" << unix_seconds << ")";
    return oss.str();
}

std::string Stage1MetadataWriter::ToTimestampLiteral(std::chrono::system_clock::time_point tp) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return ToTimestampLiteral(seconds);
}

std::string Stage1MetadataWriter::JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

std::string Stage1MetadataWriter::ToJsonArray(const std::vector<std::string>& values) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        oss << "\"" << JsonEscape(values[i]) << "\"";
        if (i + 1 < values.size()) {
            oss << ",";
        }
    }
    oss << "]";
    return oss.str();
}

std::string Stage1MetadataWriter::CurrentUsername() {
    const char* user = std::getenv("USER");
    if (!user) {
        user = std::getenv("USERNAME");
    }
    return user ? std::string(user) : std::string("laptop_user");
}

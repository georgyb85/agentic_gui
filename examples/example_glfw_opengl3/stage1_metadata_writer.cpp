#include "stage1_metadata_writer.h"
#include "Stage1DatasetManifest.h"
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
#include <thread>
#include <cctype>
#include <algorithm>

#include <json/json.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <libpq-fe.h>

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

Json::Value ParseJsonObject(const std::string& text) {
    if (text.empty()) {
        return Json::Value(Json::objectValue);
    }
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value value(Json::objectValue);
    std::string errs;
    if (reader->parse(text.data(), text.data() + text.size(), &value, &errs) && value.isObject()) {
        return value;
    }
    return Json::Value(Json::objectValue);
}

std::string SerializeJson(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::string SerializeFeatureColumns(const std::vector<std::string>& columns) {
    Json::Value array(Json::arrayValue);
    for (const auto& column : columns) {
        array.append(column);
    }
    return SerializeJson(array);
}

std::string GetEnvOrDefault(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    if (value && *value) {
        return value;
    }
    return fallback ? std::string(fallback) : std::string();
}

std::string TrimErrorMessage(const char* message) {
    if (!message) {
        return {};
    }
    std::string trimmed(message);
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }
    return trimmed;
}

std::string QuoteConnValue(const std::string& value) {
    std::string quoted;
    quoted.reserve(value.size() + 3);
    quoted.push_back('\'');
    for (char ch : value) {
        if (ch == '\'') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('\'');
    return quoted;
}

int64_t IntervalFromGranularity(const std::string& granularity) {
    if (granularity == "1m") return 60 * 1000LL;
    if (granularity == "5m") return 5 * 60 * 1000LL;
    if (granularity == "15m") return 15 * 60 * 1000LL;
    if (granularity == "30m") return 30 * 60 * 1000LL;
    if (granularity == "1h") return 60 * 60 * 1000LL;
    if (granularity == "4h") return 4 * 60 * 60 * 1000LL;
    if (granularity == "1d") return 24 * 60 * 60 * 1000LL;
    return 0;
}

stage1::DatasetManifest BuildManifestFromRecord(const Stage1MetadataWriter::DatasetRecord& record) {
    stage1::DatasetManifest manifest;
    manifest.dataset_id = record.dataset_id;
    manifest.dataset_slug = record.dataset_slug;
    manifest.symbol = record.symbol;
    manifest.granularity = record.granularity;
    manifest.source = record.source;
    manifest.ohlcv_measurement = record.ohlcv_measurement;
    manifest.indicator_measurement = record.indicator_measurement;
    manifest.ohlcv_rows = record.ohlcv_row_count;
    manifest.indicator_rows = record.indicator_row_count;
    manifest.first_ohlcv_timestamp_ms = record.ohlcv_first_timestamp_unix.value_or(0);
    manifest.last_ohlcv_timestamp_ms = record.ohlcv_last_timestamp_unix.value_or(0);
    manifest.first_indicator_timestamp_ms = record.indicator_first_timestamp_unix.value_or(0);
    manifest.last_indicator_timestamp_ms = record.indicator_last_timestamp_unix.value_or(0);
    manifest.bar_interval_ms = IntervalFromGranularity(record.granularity);
    if (manifest.bar_interval_ms == 0 && manifest.ohlcv_rows > 1 && manifest.last_ohlcv_timestamp_ms > manifest.first_ohlcv_timestamp_ms) {
        const auto span = manifest.last_ohlcv_timestamp_ms - manifest.first_ohlcv_timestamp_ms;
        manifest.bar_interval_ms = span / std::max<int64_t>(1, manifest.ohlcv_rows - 1);
    }
    if (manifest.ohlcv_rows > manifest.indicator_rows) {
        manifest.lookback_rows = manifest.ohlcv_rows - manifest.indicator_rows;
    }
    manifest.exported_at_iso = stage1::FormatIsoTimestamp(record.created_at);
    return manifest;
}

bool ExecuteDatasetSql(const std::string& sql, std::string* error) {
    if (sql.empty()) {
        return true;
    }

    const std::string host = GetEnvOrDefault("STAGE1_POSTGRES_HOST", "45.85.147.236");
    const std::string port = GetEnvOrDefault("STAGE1_POSTGRES_PORT", "5432");
    const std::string db = GetEnvOrDefault("STAGE1_POSTGRES_DB", "stage1_trading");
    const std::string user = GetEnvOrDefault("STAGE1_POSTGRES_USER", "stage1_app");
    const std::string password = GetEnvOrDefault("STAGE1_POSTGRES_PASSWORD", "TempPass2025");

    std::ostringstream conninfo;
    conninfo << "host=" << QuoteConnValue(host)
             << " port=" << QuoteConnValue(port)
             << " dbname=" << QuoteConnValue(db)
             << " user=" << QuoteConnValue(user)
             << " password=" << QuoteConnValue(password);

    PGconn* conn = PQconnectdb(conninfo.str().c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        if (error) {
            *error = TrimErrorMessage(conn ? PQerrorMessage(conn) : "PG connection failure");
        }
        if (conn) {
            PQfinish(conn);
        }
        return false;
    }

    PGresult* res = PQexec(conn, sql.c_str());
    bool success = true;
    std::string lastError;

    while (res) {
        ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            success = false;
            const char* msg = PQresultErrorMessage(res);
            if (msg && *msg) {
                lastError = TrimErrorMessage(msg);
            }
        }
        PQclear(res);
        res = PQgetResult(conn);
    }

    if (!success && error) {
        if (!lastError.empty()) {
            *error = lastError;
        } else {
            *error = TrimErrorMessage(PQerrorMessage(conn));
        }
    }

    PQfinish(conn);
    return success;
}

std::string BuildDatasetJson(const Stage1MetadataWriter::DatasetRecord& record) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    auto manifest = BuildManifestFromRecord(record);
    std::string manifestPayload = record.metadata_json.empty()
        ? manifest.ToJsonString()
        : record.metadata_json;

    writer.StartObject();
    writer.Key("dataset_id");
    writer.String(record.dataset_id.c_str());
    writer.Key("dataset_slug");
    writer.String(record.dataset_slug.c_str());
    writer.Key("symbol");
    writer.String(record.symbol.c_str());
    writer.Key("granularity");
    writer.String(record.granularity.c_str());
    writer.Key("bar_interval_ms");
    writer.Int64(manifest.bar_interval_ms);
    writer.Key("lookback_rows");
    writer.Int64(manifest.lookback_rows);
    if (record.ohlcv_first_timestamp_unix) {
        writer.Key("first_ohlcv_ts");
        writer.Int64(*record.ohlcv_first_timestamp_unix);
    }
    if (record.indicator_first_timestamp_unix) {
        writer.Key("first_indicator_ts");
        writer.Int64(*record.indicator_first_timestamp_unix);
    }
    writer.Key("metadata");
    WriteJsonObjectOrEmpty(writer, manifestPayload);
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
                         const std::string& requester,
                         const std::string& walkConfigOverride) {
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
    if (walkConfigOverride.empty()) {
        WriteJsonObjectOrEmpty(writer, record.walk_config_json);
    } else {
        WriteJsonObjectOrEmpty(writer, walkConfigOverride);
    }
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

bool Stage1MetadataWriter::NetworkExportsEnabled() {
    const char* flag = std::getenv("STAGE1_ENABLE_EXPORTS");
    if (!flag || !*flag) {
        return true;
    }
    std::string value(flag);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    return true;
}

bool Stage1MetadataWriter::DirectDatabaseExportsEnabled() {
    const char* flag = std::getenv("STAGE1_DIRECT_DB_EXPORTS");
    if (!flag || !*flag) {
        return false;
    }
    std::string value(flag);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return !(value == "0" || value == "false" || value == "off" || value == "no");
}

bool Stage1MetadataWriter::RecordDatasetExport(const DatasetRecord& record,
                                               std::string* error,
                                               PersistMode mode) {
    auto MsLiteral = [](const std::optional<std::int64_t>& value) {
        return value ? std::to_string(*value) : std::string("NULL");
    };
    const bool allowNetwork = (mode != PersistMode::FileOnly) && NetworkExportsEnabled();
    std::string manifestPayload = record.metadata_json;
    auto manifest = BuildManifestFromRecord(record);
    if (manifestPayload.empty()) {
        manifestPayload = manifest.ToJsonString();
    }
    std::string metadataJson = manifestPayload.empty()
        ? "'{}'::jsonb"
        : Quote(manifestPayload) + "::jsonb";

    const std::string datasetSlug = record.dataset_slug.empty() ? record.dataset_id : record.dataset_slug;
    const std::string symbol = record.symbol.empty() ? "UNKNOWN" : record.symbol;
    const std::string granularity = record.granularity.empty() ? "unknown" : record.granularity;

    if (manifest.bar_interval_ms <= 0 && record.ohlcv_first_timestamp_unix && record.ohlcv_last_timestamp_unix) {
        const auto span = *record.ohlcv_last_timestamp_unix - *record.ohlcv_first_timestamp_unix;
        if (span > 0 && record.ohlcv_row_count > 1) {
            manifest.bar_interval_ms = span / std::max<int64_t>(1, record.ohlcv_row_count - 1);
        }
    }
    if (manifest.lookback_rows <= 0 &&
        record.ohlcv_row_count > 0 && record.indicator_row_count > 0) {
        manifest.lookback_rows = std::max<int64_t>(0, record.ohlcv_row_count - record.indicator_row_count);
    }

    std::ostringstream sql;
    sql << "INSERT INTO datasets (dataset_id, dataset_slug, symbol, granularity, "
        << "bar_interval_ms, lookback_rows, first_ohlcv_ts, first_indicator_ts, metadata, created_at)\n"
        << "VALUES ("
        << Quote(record.dataset_id) << ", "
        << Quote(datasetSlug) << ", "
        << Quote(symbol) << ", "
        << Quote(granularity) << ", "
        << manifest.bar_interval_ms << ", "
        << manifest.lookback_rows << ", "
        << MsLiteral(record.ohlcv_first_timestamp_unix) << ", "
        << MsLiteral(record.indicator_first_timestamp_unix) << ", "
        << metadataJson << ", "
        << ToTimestampLiteral(record.created_at) << ")\n"
        << "ON CONFLICT (dataset_id) DO UPDATE SET\n"
        << "  dataset_slug = EXCLUDED.dataset_slug,\n"
        << "  symbol = EXCLUDED.symbol,\n"
        << "  granularity = EXCLUDED.granularity,\n"
        << "  bar_interval_ms = EXCLUDED.bar_interval_ms,\n"
        << "  lookback_rows = EXCLUDED.lookback_rows,\n"
        << "  first_ohlcv_ts = EXCLUDED.first_ohlcv_ts,\n"
        << "  first_indicator_ts = EXCLUDED.first_indicator_ts,\n"
        << "  metadata = EXCLUDED.metadata;\n\n";

    bool pgOk = true;
    std::string pgError;
    const bool allowDirectDb = allowNetwork && DirectDatabaseExportsEnabled();
    if (allowDirectDb) {
        pgOk = ExecuteDatasetSql(sql.str(), &pgError);
        if (!pgOk) {
            std::cerr << "[Stage1MetadataWriter] Postgres dataset upsert failed: "
                      << pgError << std::endl;
        }
    }

    const std::string datasetJson = BuildDatasetJson(record);
    std::string apiError;
    bool apiOk = true;
    if (allowNetwork) {
        apiOk = PostStage1Json("dataset", "/api/datasets", datasetJson, &apiError);
    }
    AppendSql(sql.str(), allowNetwork ? mode : PersistMode::FileOnly);

    if (!apiOk) {
        std::cerr << "[Stage1MetadataWriter] Stage1 dataset POST failed: "
                  << apiError << std::endl;
    }

    if (error) {
        if (!pgOk) {
            *error = pgError;
        } else if (!apiOk) {
            *error = apiError;
        } else {
            error->clear();
        }
    }
    return pgOk && apiOk;
}

namespace {

bool VerifyUploadedFoldCount(const Stage1MetadataWriter::WalkforwardRecord& record,
                             std::string* error) {
    stage1::RestClient& api = stage1::RestClient::Instance();
    stage1::RunDetail detail;
    std::string restError;
    if (!api.FetchRunDetail(record.run_id, &detail, &restError)) {
        if (error) {
            *error = restError.empty()
                ? "Failed to fetch run detail for verification."
                : restError;
        }
        return false;
    }
    const size_t expected = record.folds.size();
    const size_t actual = detail.folds.size();
    if (actual == expected) {
        return true;
    }
    if (error) {
        std::ostringstream oss;
        oss << "Stage1 stored " << actual << " of " << expected
            << " folds for run " << record.run_id << ".";
        *error = oss.str();
    }
    return false;
}

} // namespace

bool Stage1MetadataWriter::RecordWalkforwardRun(const WalkforwardRecord& record,
                                                std::string* error,
                                                PersistMode mode) {
    std::string requester = record.requested_by.empty() ? CurrentUsername() : record.requested_by;
    Json::Value walkConfigJsonValue = ParseJsonObject(record.walk_config_json);
    walkConfigJsonValue["prediction_measurement"] = record.prediction_measurement;
    std::string walkConfigJson = SerializeJson(walkConfigJsonValue);

    Json::Value hyperparametersValue = ParseJsonObject(record.hyperparameters_json);
    std::string hyperparametersJson = SerializeJson(hyperparametersValue);

    Json::Value summaryValue = ParseJsonObject(record.summary_metrics_json);
    std::string summaryJson = SerializeJson(summaryValue);

    std::string featureColumnsJson = SerializeFeatureColumns(record.feature_columns);

    const std::string runJson = BuildRunJson(record, requester, walkConfigJson);

    const bool allowNetwork = (mode != PersistMode::FileOnly) && NetworkExportsEnabled();

    std::ostringstream sql;
    sql << "INSERT INTO walkforward_runs (run_id, dataset_id, status, "
           "feature_columns, target_column, hyperparameters, walk_config, summary_metrics, "
           "started_at, completed_at, created_at)\n"
        << "VALUES ("
        << Quote(record.run_id) << ", "
        << Quote(record.dataset_id) << ", "
        << Quote(record.status) << ", "
        << Quote(featureColumnsJson) << "::jsonb, "
        << Quote(record.target_column) << ", "
        << Quote(hyperparametersJson) << "::jsonb, "
        << Quote(walkConfigJson) << "::jsonb, "
        << Quote(summaryJson) << "::jsonb, "
        << ToTimestampLiteral(record.started_at) << ", "
        << ToTimestampLiteral(record.completed_at) << ", "
        << ToTimestampLiteral(record.started_at) << ")\n"
        << "ON CONFLICT (run_id) DO UPDATE SET\n"
        << "  status = EXCLUDED.status,\n"
        << "  feature_columns = EXCLUDED.feature_columns,\n"
        << "  hyperparameters = EXCLUDED.hyperparameters,\n"
        << "  walk_config = EXCLUDED.walk_config,\n"
        << "  started_at = EXCLUDED.started_at,\n"
        << "  completed_at = EXCLUDED.completed_at,\n"
        << "  summary_metrics = EXCLUDED.summary_metrics;\n\n";

    AppendSql(sql.str(), allowNetwork ? mode : PersistMode::FileOnly);

    if (allowNetwork) {
        constexpr int kMaxAttempts = 5;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            std::string stage1Error;
            if (!PostStage1Json("walkforward run", "/api/runs", runJson, &stage1Error)) {
                if (error) {
                    *error = stage1Error.empty()
                        ? "Stage1 API request failed"
                        : stage1Error;
                }
                return false;
            }

            std::string verifyError;
            if (VerifyUploadedFoldCount(record, &verifyError)) {
                if (attempt > 1) {
                    std::cout << "[Stage1MetadataWriter] Run " << record.run_id
                              << " verified after " << attempt << " attempts." << std::endl;
                }
                break;
            }

            if (attempt == kMaxAttempts) {
                if (error) {
                    *error = verifyError.empty()
                        ? "Stage1 stored incomplete fold data after multiple attempts."
                        : verifyError;
                }
                return false;
            }

            std::cerr << "[Stage1MetadataWriter] Warning: " << verifyError
                      << " Retrying upload (" << attempt << "/" << kMaxAttempts << ")..."
                      << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
        }
    }

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
                << "\"best_iteration\":" << (fold.best_iteration.has_value() ? *fold.best_iteration : 0) << ","
                << "\"best_score\":" << FormatDouble(fold.best_score.has_value() ? *fold.best_score : 0.0) << ","
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
                   "test_end_idx, train_start_ts_ms, train_end_ts_ms, test_start_ts_ms, test_end_ts_ms, "
                   "samples_train, samples_test, metrics, thresholds)\n"
                << "VALUES ("
                << Quote(record.run_id) << ", "
                << fold.fold_number << ", "
                << fold.train_start << ", "
                << fold.train_end << ", "
                << fold.test_start << ", "
                << fold.test_end << ", "
                << "NULL, NULL, NULL, NULL, "
                << fold.samples_train << ", "
                << fold.samples_test << ", "
                << Quote(metrics.str()) << "::jsonb, "
                << Quote(thresholds.str()) << "::jsonb)\n"
                << "ON CONFLICT (run_id, fold_number) DO UPDATE SET\n"
                << "  train_start_idx = EXCLUDED.train_start_idx,\n"
                << "  train_end_idx = EXCLUDED.train_end_idx,\n"
                << "  test_start_idx = EXCLUDED.test_start_idx,\n"
                << "  test_end_idx = EXCLUDED.test_end_idx,\n"
                << "  samples_train = EXCLUDED.samples_train,\n"
                << "  samples_test = EXCLUDED.samples_test,\n"
                << "  metrics = EXCLUDED.metrics,\n"
                << "  thresholds = EXCLUDED.thresholds;\n\n";
        AppendSql(foldSql.str(), mode);
    }

    return true;
}

void Stage1MetadataWriter::RecordSimulationRun(
    const SimulationRecord& record,
    const std::vector<ExecutedTrade>& trades,
    PersistMode mode) {
    const bool allowNetwork = (mode != PersistMode::FileOnly) && NetworkExportsEnabled();
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
    if (allowNetwork) {
        PostStage1Json("simulation run", "/api/simulations", simulationJson);
    }
    AppendSql(sql.str(), allowNetwork ? mode : PersistMode::FileOnly);

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

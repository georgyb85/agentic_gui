#include "Stage1RestClient.h"

#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <iostream>

namespace {

constexpr const char* kDefaultBaseUrl = "https://agenticresearch.info";
constexpr const char* kBaseUrlEnv = "STAGE1_API_BASE_URL";
constexpr const char* kTokenEnv = "STAGE1_API_TOKEN";

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::string*>(userdata);
    const size_t realSize = size * nmemb;
    buffer->append(ptr, realSize);
    return realSize;
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream oss;
    oss << "\"";
    for (char ch : value) {
        switch (ch) {
            case '\"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    oss << "\\u"
                        << std::uppercase << std::hex
                        << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch));
                    oss << std::nouppercase << std::dec << std::setfill(' ');
                } else {
                    oss << ch;
                }
        }
    }
    oss << "\"";
    return oss.str();
}

std::string ValueToString(const rapidjson::Value& value) {
    if (value.IsString()) {
        return value.GetString();
    }
    if (value.IsBool()) {
        return value.GetBool() ? "true" : "false";
    }
    if (value.IsInt64()) {
        return std::to_string(value.GetInt64());
    }
    if (value.IsUint64()) {
        return std::to_string(value.GetUint64());
    }
    if (value.IsDouble()) {
        std::ostringstream oss;
        oss << value.GetDouble();
        return oss.str();
    }
    return {};
}

int64_t ValueToInt64(const rapidjson::Value& value) {
    if (value.IsInt64()) {
        return value.GetInt64();
    }
    if (value.IsUint64()) {
        return static_cast<int64_t>(value.GetUint64());
    }
    if (value.IsDouble()) {
        return static_cast<int64_t>(value.GetDouble());
    }
    if (value.IsString()) {
        try {
            return std::stoll(value.GetString());
        } catch (...) {
        }
    }
    return 0;
}

std::string ValueToJsonString(const rapidjson::Value& value) {
    if (value.IsString()) {
        return value.GetString();
    }
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

std::string FormatIsoTimestamp(int64_t millis) {
    if (millis <= 0) {
        return {};
    }
    std::time_t seconds = static_cast<std::time_t>(millis / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string DefaultMeasurement(const std::string& slug, const char* suffix) {
    if (slug.empty()) {
        return suffix ? std::string(suffix) : std::string();
    }
    std::string sanitized;
    sanitized.reserve(slug.size());
    for (char ch : slug) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    sanitized.push_back('_');
    sanitized.append(suffix ? suffix : "");
    return sanitized;
}

bool PopulateJobStatus(const rapidjson::Value& object, stage1::JobStatus* status) {
    if (!status || !object.IsObject()) {
        return false;
    }
    auto getString = [&](const char* key) -> std::string {
        auto it = object.FindMember(key);
        if (it == object.MemberEnd()) {
            return {};
        }
        return ValueToString(it->value);
    };
    auto getJson = [&](const char* key) -> std::string {
        auto it = object.FindMember(key);
        if (it == object.MemberEnd()) {
            return {};
        }
        return ValueToJsonString(it->value);
    };
    auto getInt64 = [&](const char* key) -> int64_t {
        auto it = object.FindMember(key);
        if (it == object.MemberEnd()) {
            return 0;
        }
        return ValueToInt64(it->value);
    };

    status->job_id = getString("job_id");
    status->job_type = getString("job_type");
    status->status = getString("status");
    status->progress = getInt64("progress");
    status->total = getInt64("total");
    status->message = getString("message");
    status->error = getString("error");
    status->payload = getJson("payload");
    status->result = getJson("result");
    status->created_at = getString("created_at");
    status->updated_at = getString("updated_at");
    status->started_at = getString("started_at");
    status->completed_at = getString("completed_at");
    return true;
}

bool ParseDatasetSummaryNode(const rapidjson::Value& item,
                             stage1::DatasetSummary* summary) {
    if (!summary || !item.IsObject()) {
        return false;
    }
    auto getString = [&](const char* key) -> std::string {
        auto it = item.FindMember(key);
        if (it == item.MemberEnd()) {
            return {};
        }
        return ValueToString(it->value);
    };
    auto getInt64 = [&](const char* key) -> int64_t {
        auto it = item.FindMember(key);
        if (it == item.MemberEnd()) {
            return 0;
        }
        return ValueToInt64(it->value);
    };
    rapidjson::Document metadataDoc;
    const rapidjson::Value* manifestNode = nullptr;
    auto metadataIt = item.FindMember("metadata");
    if (metadataIt != item.MemberEnd()) {
        if (metadataIt->value.IsObject()) {
            manifestNode = &metadataIt->value;
        } else if (metadataIt->value.IsString()) {
            metadataDoc.Parse(metadataIt->value.GetString());
            if (!metadataDoc.HasParseError() && metadataDoc.IsObject()) {
                manifestNode = &metadataDoc;
            }
        }
    }
    auto manifestString = [&](const char* key) -> std::string {
        if (!manifestNode || !manifestNode->HasMember(key)) {
            return {};
        }
        return ValueToString((*manifestNode)[key]);
    };
    auto manifestInt = [&](const char* key) -> int64_t {
        if (!manifestNode || !manifestNode->HasMember(key)) {
            return 0;
        }
        return ValueToInt64((*manifestNode)[key]);
    };

    summary->dataset_id = getString("dataset_id");
    summary->dataset_slug = getString("dataset_slug");
    summary->symbol = getString("symbol");
    summary->granularity = getString("granularity");
    summary->source = manifestString("source");
    summary->bar_interval_ms = getInt64("bar_interval_ms");
    if (summary->bar_interval_ms == 0) {
        summary->bar_interval_ms = manifestInt("bar_interval_ms");
    }
    summary->lookback_rows = getInt64("lookback_rows");
    if (summary->lookback_rows == 0) {
        summary->lookback_rows = manifestInt("lookback_rows");
    }
    summary->first_ohlcv_ts_ms = getInt64("first_ohlcv_ts");
    if (summary->first_ohlcv_ts_ms == 0) {
        summary->first_ohlcv_ts_ms = manifestInt("first_ohlcv_timestamp_ms");
    }
    summary->first_indicator_ts_ms = getInt64("first_indicator_ts");
    if (summary->first_indicator_ts_ms == 0) {
        summary->first_indicator_ts_ms = manifestInt("first_indicator_timestamp_ms");
    }
    summary->ohlcv_row_count = manifestInt("ohlcv_rows");
    if (summary->ohlcv_row_count == 0) {
        summary->ohlcv_row_count = getInt64("ohlcv_row_count");
    }
    summary->indicator_row_count = manifestInt("indicator_rows");
    if (summary->indicator_row_count == 0) {
        summary->indicator_row_count = getInt64("indicator_row_count");
    }
    summary->ohlcv_measurement = manifestString("ohlcv_measurement");
    summary->indicator_measurement = manifestString("indicator_measurement");
    if (summary->ohlcv_measurement.empty()) {
        summary->ohlcv_measurement = getString("ohlcv_measurement");
    }
    if (summary->indicator_measurement.empty()) {
        summary->indicator_measurement = getString("indicator_measurement");
    }
    if (summary->ohlcv_measurement.empty()) {
        summary->ohlcv_measurement = DefaultMeasurement(summary->dataset_slug, "ohlcv");
    }
    if (summary->indicator_measurement.empty()) {
        summary->indicator_measurement = DefaultMeasurement(summary->dataset_slug, "ind");
    }
    summary->ohlcv_first_ts = summary->first_ohlcv_ts_ms > 0
        ? FormatIsoTimestamp(summary->first_ohlcv_ts_ms)
        : getString("ohlcv_first_ts");
    int64_t lastOhlcvMs = manifestInt("last_ohlcv_timestamp_ms");
    summary->ohlcv_last_ts = getString("ohlcv_last_ts");
    if (summary->ohlcv_last_ts.empty() && lastOhlcvMs > 0) {
        summary->ohlcv_last_ts = FormatIsoTimestamp(lastOhlcvMs);
    }
    summary->indicator_first_ts = summary->first_indicator_ts_ms > 0
        ? FormatIsoTimestamp(summary->first_indicator_ts_ms)
        : getString("indicator_first_ts");
    int64_t lastIndicatorMs = manifestInt("last_indicator_timestamp_ms");
    summary->indicator_last_ts = getString("indicator_last_ts");
    if (summary->indicator_last_ts.empty() && lastIndicatorMs > 0) {
        summary->indicator_last_ts = FormatIsoTimestamp(lastIndicatorMs);
    }
    summary->run_count = getInt64("run_count");
    summary->simulation_count = getInt64("simulation_count");
    summary->updated_at = manifestString("exported_at");
    if (summary->updated_at.empty()) {
        summary->updated_at = getString("updated_at");
    }
    return true;
}

} // namespace

namespace stage1 {

RestClient& RestClient::Instance() {
    static RestClient client;
    return client;
}

RestClient::RestClient() {
    const char* env = std::getenv(kBaseUrlEnv);
    m_baseUrl = env && *env ? env : kDefaultBaseUrl;
    const char* token = std::getenv(kTokenEnv);
    if (token && *token) {
        m_apiToken = token;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

RestClient::~RestClient() {
    curl_global_cleanup();
}

void RestClient::SetBaseUrl(const std::string& url) {
    if (!url.empty()) {
        m_baseUrl = url;
    }
}

void RestClient::SetApiToken(const std::string& token) {
    m_apiToken = token;
}

bool RestClient::FetchDatasets(int limit,
                               int offset,
                               std::vector<DatasetSummary>* datasets,
                               std::string* error) {
    if (!datasets) {
        if (error) *error = "Dataset container is null.";
        return false;
    }
    std::ostringstream path;
    path << "/api/datasets?limit=" << limit << "&offset=" << offset;
    long status = 0;
    std::string response;
    if (!Execute("GET", path.str(), "", {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "Stage1 API returned HTTP " << status << " while listing datasets.";
            if (!response.empty()) {
                msg << " Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }
    return ParseDatasets(response, datasets, error);
}

bool RestClient::FetchDatasetRuns(const std::string& datasetId,
                                  int limit,
                                  int offset,
                                  std::vector<RunSummary>* runs,
                                  std::string* error) {
    if (!runs) {
        if (error) *error = "Run container is null.";
        return false;
    }
    if (datasetId.empty()) {
        if (error) *error = "Dataset ID is required.";
        return false;
    }
    std::ostringstream path;
    path << "/api/datasets/" << datasetId << "/runs?limit=" << limit << "&offset=" << offset;
    long status = 0;
    std::string response;
    if (!Execute("GET", path.str(), "", {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "Stage1 API returned HTTP " << status << " while listing runs.";
            if (!response.empty()) {
                msg << " Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }
    return ParseDatasetRuns(response, runs, error);
}

bool RestClient::FetchRunDetail(const std::string& runId,
                                RunDetail* detail,
                                std::string* error) {
    if (!detail) {
        if (error) *error = "Detail container is null.";
        return false;
    }
    if (runId.empty()) {
        if (error) *error = "Run ID is required.";
        return false;
    }
    auto fetchFromPath = [&](const std::string& path, RunDetail* out, std::string* err, long* statusOut) -> bool {
        long status = 0;
        std::string response;
        if (!Execute("GET", path, "", {}, &status, &response, err)) {
            return false;
        }
        if (statusOut) {
            *statusOut = status;
        }
        if (status == 404) {
            if (err) *err = "Run not found.";
            return false;
        }
        if (status < 200 || status >= 300) {
            if (err) {
                std::ostringstream msg;
                msg << "Stage1 API returned HTTP " << status << " for " << path;
                if (!response.empty()) {
                    msg << " Body: " << response;
                }
                *err = msg.str();
            }
            return false;
        }
        return ParseRunDetail(response, out, err);
    };

    long primaryStatus = 0;
    RunDetail primary;
    if (!fetchFromPath("/api/runs/" + runId, &primary, error, &primaryStatus)) {
        return false;
    }

    if (primary.folds.empty()) {
        std::cerr << "[Stage1RestClient] /api/runs/" << runId
                  << " returned zero folds; retrying via /api/walkforward/runs/"
                  << runId << std::endl;
        std::string fallbackError;
        RunDetail fallback;
        if (fetchFromPath("/api/walkforward/runs/" + runId, &fallback, &fallbackError, nullptr)
            && !fallback.folds.empty()) {
            *detail = std::move(fallback);
            return true;
        }
        std::cerr << "[Stage1RestClient] Fallback /api/walkforward/runs/" << runId
                  << " failed: " << fallbackError << std::endl;
    }

    *detail = std::move(primary);
    return true;
}

bool RestClient::SubmitQuestDbImport(const std::string& measurement,
                                     const std::string& csvData,
                                     const std::string& filenameHint,
                                     std::string* jobId,
                                     std::string* error) {
    if (measurement.empty() || csvData.empty()) {
        if (error) *error = "Measurement and CSV data are required.";
        return false;
    }
    std::ostringstream body;
    body << "{"
         << "\"measurement\":" << JsonEscape(measurement) << ","
         << "\"data\":" << JsonEscape(csvData) << ",";
    std::string fileLabel = filenameHint.empty() ? measurement + ".csv" : filenameHint;
    body << "\"filename\":" << JsonEscape(fileLabel) << "}";

    long status = 0;
    std::string response;
    if (!Execute("POST", "/api/questdb/import/async", body.str(), {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "QuestDB import request failed with HTTP " << status << ".";
            if (!response.empty()) {
                msg << " Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }

    rapidjson::Document doc;
    doc.Parse(response.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse QuestDB import response.";
        return false;
    }
    const auto jobIter = doc.FindMember("job_id");
    if (jobIter == doc.MemberEnd() || !jobIter->value.IsString()) {
        if (error) *error = "QuestDB import response missing job_id.";
        return false;
    }
    if (jobId) {
        *jobId = jobIter->value.GetString();
    }
    return true;
}

bool RestClient::GetJobStatus(const std::string& jobId,
                              JobStatus* statusOut,
                              std::string* error) {
    if (!statusOut) {
        if (error) *error = "Job status container is null.";
        return false;
    }
    if (jobId.empty()) {
        if (error) *error = "Job ID is required.";
        return false;
    }
    std::string path = "/api/jobs/" + jobId;
    long statusCode = 0;
    std::string response;
    if (!Execute("GET", path, "", {}, &statusCode, &response, error)) {
        return false;
    }
    if (statusCode == 404) {
        if (error) *error = "Job not found.";
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "Stage1 API returned HTTP " << statusCode << " while fetching job status.";
            if (!response.empty()) {
                msg << " Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }
    return ParseJob(response, statusOut, error);
}

bool RestClient::PostJson(const std::string& path,
                          const std::string& body,
                          long* httpStatus,
                          std::string* responseBody,
                          std::string* error) {
    return Execute("POST", path, body, {}, httpStatus, responseBody, error);
}

bool RestClient::QuestDbQuery(const std::string& sql,
                              std::vector<std::string>* columns,
                              std::vector<std::vector<std::string>>* rows,
                              std::string* error) {
    if (!columns || !rows) {
        if (error) *error = "Result buffers are null.";
        return false;
    }
    if (sql.empty()) {
        if (error) *error = "SQL query is required.";
        return false;
    }
    std::string body = std::string("{\"sql\":") + JsonEscape(sql) + "}";
    long status = 0;
    std::string response;
    if (!Execute("POST", "/api/questdb/query", body, {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "QuestDB query failed with HTTP " << status;
            if (!response.empty()) {
                msg << ". Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }

    rapidjson::Document doc;
    doc.Parse(response.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse QuestDB response.";
        return false;
    }

    const auto columnsIt = doc.FindMember("columns");
    const auto dataIt = doc.FindMember("dataset");
    if (columnsIt == doc.MemberEnd() || !columnsIt->value.IsArray()) {
        if (error) *error = "QuestDB response missing columns array.";
        return false;
    }
    columns->clear();
    for (const auto& col : columnsIt->value.GetArray()) {
        if (col.IsString()) {
            columns->push_back(col.GetString());
        } else {
            columns->push_back(ValueToJsonString(col));
        }
    }

    rows->clear();
    if (dataIt != doc.MemberEnd() && dataIt->value.IsArray()) {
        for (const auto& entry : dataIt->value.GetArray()) {
            if (!entry.IsArray()) {
                continue;
            }
            std::vector<std::string> row;
            row.reserve(entry.GetArray().Size());
            for (const auto& cell : entry.GetArray()) {
                row.push_back(ValueToString(cell));
            }
            rows->push_back(std::move(row));
        }
    }
    return true;
}

bool RestClient::FetchDataset(const std::string& datasetId,
                              DatasetSummary* summary,
                              std::string* error) {
    if (!summary) {
        if (error) *error = "Dataset summary pointer is null.";
        return false;
    }
    if (datasetId.empty()) {
        if (error) *error = "dataset_id is required.";
        return false;
    }
    long status = 0;
    std::string response;
    if (!Execute("GET", "/api/datasets/" + datasetId, "", {}, &status, &response, error)) {
        return false;
    }
    if (status == 404) {
        if (error) *error = "Dataset not found";
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "Fetch dataset failed with HTTP " << status;
            if (!response.empty()) {
                msg << ": " << response;
            }
            *error = msg.str();
        }
        return false;
    }
    rapidjson::Document doc;
    doc.Parse(response.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse dataset JSON.";
        return false;
    }
    if (!ParseDatasetSummaryNode(doc, summary)) {
        if (error) *error = "Dataset payload missing required fields.";
        return false;
    }
    return true;
}

bool RestClient::AppendDatasetRows(const std::string& datasetId,
                                   const Json::Value& payload,
                                   AppendTarget target,
                                   std::string* error) {
    if (datasetId.empty()) {
        if (error) *error = "dataset_id is required for append.";
        return false;
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, payload);
    std::string path = "/api/datasets/" + datasetId;
    path += (target == AppendTarget::Ohlcv) ? "/ohlcv/append" : "/indicators/append";
    long status = 0;
    std::string response;
    if (!Execute("POST", path, body, {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream oss;
            oss << "Stage1 append failed with HTTP " << status;
            if (!response.empty()) {
                oss << ": " << response;
            }
            *error = oss.str();
        }
        return false;
    }
    return true;
}

bool RestClient::CreateOrUpdateDataset(const std::string& datasetId,
                                       const std::string& datasetSlug,
                                       const std::string& granularity,
                                       int64_t barIntervalMs,
                                       int64_t lookbackRows,
                                       int64_t firstOhlcvTs,
                                       int64_t firstIndicatorTs,
                                       const std::string& metadataJson,
                                       std::string* error) {
    if (datasetId.empty()) {
        if (error) *error = "dataset_id is required.";
        return false;
    }
    if (datasetSlug.empty()) {
        if (error) *error = "dataset_slug is required.";
        return false;
    }

    Json::Value payload;
    payload["dataset_id"] = datasetId;
    payload["dataset_slug"] = datasetSlug;
    payload["granularity"] = granularity;
    payload["bar_interval_ms"] = Json::Int64(barIntervalMs);
    payload["lookback_rows"] = Json::Int64(lookbackRows);
    payload["first_ohlcv_ts"] = Json::Int64(firstOhlcvTs);
    payload["first_indicator_ts"] = Json::Int64(firstIndicatorTs);

    // Include metadata if provided
    if (!metadataJson.empty()) {
        payload["metadata"] = metadataJson;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, payload);

    std::cout << "[RestClient] POST /api/datasets" << std::endl;
    std::cout << "[RestClient]   URL: " << m_baseUrl << "/api/datasets" << std::endl;
    std::cout << "[RestClient]   Payload size: " << body.size() << " bytes" << std::endl;

    long status = 0;
    std::string response;
    if (!Execute("POST", "/api/datasets", body, {}, &status, &response, error)) {
        std::cout << "[RestClient] Execute failed: " << (error ? *error : "unknown") << std::endl;
        return false;
    }

    std::cout << "[RestClient] Response: HTTP " << status << std::endl;
    if (!response.empty()) {
        std::cout << "[RestClient] Body: " << response << std::endl;
    }

    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream oss;
            oss << "Failed to create/update dataset with HTTP " << status;
            if (!response.empty()) {
                oss << ": " << response;
            }
            *error = oss.str();
        }
        return false;
    }

    return true;
}

bool RestClient::ListMeasurements(const std::string& prefix,
                                  std::vector<MeasurementInfo>* measurements,
                                  std::string* error) {
    if (!measurements) {
        if (error) *error = "Measurement container is null.";
        return false;
    }
    std::string path = "/api/questdb/measurements";
    if (!prefix.empty()) {
        path += "?prefix=" + prefix;
    }
    long status = 0;
    std::string response;
    if (!Execute("GET", path, "", {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "List measurements failed with HTTP " << status;
            if (!response.empty()) {
                msg << ". Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }

    rapidjson::Document doc;
    doc.Parse(response.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse measurement list JSON.";
        return false;
    }
    const auto listIt = doc.FindMember("measurements");
    if (listIt == doc.MemberEnd() || !listIt->value.IsArray()) {
        if (error) *error = "Measurement response missing array.";
        return false;
    }
    measurements->clear();
    for (const auto& item : listIt->value.GetArray()) {
        if (!item.IsObject()) continue;
        MeasurementInfo info;
        auto getString = [&](const char* key) -> std::string {
            auto it = item.FindMember(key);
            if (it == item.MemberEnd()) {
                return {};
            }
            return ValueToString(it->value);
        };
        info.name = getString("name");
        info.designated_timestamp = getString("designatedTimestamp");
        info.partition_by = getString("partitionBy");
        auto rowCountIt = item.FindMember("row_count");
        if (rowCountIt != item.MemberEnd()) {
            info.row_count = ValueToInt64(rowCountIt->value);
        } else {
            auto alt = item.FindMember("rowCount");
            if (alt != item.MemberEnd()) {
                info.row_count = ValueToInt64(alt->value);
            }
        }
        info.first_ts = getString("first_ts");
        if (info.first_ts.empty()) {
            info.first_ts = getString("firstTimestamp");
        }
        info.last_ts = getString("last_ts");
        if (info.last_ts.empty()) {
            info.last_ts = getString("lastTimestamp");
        }
        measurements->push_back(std::move(info));
    }
    return true;
}

bool RestClient::FetchJobs(int limit,
                           int offset,
                           std::vector<JobStatus>* jobs,
                           std::string* error) {
    if (!jobs) {
        if (error) *error = "Job container is null.";
        return false;
    }
    std::ostringstream path;
    path << "/api/jobs?limit=" << limit << "&offset=" << offset;
    long status = 0;
    std::string response;
    if (!Execute("GET", path.str(), "", {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "List jobs failed with HTTP " << status;
            if (!response.empty()) {
                msg << ". Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }

    rapidjson::Document doc;
    doc.Parse(response.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse job list JSON.";
        return false;
    }
    const auto jobsIt = doc.FindMember("jobs");
    if (jobsIt == doc.MemberEnd() || !jobsIt->value.IsArray()) {
        if (error) *error = "Job list response missing jobs array.";
        return false;
    }
    jobs->clear();
    for (const auto& entry : jobsIt->value.GetArray()) {
        JobStatus job;
        if (PopulateJobStatus(entry, &job)) {
            jobs->push_back(std::move(job));
        }
    }
    return true;
}

bool RestClient::GetHealth(std::string* payload,
                           std::string* error) {
    long status = 0;
    std::string response;
    if (!Execute("GET", "/api/health", "", {}, &status, &response, error)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error) {
            std::ostringstream msg;
            msg << "Health check failed with HTTP " << status;
            if (!response.empty()) {
                msg << ". Body: " << response;
            }
            *error = msg.str();
        }
        return false;
    }
    if (payload) {
        *payload = response;
    }
    return true;
}

bool RestClient::Execute(const std::string& method,
                         const std::string& path,
                         const std::string& body,
                         const std::vector<std::string>& extraHeaders,
                         long* httpStatus,
                         std::string* responseBody,
                         std::string* error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error) *error = "Failed to initialize CURL.";
        return false;
    }

    std::string fullUrl = m_baseUrl;
    if (!path.empty() && path.front() != '/') {
        fullUrl.push_back('/');
    }
    fullUrl += path;

    curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!m_apiToken.empty()) {
        std::string tokenHeader = "X-Stage1-Token: " + m_apiToken;
        headers = curl_slist_append(headers, tokenHeader.c_str());
    }
    for (const auto& hdr : extraHeaders) {
        headers = curl_slist_append(headers, hdr.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    } else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
        }
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (error) *error = curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (httpStatus) {
        *httpStatus = status;
    }
    if (responseBody) {
        *responseBody = std::move(response);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return true;
}

bool RestClient::ParseDatasets(const std::string& json,
                               std::vector<DatasetSummary>* datasets,
                               std::string* error) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse dataset list JSON.";
        return false;
    }
    const auto datasetsIt = doc.FindMember("datasets");
    if (datasetsIt == doc.MemberEnd() || !datasetsIt->value.IsArray()) {
        if (error) *error = "Dataset list response missing 'datasets' array.";
        return false;
    }
    datasets->clear();
    for (const auto& item : datasetsIt->value.GetArray()) {
        DatasetSummary summary;
        if (ParseDatasetSummaryNode(item, &summary)) {
            datasets->push_back(std::move(summary));
        }
    }
    return true;
}

bool RestClient::ParseDatasetRuns(const std::string& json,
                                  std::vector<RunSummary>* runs,
                                  std::string* error) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse dataset runs JSON.";
        return false;
    }
    const auto runsIt = doc.FindMember("runs");
    if (runsIt == doc.MemberEnd() || !runsIt->value.IsArray()) {
        if (error) *error = "Run list response missing 'runs' array.";
        return false;
    }
    runs->clear();
    for (const auto& item : runsIt->value.GetArray()) {
        if (!item.IsObject()) continue;
        RunSummary summary;
        auto getString = [&](const char* key) -> std::string {
            auto it = item.FindMember(key);
            if (it == item.MemberEnd()) {
                return {};
            }
            return ValueToString(it->value);
        };
        summary.run_id = getString("run_id");
        summary.dataset_id = getString("dataset_id");
        summary.dataset_slug = getString("dataset_slug");
        summary.prediction_measurement = getString("prediction_measurement");
        summary.status = getString("status");
        summary.started_at = getString("started_at");
        summary.completed_at = getString("completed_at");
        runs->push_back(std::move(summary));
    }
    return true;
}

bool RestClient::ParseRunDetail(const std::string& json,
                                RunDetail* detail,
                                std::string* error) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse run detail JSON.";
        return false;
    }

    const rapidjson::Value* runNode = &doc;
    const auto runIt = doc.FindMember("run");
    if (runIt != doc.MemberEnd() && runIt->value.IsObject()) {
        runNode = &runIt->value;
    }

    auto getString = [&](const char* key) -> std::string {
        if (runNode->IsObject()) {
            auto it = runNode->FindMember(key);
            if (it != runNode->MemberEnd()) {
                return ValueToString(it->value);
            }
        }
        auto it = doc.FindMember(key);
        if (it != doc.MemberEnd()) {
            return ValueToString(it->value);
        }
        return {};
    };

    auto getJson = [&](const char* key) -> std::string {
        if (runNode->IsObject()) {
            auto it = runNode->FindMember(key);
            if (it != runNode->MemberEnd()) {
                return ValueToJsonString(it->value);
            }
        }
        auto it = doc.FindMember(key);
        if (it != doc.MemberEnd()) {
            return ValueToJsonString(it->value);
        }
        return "{}";
    };

    detail->run_id = getString("run_id");
    detail->dataset_id = getString("dataset_id");
    detail->dataset_slug = getString("dataset_slug");
    detail->prediction_measurement = getString("prediction_measurement");
    detail->target_column = getString("target_column");
    detail->status = getString("status");
    detail->started_at = getString("started_at");
    detail->completed_at = getString("completed_at");
    detail->hyperparameters_json = getJson("hyperparameters");
    detail->walk_config_json = getJson("walk_config");
    detail->summary_metrics_json = getJson("summary_metrics");

    detail->feature_columns.clear();
    const rapidjson::Value* featureArray = nullptr;
    if (runNode->IsObject()) {
        auto featureIt = runNode->FindMember("feature_columns");
        if (featureIt != runNode->MemberEnd()) {
            featureArray = &featureIt->value;
        }
    }
    if (!featureArray) {
        auto featureIt = doc.FindMember("feature_columns");
        if (featureIt != doc.MemberEnd()) {
            featureArray = &featureIt->value;
        }
    }
    if (featureArray && featureArray->IsArray()) {
        for (const auto& entry : featureArray->GetArray()) {
            if (entry.IsString()) {
                detail->feature_columns.emplace_back(entry.GetString());
            }
        }
    }

    detail->folds.clear();
    const rapidjson::Value* foldsNode = nullptr;
    if (doc.HasMember("folds") && doc["folds"].IsArray()) {
        foldsNode = &doc["folds"];
    } else if (runNode->IsObject()) {
        auto foldsIt = runNode->FindMember("folds");
        if (foldsIt != runNode->MemberEnd() && foldsIt->value.IsArray()) {
            foldsNode = &foldsIt->value;
        }
    }
    if (foldsNode) {
        if (!foldsNode->IsArray()) {
            std::cerr << "[Stage1RestClient] 'folds' value is not an array (type="
                      << static_cast<int>(foldsNode->GetType()) << ") for run "
                      << detail->run_id << std::endl;
        } else if (foldsNode->Empty()) {
            std::cerr << "[Stage1RestClient] 'folds' array is empty for run "
                      << detail->run_id << std::endl;
        }
        for (const auto& entry : foldsNode->GetArray()) {
            if (!entry.IsObject()) {
                continue;
            }
            FoldDetail fold;
            auto getFoldInt = [&](std::initializer_list<const char*> keys) -> int {
                for (const char* key : keys) {
                    auto it = entry.FindMember(key);
                    if (it == entry.MemberEnd()) continue;
                    if (it->value.IsInt()) return it->value.GetInt();
                    if (it->value.IsInt64()) return static_cast<int>(it->value.GetInt64());
                    if (it->value.IsDouble()) return static_cast<int>(it->value.GetDouble());
                }
                return 0;
            };
            auto getFoldDouble = [&](std::initializer_list<const char*> keys) -> double {
                for (const char* key : keys) {
                    auto it = entry.FindMember(key);
                    if (it == entry.MemberEnd()) continue;
                    if (it->value.IsNumber()) return it->value.GetDouble();
                }
                return 0.0;
            };
            fold.fold_number = getFoldInt({"fold_number"});
            fold.train_start = getFoldInt({"train_start", "train_start_idx"});
            fold.train_end = getFoldInt({"train_end", "train_end_idx"});
            fold.test_start = getFoldInt({"test_start", "test_start_idx"});
            fold.test_end = getFoldInt({"test_end", "test_end_idx"});
            fold.samples_train = getFoldInt({"samples_train"});
            fold.samples_test = getFoldInt({"samples_test"});
            fold.hit_rate = getFoldDouble({"hit_rate"});
            fold.short_hit_rate = getFoldDouble({"short_hit_rate"});
            fold.profit_factor_test = getFoldDouble({"profit_factor_test"});
            fold.long_threshold = getFoldDouble({"long_threshold", "threshold_long", "thresholds_long"});
            fold.short_threshold = getFoldDouble({"short_threshold", "threshold_short", "thresholds_short"});

            std::string thresholds = "{}";
            auto thresholdsIt = entry.FindMember("thresholds");
            if (thresholdsIt != entry.MemberEnd()) {
                thresholds = ValueToJsonString(thresholdsIt->value);
            }
            fold.thresholds_json = std::move(thresholds);

            std::string metrics = "{}";
            auto metricsIt = entry.FindMember("metrics");
            if (metricsIt != entry.MemberEnd()) {
                metrics = ValueToJsonString(metricsIt->value);
            }
            fold.metrics_json = std::move(metrics);

            detail->folds.push_back(std::move(fold));
        }
    }
    if (!foldsNode) {
        std::cerr << "[Stage1RestClient] No 'folds' node in run detail for run "
                  << detail->run_id << " (available keys: ";
        for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
            std::cerr << it->name.GetString();
            if (it + 1 != doc.MemberEnd()) {
                std::cerr << ", ";
            }
        }
        std::cerr << ")" << std::endl;
    }


    return true;
}

bool RestClient::ParseJob(const std::string& json,
                          JobStatus* status,
                          std::string* error) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        if (error) *error = "Failed to parse job JSON.";
        return false;
    }
    if (!PopulateJobStatus(doc, status)) {
        if (error) *error = "Job JSON missing required fields.";
        return false;
    }
    return true;
}

} // namespace stage1

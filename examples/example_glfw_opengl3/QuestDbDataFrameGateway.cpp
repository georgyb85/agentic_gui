#include "QuestDbDataFrameGateway.h"

#include <arrow/table.h>
#include <arrow/scalar.h>

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <iostream>

#if defined(_WIN32)
#    if defined(min)
#        undef min
#    endif
#    if defined(max)
#        undef max
#    endif
#endif

#include "chronosflow.h"
#include "dataframe_io.h"

namespace questdb {
namespace {

std::string GetEnvOrEmpty(const char* key) {
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string();
}

std::string EscapeIdentifier(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == ' ' || ch == ',' || ch == '=') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string EscapeStringValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
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

std::optional<int64_t> ScalarToMillis(const std::shared_ptr<arrow::Scalar>& scalar,
                                      bool coerce_seconds_to_millis) {
    if (!scalar || !scalar->is_valid) {
        return std::nullopt;
    }
    switch (scalar->type->id()) {
        case arrow::Type::INT64: {
            int64_t value = std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value;
            if (coerce_seconds_to_millis && std::llabs(value) < 4'000'000'000LL) {
                value *= 1000;
            }
            return value;
        }
        case arrow::Type::INT32: {
            int64_t value = std::static_pointer_cast<arrow::Int32Scalar>(scalar)->value;
            if (coerce_seconds_to_millis && std::llabs(value) < 4'000'000'000LL) {
                value *= 1000;
            }
            return value;
        }
        case arrow::Type::DOUBLE:
        case arrow::Type::FLOAT: {
            double numeric = (scalar->type->id() == arrow::Type::DOUBLE)
                ? std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value
                : static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value);
            int64_t value = static_cast<int64_t>(std::llround(numeric));
            if (coerce_seconds_to_millis && std::llabs(value) < 4'000'000'000LL) {
                value *= 1000;
            }
            return value;
        }
        case arrow::Type::STRING:
        case arrow::Type::LARGE_STRING: {
            auto text = scalar->ToString();
            if (text.empty()) {
                return std::nullopt;
            }
            bool isNumeric = std::all_of(text.begin(), text.end(), [](unsigned char ch) {
                return std::isdigit(ch) || ch == '-' || ch == '+';
            });
            if (isNumeric) {
                try {
                    int64_t value = std::stoll(text);
                    if (coerce_seconds_to_millis && std::llabs(value) < 4'000'000'000LL) {
                        value *= 1000;
                    }
                    return value;
                } catch (...) {
                }
            }
            return ParseIsoToMillis(text);
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

const char* kTimestampCandidates[] = {
    "timestamp_unix",
    "timestamp",
    "timestamp_seconds",
    "timestamp_unix_s",
    "ts",
    "time"
};

std::string DetectTimestampColumn(const std::shared_ptr<arrow::Schema>& schema,
                                  const std::string& preferred) {
    if (!schema) {
        return {};
    }
    if (!preferred.empty()) {
        return schema->GetFieldIndex(preferred) >= 0 ? preferred : std::string();
    }
    for (const char* candidate : kTimestampCandidates) {
        if (!candidate) continue;
        if (schema->GetFieldIndex(candidate) >= 0) {
            return candidate;
        }
    }
    return {};
}

struct CurlFileContext {
    std::ofstream* stream = nullptr;
    size_t sniff_limit = 4096;
    std::string sniff;
    bool errored = false;
};

size_t CurlWriteToFile(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    if (total == 0) {
        return 0;
    }
    auto* ctx = static_cast<CurlFileContext*>(userp);
    if (!ctx || !ctx->stream || !ctx->stream->good()) {
        return 0;
    }
    ctx->stream->write(static_cast<const char*>(contents), static_cast<std::streamsize>(total));
    if (!ctx->stream->good()) {
        ctx->errored = true;
        return 0;
    }
    if (ctx->sniff.size() < ctx->sniff_limit) {
        const size_t remaining = ctx->sniff_limit - ctx->sniff.size();
        ctx->sniff.append(static_cast<const char*>(contents), std::min(total, remaining));
    }
    return total;
}

std::string ReadFilePrefix(const std::filesystem::path& path, size_t maxBytes) {
    std::string buffer;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return buffer;
    }
    buffer.resize(maxBytes);
    in.read(buffer.data(), static_cast<std::streamsize>(maxBytes));
    buffer.resize(static_cast<size_t>(in.gcount()));
    return buffer;
}

std::string EscapeTagValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == ' ' || ch == ',' || ch == '=' || ch == '\t') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    if (escaped.empty()) {
        escaped = "none";
    }
    return escaped;
}

std::string BuildStaticTagSuffix(const std::map<std::string, std::string>& tags) {
    if (tags.empty()) {
        return {};
    }
    std::ostringstream oss;
    for (const auto& kv : tags) {
        if (kv.first.empty()) {
            continue;
        }
        oss << ',' << EscapeIdentifier(kv.first) << '=' << EscapeTagValue(kv.second);
    }
    return oss.str();
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> cols;
    std::string current;
    bool in_quotes = false;
    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (ch == ',' && !in_quotes) {
            cols.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    cols.push_back(current);
    return cols;
}

} // namespace

DataFrameGateway::DataFrameGateway(ConnectionOptions options)
    : m_options(std::move(options)) {
    if (std::string host = GetEnvOrEmpty("STAGE1_QUESTDB_HOST"); !host.empty()) {
        m_options.ilp_host = host;
    }
    if (std::string port = GetEnvOrEmpty("STAGE1_QUESTDB_ILP_PORT"); !port.empty()) {
        try {
            m_options.ilp_port = std::stoi(port);
        } catch (...) {
        }
    }
    if (std::string rest = GetEnvOrEmpty("STAGE1_QUESTDB_REST"); !rest.empty()) {
        m_options.rest_url = rest;
    }
    if (std::string connect_timeout = GetEnvOrEmpty("STAGE1_QUESTDB_CONNECT_TIMEOUT_MS"); !connect_timeout.empty()) {
        try {
            m_options.connect_timeout_ms = std::max<long>(1000, std::stol(connect_timeout));
        } catch (...) {
        }
    }
    if (std::string request_timeout = GetEnvOrEmpty("STAGE1_QUESTDB_REQUEST_TIMEOUT_MS"); !request_timeout.empty()) {
        try {
            m_options.request_timeout_ms = std::max<long>(1000, std::stol(request_timeout));
        } catch (...) {
        }
    }
    if (std::string send_retry = GetEnvOrEmpty("STAGE1_QUESTDB_SEND_RETRY_MS"); !send_retry.empty()) {
        try {
            m_options.send_retry_window_ms = std::max<long>(1000, std::stol(send_retry));
        } catch (...) {
        }
    }
    if (std::string rest_timeout = GetEnvOrEmpty("STAGE1_QUESTDB_REST_TIMEOUT_MS"); !rest_timeout.empty()) {
        try {
            m_options.rest_timeout_ms = std::max<long>(1000, std::stol(rest_timeout));
        } catch (...) {
        }
    }
}

bool DataFrameGateway::Export(const chronosflow::AnalyticsDataFrame& dataframe,
                              const ExportSpec& spec,
                              ExportResult* result,
                              std::string* error) const {
    auto table = dataframe.get_cpu_table();
    if (!table) {
        if (error) *error = "Dataset is not available on CPU.";
        return false;
    }

    const auto sanitizedMeasurement = spec.measurement.empty() ? "measurement" : spec.measurement;
    int64_t rowsSerialized = 0;

    auto schema = table->schema();
    const int numColumns = schema->num_fields();
    const int64_t numRows = table->num_rows();
    if (numColumns == 0 || numRows == 0) {
        if (error) *error = "Table is empty.";
        return false;
    }

    // Build ILP payload
    std::ostringstream lines;
    lines.setf(std::ios::fixed, std::ios::floatfield);

    std::string timestampColumnName = DetectTimestampColumn(schema, spec.timestamp_column);
    if (timestampColumnName.empty()) {
        if (error) {
            *error = "Dataset is missing a timestamp column (expected one of: timestamp_unix, timestamp, timestamp_seconds, timestamp_unix_s, ts, time).";
        }
        return false;
    }
    int timestampColumnIndex = schema->GetFieldIndex(timestampColumnName);
    if (timestampColumnIndex < 0) {
        if (error) {
            *error = "Timestamp column '" + timestampColumnName + "' not found.";
        }
        return false;
    }
    auto timestampColumn = table->column(timestampColumnIndex);
    if (!timestampColumn) {
        if (error) {
            *error = "Timestamp column '" + timestampColumnName + "' is unavailable.";
        }
        return false;
    }

    const auto measurementEscaped = EscapeIdentifier(sanitizedMeasurement);
    const auto staticTagSuffix = BuildStaticTagSuffix(spec.static_tags);

    struct DynamicTagColumn {
        int index = -1;
        std::shared_ptr<arrow::ChunkedArray> data;
        std::string name;
    };
    std::vector<DynamicTagColumn> dynamicTags;
    std::vector<bool> skipColumn(numColumns, false);
    if (timestampColumnIndex >= 0 && timestampColumnIndex < numColumns) {
        skipColumn[timestampColumnIndex] = true;
    }
    int timestampFieldColumnIndex = -1;
    if (spec.emit_timestamp_field && !spec.timestamp_field_name.empty()) {
        timestampFieldColumnIndex = schema->GetFieldIndex(spec.timestamp_field_name);
        if (timestampFieldColumnIndex >= 0 && timestampFieldColumnIndex < numColumns) {
            skipColumn[timestampFieldColumnIndex] = true;
        }
    }
    for (const auto& tagName : spec.tag_columns) {
        int idx = schema->GetFieldIndex(tagName);
        if (idx < 0 || idx >= numColumns) {
            continue;
        }
        DynamicTagColumn column;
        column.index = idx;
        column.data = table->column(idx);
        column.name = tagName;
        dynamicTags.push_back(std::move(column));
        skipColumn[idx] = true;
    }

    for (int64_t row = 0; row < numRows; ++row) {
        std::ostringstream fields;
        int fieldCount = 0;
        for (int col = 0; col < numColumns; ++col) {
            if ((col == timestampColumnIndex) || (col >= 0 && col < numColumns && skipColumn[col])) {
                continue;
            }

            auto columnData = table->column(col);
            auto scalarResult = columnData->GetScalar(row);
            if (!scalarResult.ok()) {
                continue;
            }
            auto scalar = scalarResult.ValueOrDie();
            if (!scalar || !scalar->is_valid) {
                continue;
            }

            const auto& field = schema->field(col);
            std::string fieldValue;
            switch (field->type()->id()) {
                case arrow::Type::BOOL: {
                    auto boolScalar = std::static_pointer_cast<arrow::BooleanScalar>(scalar);
                    fieldValue = boolScalar->value ? "true" : "false";
                    break;
                }
                case arrow::Type::FLOAT:
                case arrow::Type::DOUBLE: {
                    double numeric = (field->type()->id() == arrow::Type::FLOAT)
                        ? static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value)
                        : std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
                    if (!std::isfinite(numeric)) continue;
                    std::ostringstream valueStream;
                    valueStream << std::setprecision(std::numeric_limits<double>::digits10 + 1) << numeric;
                    fieldValue = valueStream.str();
                    break;
                }
                case arrow::Type::INT8:
                case arrow::Type::INT16:
                case arrow::Type::INT32:
                case arrow::Type::INT64:
                case arrow::Type::UINT8:
                case arrow::Type::UINT16:
                case arrow::Type::UINT32:
                case arrow::Type::UINT64: {
                    fieldValue = scalar->ToString();
                    if (fieldValue.empty()) continue;
                    fieldValue.push_back('i');
                    break;
                }
                case arrow::Type::STRING:
                case arrow::Type::LARGE_STRING: {
                    fieldValue = EscapeStringValue(scalar->ToString());
                    break;
                }
                default: {
                    std::string asString = scalar->ToString();
                    if (asString.empty()) continue;
                    fieldValue = EscapeStringValue(asString);
                    break;
                }
            }

            if (fieldValue.empty()) {
                continue;
            }

            if (fieldCount > 0) {
                fields << ',';
            }
            fields << EscapeIdentifier(field->name()) << '=' << fieldValue;
            ++fieldCount;
        }

        std::ostringstream measurementWithTags;
        measurementWithTags << measurementEscaped << staticTagSuffix;
        for (const auto& tagColumn : dynamicTags) {
            if (!tagColumn.data) {
                continue;
            }
            auto scalarResult = tagColumn.data->GetScalar(row);
            if (!scalarResult.ok()) {
                continue;
            }
            auto scalar = scalarResult.ValueOrDie();
            if (!scalar || !scalar->is_valid) {
                continue;
            }
            measurementWithTags << ',' << EscapeIdentifier(tagColumn.name)
                                << '=' << EscapeTagValue(scalar->ToString());
        }

        auto tsScalarResult = timestampColumn->GetScalar(row);
        if (!tsScalarResult.ok()) {
            if (error) {
                *error = "Failed to read timestamp for row " + std::to_string(row) + ": "
                    + tsScalarResult.status().ToString();
            }
            return false;
        }
        auto tsScalar = tsScalarResult.ValueOrDie();
        auto timestampOpt = ScalarToMillis(tsScalar, spec.coerce_seconds_to_millis);
        if (!timestampOpt || *timestampOpt == 0) {
            if (error) {
                *error = "Row " + std::to_string(row) + " is missing a valid timestamp in column '"
                    + timestampColumnName + "'.";
            }
            return false;
        }
        int64_t timestampMs = *timestampOpt;

        if (spec.emit_timestamp_field && !spec.timestamp_field_name.empty()) {
            if (fieldCount > 0) {
                fields << ',';
            }
            fields << EscapeIdentifier(spec.timestamp_field_name) << '='
                   << timestampMs << 'i';
            ++fieldCount;
        }

        if (fieldCount == 0) {
            continue;
        }

        const int64_t timestampNs = timestampMs * 1000000LL;
        lines << measurementWithTags.str() << ' ' << fields.str() << ' ' << timestampNs << '\n';
        ++rowsSerialized;
    }

    const std::string payload = lines.str();
    if (rowsSerialized == 0 || payload.empty()) {
        if (error) *error = "Nothing to export.";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error) *error = "Failed to initialize CURL.";
        return false;
    }

    std::ostringstream url;
    url << "telnet://" << m_options.ilp_host << ':' << m_options.ilp_port;

    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, m_options.connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, m_options.connect_timeout_ms + m_options.request_timeout_ms);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (error) {
            *error = std::string("QuestDB connection failed: ") + curl_easy_strerror(res);
        }
        curl_easy_cleanup(curl);
        return false;
    }

    const char* buffer = payload.c_str();
    size_t total = payload.size();
    size_t sentTotal = 0;
    const auto retryBudget = std::chrono::milliseconds(m_options.send_retry_window_ms);
    auto deadline = std::chrono::steady_clock::now() + retryBudget;

    while (sentTotal < total) {
        size_t sent = 0;
        res = curl_easy_send(curl, buffer + sentTotal, total - sentTotal, &sent);
        if (res == CURLE_AGAIN) {
            if (std::chrono::steady_clock::now() > deadline) {
                if (error) {
                    *error = "QuestDB send timed out.";
                }
                curl_easy_cleanup(curl);
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (res != CURLE_OK) {
            if (error) {
                *error = std::string("QuestDB send failed: ") + curl_easy_strerror(res);
            }
            curl_easy_cleanup(curl);
            return false;
        }
        if (sent == 0) {
            if (std::chrono::steady_clock::now() > deadline) {
                if (error) {
                    *error = "QuestDB send stalled.";
                }
                curl_easy_cleanup(curl);
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        sentTotal += sent;
        deadline = std::chrono::steady_clock::now() + retryBudget;
    }

    curl_easy_cleanup(curl);
    if (result) {
        result->rows_serialized = rowsSerialized;
    }
    return true;
}

bool DataFrameGateway::Export(const chronosflow::AnalyticsDataFrame& dataframe,
                              const std::string& measurement,
                              ExportResult* result,
                              std::string* error) const {
    ExportSpec spec;
    spec.measurement = measurement;
    return Export(dataframe, spec, result, error);
}

arrow::Result<chronosflow::AnalyticsDataFrame> DataFrameGateway::Import(const ImportSpec& spec) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return arrow::Status::IOError("Failed to initialize CURL for QuestDB fetch.");
    }

    std::string query;
    if (!spec.sql_query.empty()) {
        query = spec.sql_query;
    } else if (!spec.table_name.empty()) {
        query = "SELECT * FROM \"" + spec.table_name + "\"";
    } else {
        curl_easy_cleanup(curl);
        return arrow::Status::Invalid("ImportSpec requires either table_name or sql_query.");
    }

    std::unique_ptr<char, decltype(&curl_free)> encodedQuery(
        curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size())), &curl_free);
    if (!encodedQuery) {
        curl_easy_cleanup(curl);
        return arrow::Status::IOError("Failed to encode QuestDB query.");
    }

    std::string url = m_options.rest_url + "/exp?query=";
    url += encodedQuery.get();
    url += "&fmt=csv";

    auto tempDir = std::filesystem::temp_directory_path();
    auto uniqueSuffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path tempFile = tempDir / ("questdb_import_" + uniqueSuffix + ".csv");

    std::ofstream out(tempFile, std::ios::binary);
    if (!out) {
        curl_easy_cleanup(curl);
        return arrow::Status::IOError("Failed to create temp file for QuestDB import.");
    }

    CurlFileContext ctx;
    ctx.stream = &out;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, m_options.connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, m_options.rest_timeout_ms);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    }
    curl_easy_cleanup(curl);
    out.close();

    auto cleanupTemp = [&]() {
        std::error_code ec;
        std::filesystem::remove(tempFile, ec);
    };

    if (ctx.errored) {
        cleanupTemp();
        return arrow::Status::IOError("QuestDB fetch failed: could not write response.");
    }
    if (res != CURLE_OK) {
        cleanupTemp();
        return arrow::Status::IOError(std::string("QuestDB fetch failed: ") + curl_easy_strerror(res));
    }

    std::error_code sizeEc;
    auto fileSizeResult = std::filesystem::file_size(tempFile, sizeEc);
    if (sizeEc || fileSizeResult == 0) {
        cleanupTemp();
        return arrow::Status::IOError("QuestDB returned empty response.");
    }

    auto ltrim = [](const std::string& text) -> std::string_view {
        size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
            ++start;
        }
        return std::string_view(text).substr(start);
    };

    auto ExtractJsonMessage = [](std::string_view json) -> std::string {
        size_t errorPos = json.find("\"error\"");
        if (errorPos == std::string_view::npos) {
            return std::string(json.substr(0, std::min<size_t>(512, json.size())));
        }
        size_t colon = json.find(':', errorPos);
        if (colon == std::string_view::npos) {
            return std::string(json.substr(0, std::min<size_t>(512, json.size())));
        }
        size_t start = colon + 1;
        while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) {
            ++start;
        }
        bool quoted = start < json.size() && json[start] == '"';
        if (quoted) {
            ++start;
        }
        size_t end = start;
        while (end < json.size()) {
            char ch = json[end];
            if ((quoted && ch == '"') || (!quoted && (ch == '\r' || ch == '\n' || ch == ',' || ch == '}'))) {
                break;
            }
            ++end;
        }
        if (end <= start) {
            return std::string(json.substr(0, std::min<size_t>(512, json.size())));
        }
        return std::string(json.substr(start, end - start));
    };

    std::string peek = ctx.sniff.empty() ? ReadFilePrefix(tempFile, 4096) : ctx.sniff;
    std::string_view trimmed = ltrim(peek);
    if (httpCode >= 400 || (!trimmed.empty() && trimmed.front() == '{')) {
        std::string message;
        if (httpCode >= 400) {
            message = "QuestDB HTTP " + std::to_string(httpCode);
        }
        if (!trimmed.empty() && trimmed.front() == '{') {
            std::string jsonMsg = ExtractJsonMessage(trimmed);
            if (jsonMsg.rfind("QuestDB error:", 0) != 0) {
                jsonMsg = "QuestDB returned JSON instead of CSV: " + jsonMsg;
            }
            if (!message.empty()) {
                message += " - ";
            }
            message += jsonMsg;
        }
        if (message.empty()) {
            message = "QuestDB returned error response.";
        }
        cleanupTemp();
        return arrow::Status::IOError(message);
    }

    chronosflow::TSSBReadOptions options;
    options.auto_detect_delimiter = true;
    options.has_header = true;

    auto dataframeResult = chronosflow::DataFrameIO::read_tssb(tempFile.string(), options);
    cleanupTemp();
    return dataframeResult;
}

arrow::Result<chronosflow::AnalyticsDataFrame> DataFrameGateway::Import(const std::string& table_name) const {
    ImportSpec spec;
    spec.table_name = table_name;
    return Import(spec);
}

} // namespace questdb

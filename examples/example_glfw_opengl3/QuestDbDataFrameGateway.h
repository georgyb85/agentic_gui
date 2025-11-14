#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <arrow/result.h>

namespace chronosflow {
class AnalyticsDataFrame;
}

namespace questdb {

struct ConnectionOptions {
    std::string ilp_host = "45.85.147.236";
    int ilp_port = 9009;
    std::string rest_url = "http://45.85.147.236:9000";
    long connect_timeout_ms = 5000;
    long request_timeout_ms = 15000;
    long send_retry_window_ms = 10000;
    long rest_timeout_ms = 60000;
};

struct ExportResult {
    int64_t rows_serialized = 0;
};

struct ExportSpec {
    std::string measurement;
    std::string timestamp_column = "timestamp_unix";
    bool coerce_seconds_to_millis = true;
    std::map<std::string, std::string> static_tags;
    std::vector<std::string> tag_columns;
    bool emit_timestamp_field = false;
    std::string timestamp_field_name = "timestamp_ms";
};

struct ImportSpec {
    std::string table_name;
    std::string sql_query;
};

class DataFrameGateway {
public:
    explicit DataFrameGateway(ConnectionOptions options = ConnectionOptions());

    bool Export(const chronosflow::AnalyticsDataFrame& dataframe,
                const ExportSpec& spec,
                ExportResult* result,
                std::string* error) const;

    bool Export(const chronosflow::AnalyticsDataFrame& dataframe,
                const std::string& measurement,
                ExportResult* result,
                std::string* error) const;

    arrow::Result<chronosflow::AnalyticsDataFrame> Import(const ImportSpec& spec) const;

    arrow::Result<chronosflow::AnalyticsDataFrame> Import(const std::string& table_name) const;

private:
    ConnectionOptions m_options;
};

} // namespace questdb

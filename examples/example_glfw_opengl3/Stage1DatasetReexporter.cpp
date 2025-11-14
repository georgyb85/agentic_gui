#include "QuestDbDataFrameGateway.h"
#include "Stage1DatasetManifest.h"
#include "stage1_metadata_writer.h"
#include "analytics_dataframe.h"
#include "dataframe_io.h"

#include <arrow/table.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct CliOptions {
    std::string indicator_path;
    std::string ohlcv_path;
    std::string dataset_slug;
    std::string dataset_id;
    std::string indicator_measurement;
    std::string ohlcv_measurement;
    std::string date_column = "Date";
    std::string time_column = "Time";
    std::string time_format = "hhmm";
};

void PrintUsage(const char* exe) {
    std::cout << "Usage: " << exe
              << " --indicator <path> --ohlcv <path> --slug <dataset_slug> [options]\n\n"
              << "Options:\n"
              << "  --dataset-id <uuid>              Optional explicit dataset ID\n"
              << "  --indicator-measurement <name>   QuestDB table for indicators\n"
              << "  --ohlcv-measurement <name>       QuestDB table for OHLCV bars\n"
              << "  --date-column <name>             TSSB date column name (default: Date)\n"
              << "  --time-column <name>             TSSB time column name (default: Time)\n"
              << "  --time-format <hhmm|hhmmss>      TSSB time encoding (default: hhmm)\n";
}

bool ParseArgs(int argc, char** argv, CliOptions* out) {
    if (!out) return false;
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto requireValue = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Flag '" + flag + "' requires a value.");
            }
            return argv[++i];
        };
        if (arg == "--indicator") {
            options.indicator_path = requireValue(arg);
        } else if (arg == "--ohlcv") {
            options.ohlcv_path = requireValue(arg);
        } else if (arg == "--slug") {
            options.dataset_slug = requireValue(arg);
        } else if (arg == "--dataset-id") {
            options.dataset_id = requireValue(arg);
        } else if (arg == "--indicator-measurement") {
            options.indicator_measurement = requireValue(arg);
        } else if (arg == "--ohlcv-measurement") {
            options.ohlcv_measurement = requireValue(arg);
        } else if (arg == "--date-column") {
            options.date_column = requireValue(arg);
        } else if (arg == "--time-column") {
            options.time_column = requireValue(arg);
        } else if (arg == "--time-format") {
            options.time_format = requireValue(arg);
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (options.indicator_path.empty() || options.ohlcv_path.empty() || options.dataset_slug.empty()) {
        PrintUsage(argv[0]);
        throw std::runtime_error("indicator, ohlcv, and slug arguments are required.");
    }

    if (options.indicator_measurement.empty()) {
        options.indicator_measurement = options.dataset_slug + "_ind";
    }
    if (options.ohlcv_measurement.empty()) {
        options.ohlcv_measurement = options.dataset_slug + "_ohlcv";
    }
    if (options.dataset_id.empty()) {
        options.dataset_id = Stage1MetadataWriter::MakeDeterministicUuid(options.dataset_slug);
    }

    *out = options;
    return true;
}

chronosflow::TimeFormat ParseTimeFormat(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (char ch : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lower == "hhmmss") {
        return chronosflow::TimeFormat::HHMMSS;
    }
    return chronosflow::TimeFormat::HHMM;
}

arrow::Result<chronosflow::AnalyticsDataFrame> LoadTssbFrame(const std::string& path,
                                                             const CliOptions& options) {
    chronosflow::TSSBReadOptions readOptions;
    readOptions.auto_detect_delimiter = true;
    readOptions.has_header = true;
    readOptions.date_column = options.date_column;
    readOptions.time_column = options.time_column;

    ARROW_ASSIGN_OR_RAISE(auto frame, chronosflow::DataFrameIO::read_tssb(path, readOptions));
    frame.set_tssb_metadata(options.date_column, options.time_column);
    ARROW_ASSIGN_OR_RAISE(auto withUnix,
                          frame.with_unix_timestamp("timestamp_unix", ParseTimeFormat(options.time_format)));
    return withUnix;
}

std::pair<std::optional<int64_t>, std::optional<int64_t>> ExtractBounds(
    const chronosflow::AnalyticsDataFrame& frame,
    const std::string& columnName) {
    auto table = frame.get_cpu_table();
    if (!table) {
        return {};
    }
    const auto schema = table->schema();
    const int idx = schema ? schema->GetFieldIndex(columnName) : -1;
    if (idx < 0) {
        return {};
    }
    auto column = table->column(idx);
    if (!column) {
        return {};
    }
    std::optional<int64_t> first;
    std::optional<int64_t> last;
    for (const auto& chunk : column->chunks()) {
        auto array = std::static_pointer_cast<arrow::Int64Array>(chunk);
        if (!array) {
            continue;
        }
        if (!first) {
            for (int64_t i = 0; i < array->length(); ++i) {
                if (!array->IsValid(i)) continue;
                first = array->Value(i);
                break;
            }
        }
        for (int64_t i = array->length() - 1; i >= 0; --i) {
            if (!array->IsValid(i)) continue;
            last = array->Value(i);
            break;
        }
    }
    return {first, last};
}

std::optional<int64_t> ComputeIntervalMs(const chronosflow::AnalyticsDataFrame& frame,
                                         const std::string& columnName) {
    auto table = frame.get_cpu_table();
    if (!table) {
        return std::nullopt;
    }
    const auto schema = table->schema();
    const int idx = schema ? schema->GetFieldIndex(columnName) : -1;
    if (idx < 0) {
        return std::nullopt;
    }
    auto column = table->column(idx);
    if (!column) {
        return std::nullopt;
    }
    std::optional<int64_t> previous;
    for (const auto& chunk : column->chunks()) {
        auto array = std::static_pointer_cast<arrow::Int64Array>(chunk);
        if (!array) {
            continue;
        }
        for (int64_t i = 0; i < array->length(); ++i) {
            if (!array->IsValid(i)) {
                continue;
            }
            const int64_t value = array->Value(i);
            if (previous && value > *previous) {
                return value - *previous;
            }
            previous = value;
        }
    }
    return std::nullopt;
}

std::string FormatGranularity(int64_t intervalMs) {
    if (intervalMs <= 0) {
        return "unknown";
    }
    if (intervalMs == 60 * 1000) return "1m";
    if (intervalMs == 5 * 60 * 1000) return "5m";
    if (intervalMs == 15 * 60 * 1000) return "15m";
    if (intervalMs == 60 * 60 * 1000) return "1h";
    if (intervalMs == 4 * 60 * 60 * 1000) return "4h";
    if (intervalMs == 24 * 60 * 60 * 1000) return "1d";
    std::ostringstream oss;
    oss << intervalMs << "ms";
    return oss.str();
}

void ExportFrameToQuestDb(const chronosflow::AnalyticsDataFrame& frame,
                          const std::string& measurement,
                          const std::string& timestampColumn) {
    questdb::DataFrameGateway gateway;
    questdb::ExportSpec spec;
    spec.measurement = measurement;
    spec.timestamp_column = timestampColumn;
    spec.emit_timestamp_field = true;
    spec.timestamp_field_name = "timestamp_ms";
    questdb::ExportResult result;
    std::string error;
    if (!gateway.Export(frame, spec, &result, &error)) {
        throw std::runtime_error("QuestDB export failed for measurement '" + measurement + "': " + error);
    }
    std::cout << "Exported " << result.rows_serialized << " rows to measurement '" << measurement << "'.\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        CliOptions options;
        if (!ParseArgs(argc, argv, &options)) {
            return 0;
        }

        auto indicatorFrameResult = LoadTssbFrame(options.indicator_path, options);
        if (!indicatorFrameResult.ok()) {
            throw std::runtime_error("Failed to load indicator file: " + indicatorFrameResult.status().ToString());
        }
        auto ohlcvFrameResult = LoadTssbFrame(options.ohlcv_path, options);
        if (!ohlcvFrameResult.ok()) {
            throw std::runtime_error("Failed to load OHLCV file: " + ohlcvFrameResult.status().ToString());
        }

        chronosflow::AnalyticsDataFrame indicatorFrame = std::move(indicatorFrameResult).ValueOrDie();
        chronosflow::AnalyticsDataFrame ohlcvFrame = std::move(ohlcvFrameResult).ValueOrDie();

        ExportFrameToQuestDb(indicatorFrame, options.indicator_measurement, "timestamp_unix");
        ExportFrameToQuestDb(ohlcvFrame, options.ohlcv_measurement, "timestamp_unix");

        auto indicatorBounds = ExtractBounds(indicatorFrame, "timestamp_unix");
        auto ohlcvBounds = ExtractBounds(ohlcvFrame, "timestamp_unix");
        auto interval = ComputeIntervalMs(ohlcvFrame, "timestamp_unix").value_or(0);
        const auto exportedAt = std::chrono::system_clock::now();

        stage1::DatasetManifest manifest;
        manifest.dataset_id = options.dataset_id;
        manifest.dataset_slug = options.dataset_slug;
        manifest.symbol = options.dataset_slug;
        manifest.granularity = FormatGranularity(interval);
        manifest.source = "stage1_cli";
        manifest.ohlcv_measurement = options.ohlcv_measurement;
        manifest.indicator_measurement = options.indicator_measurement;
        manifest.bar_interval_ms = interval;
        manifest.ohlcv_rows = ohlcvFrame.num_rows();
        manifest.indicator_rows = indicatorFrame.num_rows();
        manifest.first_ohlcv_timestamp_ms = ohlcvBounds.first.value_or(0);
        manifest.last_ohlcv_timestamp_ms = ohlcvBounds.second.value_or(0);
        manifest.first_indicator_timestamp_ms = indicatorBounds.first.value_or(0);
        manifest.last_indicator_timestamp_ms = indicatorBounds.second.value_or(0);
        manifest.lookback_rows = manifest.ohlcv_rows > manifest.indicator_rows
            ? manifest.ohlcv_rows - manifest.indicator_rows
            : 0;
        manifest.exported_at_iso = stage1::FormatIsoTimestamp(exportedAt);

        Stage1MetadataWriter::DatasetRecord record;
        record.dataset_id = options.dataset_id;
        record.dataset_slug = options.dataset_slug;
        record.symbol = options.dataset_slug;
        record.granularity = manifest.granularity;
        record.source = "stage1_cli";
        record.ohlcv_measurement = options.ohlcv_measurement;
        record.indicator_measurement = options.indicator_measurement;
        record.ohlcv_row_count = manifest.ohlcv_rows;
        record.indicator_row_count = manifest.indicator_rows;
        record.ohlcv_first_timestamp_unix = manifest.first_ohlcv_timestamp_ms;
        record.ohlcv_last_timestamp_unix = manifest.last_ohlcv_timestamp_ms;
        record.indicator_first_timestamp_unix = manifest.first_indicator_timestamp_ms;
        record.indicator_last_timestamp_unix = manifest.last_indicator_timestamp_ms;
        record.metadata_json = manifest.ToJsonString();
        record.created_at = exportedAt;

        std::string metadataError;
        if (!Stage1MetadataWriter::Instance().RecordDatasetExport(record, &metadataError)) {
            throw std::runtime_error(metadataError.empty()
                                         ? "Stage1 metadata registration failed."
                                         : metadataError);
        }

        std::string manifestError;
        if (!stage1::WriteManifestToDirectory(manifest,
                                              std::filesystem::path("docs/fixtures/stage1_3/datasets")
                                                  / options.dataset_slug,
                                              &manifestError)) {
            std::cerr << "Warning: failed to write manifest file: " << manifestError << std::endl;
        }

        std::cout << "Dataset '" << options.dataset_slug << "' re-exported successfully.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "stage1_dataset_reexport error: " << ex.what() << std::endl;
        return 1;
    }
}

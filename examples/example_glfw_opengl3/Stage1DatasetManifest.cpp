#include "Stage1DatasetManifest.h"

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace stage1 {
namespace {

std::string SerializeJson(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, value);
}

bool PopulateManifestFromJson(const Json::Value& root,
                              DatasetManifest* manifest,
                              std::string* error) {
    if (!manifest) {
        if (error) {
            *error = "Manifest pointer is null.";
        }
        return false;
    }
    if (!root.isObject()) {
        if (error) {
            *error = "Manifest JSON must be an object.";
        }
        return false;
    }

    DatasetManifest parsed;
    parsed.version = root.get("version", 1).asInt();
    parsed.dataset_id = root.get("dataset_id", "").asString();
    parsed.dataset_slug = root.get("dataset_slug", "").asString();
    parsed.symbol = root.get("symbol", "").asString();
    parsed.granularity = root.get("granularity", "").asString();
    parsed.source = root.get("source", "").asString();
    parsed.ohlcv_measurement = root.get("ohlcv_measurement", "").asString();
    parsed.indicator_measurement = root.get("indicator_measurement", "").asString();
    parsed.bar_interval_ms = root.get("bar_interval_ms", 0).asInt64();
    parsed.lookback_rows = root.get("lookback_rows", 0).asInt64();
    parsed.first_ohlcv_timestamp_ms = root.get("first_ohlcv_timestamp_ms", 0).asInt64();
    parsed.last_ohlcv_timestamp_ms = root.get("last_ohlcv_timestamp_ms", 0).asInt64();
    parsed.first_indicator_timestamp_ms = root.get("first_indicator_timestamp_ms", 0).asInt64();
    parsed.last_indicator_timestamp_ms = root.get("last_indicator_timestamp_ms", 0).asInt64();
    parsed.ohlcv_rows = root.get("ohlcv_rows", 0).asInt64();
    parsed.indicator_rows = root.get("indicator_rows", 0).asInt64();
    if (root.isMember("exported_at")) {
        parsed.exported_at_iso = root["exported_at"].asString();
    } else if (root.isMember("exported_at_iso")) {
        parsed.exported_at_iso = root["exported_at_iso"].asString();
    }

    *manifest = std::move(parsed);
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace

std::string DatasetManifest::ToJsonString() const {
    Json::Value root(Json::objectValue);
    root["version"] = version;
    root["dataset_id"] = dataset_id;
    root["dataset_slug"] = dataset_slug;
    root["symbol"] = symbol;
    root["granularity"] = granularity;
    root["source"] = source;
    root["ohlcv_measurement"] = ohlcv_measurement;
    root["indicator_measurement"] = indicator_measurement;
    root["bar_interval_ms"] = static_cast<Json::Int64>(bar_interval_ms);
    root["lookback_rows"] = static_cast<Json::Int64>(lookback_rows);
    root["first_ohlcv_timestamp_ms"] = static_cast<Json::Int64>(first_ohlcv_timestamp_ms);
    root["last_ohlcv_timestamp_ms"] = static_cast<Json::Int64>(last_ohlcv_timestamp_ms);
    root["first_indicator_timestamp_ms"] = static_cast<Json::Int64>(first_indicator_timestamp_ms);
    root["last_indicator_timestamp_ms"] = static_cast<Json::Int64>(last_indicator_timestamp_ms);
    root["ohlcv_rows"] = static_cast<Json::Int64>(ohlcv_rows);
    root["indicator_rows"] = static_cast<Json::Int64>(indicator_rows);
    root["exported_at"] = exported_at_iso.empty() ? FormatIsoTimestamp(std::chrono::system_clock::now())
                                                  : exported_at_iso;
    return SerializeJson(root);
}

std::string FormatIsoTimestamp(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool WriteManifestToDirectory(const DatasetManifest& manifest,
                              const std::filesystem::path& directory,
                              std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        if (error) {
            *error = "Unable to create manifest directory '" + directory.string() + "': " + ec.message();
        }
        return false;
    }
    const auto filePath = directory / "manifest.json";
    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        if (error) {
            *error = "Unable to open manifest file '" + filePath.string() + "' for writing.";
        }
        return false;
    }
    out << manifest.ToJsonString();
    if (!out.good()) {
        if (error) {
            *error = "Failed to write manifest file '" + filePath.string() + "'.";
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool ReadManifestFromDirectory(const std::filesystem::path& directory,
                               DatasetManifest* manifest,
                               std::string* error) {
    const auto filePath = directory / "manifest.json";
    return ReadManifestFromFile(filePath, manifest, error);
}

bool ReadManifestFromFile(const std::filesystem::path& file_path,
                          DatasetManifest* manifest,
                          std::string* error) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = "Unable to open manifest file '" + file_path.string() + "' for reading.";
        }
        return false;
    }
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    if (!Json::parseFromStream(builder, in, &root, &errs)) {
        if (error) {
            *error = errs.empty()
                ? "Failed to parse manifest JSON at '" + file_path.string() + "'."
                : errs;
        }
        return false;
    }
    return PopulateManifestFromJson(root, manifest, error);
}

}  // namespace stage1

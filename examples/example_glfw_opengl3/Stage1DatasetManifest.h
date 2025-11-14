#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <chrono>

namespace stage1 {

struct DatasetManifest {
    int version = 1;
    std::string dataset_id;
    std::string dataset_slug;
    std::string symbol;
    std::string granularity;
    std::string source;
    std::string ohlcv_measurement;
    std::string indicator_measurement;
    int64_t bar_interval_ms = 0;
    int64_t lookback_rows = 0;
    int64_t first_ohlcv_timestamp_ms = 0;
    int64_t last_ohlcv_timestamp_ms = 0;
    int64_t first_indicator_timestamp_ms = 0;
    int64_t last_indicator_timestamp_ms = 0;
    int64_t ohlcv_rows = 0;
    int64_t indicator_rows = 0;
    std::string exported_at_iso;

    [[nodiscard]] std::string ToJsonString() const;
};

std::string FormatIsoTimestamp(std::chrono::system_clock::time_point tp);

bool WriteManifestToDirectory(const DatasetManifest& manifest,
                              const std::filesystem::path& directory,
                              std::string* error = nullptr);

bool ReadManifestFromDirectory(const std::filesystem::path& directory,
                               DatasetManifest* manifest,
                               std::string* error = nullptr);

bool ReadManifestFromFile(const std::filesystem::path& file_path,
                          DatasetManifest* manifest,
                          std::string* error = nullptr);

}  // namespace stage1

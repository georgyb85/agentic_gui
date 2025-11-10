#pragma once

#include "analytics_dataframe.h"
#include <arrow/io/interfaces.h> // Required for InputStream
#include <arrow/result.h>
#include <arrow/status.h>  // DIAGNOSTIC: Added missing Status header
#include <string>

namespace chronosflow {

// A custom input stream that normalizes multiple whitespace chars into a single tab
// on the fly, allowing Arrow's CSV reader to parse space-delimited files.
class WhitespaceNormalizingInputStream : public arrow::io::InputStream {
public:
    WhitespaceNormalizingInputStream(std::shared_ptr<arrow::io::InputStream> underlying,
                                     char normalized_delimiter = '\t');

    // --- Implementation of the arrow::io::InputStream interface ---
    bool closed() const override;
    arrow::Result<int64_t> Tell() const override;
    arrow::Result<int64_t> Read(int64_t nbytes, void* out) override;
    arrow::Result<std::shared_ptr<arrow::Buffer>> Read(int64_t nbytes) override;

private:
    arrow::Status Close() override;

    // Internal state for block processing
    std::shared_ptr<arrow::io::InputStream> underlying_;
    std::shared_ptr<arrow::Buffer> read_buffer_; // Buffer for data from underlying stream
    const char* read_ptr_ = nullptr;             // Pointer to current read position in read_buffer_
    const char* read_end_ = nullptr;             // Pointer to the end of read_buffer_
    int64_t pos_ = 0;
    bool in_whitespace_ = false;
    char normalized_delimiter_;
};

struct TSSBReadOptions {
    bool auto_detect_delimiter = true;
    char delimiter = '\0';
    bool has_header = true;
    std::string date_column = "";
    std::string time_column = "";
    
    static TSSBReadOptions Defaults() {
        return TSSBReadOptions{};
    }
};

struct TSSBWriteOptions {
    char delimiter = '\t';
    bool write_header = true;
    
    static TSSBWriteOptions Defaults() {
        return TSSBWriteOptions{};
    }
};

class DataFrameIO {
public:
    static arrow::Result<AnalyticsDataFrame> read_tssb(
        const std::string& file_path,
        const TSSBReadOptions& options = TSSBReadOptions::Defaults());

    static arrow::Status write_tssb(
        const AnalyticsDataFrame& df,
        const std::string& file_path,
        const TSSBWriteOptions& options = TSSBWriteOptions::Defaults());

    static arrow::Result<AnalyticsDataFrame> read_parquet(
        const std::string& file_path);

    static arrow::Status write_parquet(
        const AnalyticsDataFrame& df,
        const std::string& file_path,
        bool use_compression = true);

private:
    static char detect_delimiter(const std::string& sample_line);

    static arrow::Result<std::shared_ptr<arrow::Table>> parse_tssb_stream(
        std::shared_ptr<arrow::io::InputStream> input,
        char delimiter,
        bool has_header);
};

} // namespace chronosflow
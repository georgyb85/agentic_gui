#include "dataframe_io.h"
#include <arrow/io/file.h>
#include <arrow/io/memory.h>
#include <arrow/buffer.h>
#include <arrow/util/macros.h> // For ARROW_RETURN_NOT_OK
#include <arrow/csv/api.h>
#include <arrow/csv/writer.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>
#include <arrow/scalar.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace chronosflow {

// --- Implementation of WhitespaceNormalizingInputStream ---

WhitespaceNormalizingInputStream::WhitespaceNormalizingInputStream(
    std::shared_ptr<arrow::io::InputStream> underlying, char normalized_delimiter)
    : underlying_(std::move(underlying)), 
      normalized_delimiter_(normalized_delimiter) {}

arrow::Status WhitespaceNormalizingInputStream::Close() {
    return underlying_->Close();
}

bool WhitespaceNormalizingInputStream::closed() const {
    return underlying_->closed();
}

arrow::Result<int64_t> WhitespaceNormalizingInputStream::Tell() const {
    return pos_;
}

arrow::Result<std::shared_ptr<arrow::Buffer>> WhitespaceNormalizingInputStream::Read(int64_t nbytes) {
    // Allocate a buffer for the output
    ARROW_ASSIGN_OR_RAISE(auto out_buffer, arrow::AllocateResizableBuffer(nbytes));
    int64_t bytes_read = 0;
    ARROW_ASSIGN_OR_RAISE(bytes_read, Read(nbytes, out_buffer->mutable_data()));
    
    // Resize to actual number of bytes read
    ARROW_RETURN_NOT_OK(out_buffer->Resize(bytes_read));
    return out_buffer;
}

// High-performance block processing for all lines (including header)
arrow::Result<int64_t> WhitespaceNormalizingInputStream::Read(int64_t nbytes, void* out) {
    auto write_ptr = static_cast<char*>(out);
    auto write_end = write_ptr + nbytes;

    while (write_ptr < write_end) {
        if (read_ptr_ == read_end_) {
            ARROW_ASSIGN_OR_RAISE(read_buffer_, underlying_->Read(32768));
            if (read_buffer_->size() == 0) {
                break;
            }
            read_ptr_ = reinterpret_cast<const char*>(read_buffer_->data());
            read_end_ = read_ptr_ + read_buffer_->size();
        }

        while (read_ptr_ < read_end_ && write_ptr < write_end) {
            char c = *read_ptr_++;
            
            // Pass through newlines and carriage returns directly, and reset state
            if (c == '\n' || c == '\r') {
                *write_ptr++ = c;
                in_whitespace_ = false; 
            } else if (c == ' ' || c == '\t') {
                if (!in_whitespace_) {
                    *write_ptr++ = normalized_delimiter_;
                    in_whitespace_ = true;
                }
            } else {
                *write_ptr++ = c;
                in_whitespace_ = false;
            }
        }
    }

    int64_t total_written = write_ptr - static_cast<char*>(out);
    pos_ += total_written;
    return total_written;
}

arrow::Result<AnalyticsDataFrame> DataFrameIO::read_tssb(
    const std::string& file_path,
    const TSSBReadOptions& options) {
    
    ARROW_ASSIGN_OR_RAISE(auto input_file, arrow::io::ReadableFile::Open(file_path));
    
    std::shared_ptr<arrow::io::InputStream> input = input_file;
    
    char delimiter = options.delimiter;
    
    if (options.auto_detect_delimiter) {
        // Read first few lines for detection
        auto buffer_result = input->Read(1024);
        if (!buffer_result.ok()) {
            return buffer_result.status();
        }
        
        auto buffer = buffer_result.ValueOrDie();
        std::string sample(reinterpret_cast<const char*>(buffer->data()), buffer->size());
        
        std::istringstream stream(sample);
        std::string first_line;
        std::getline(stream, first_line);
        
        delimiter = detect_delimiter(first_line);
        
        // Reset file position after reading sample
        ARROW_RETURN_NOT_OK(input_file->Seek(0));
    }
    
    // If space delimiter is detected, wrap the input stream with our normalizer
    if (delimiter == ' ') {
        input = std::make_shared<WhitespaceNormalizingInputStream>(input);
        delimiter = '\t'; // The normalizer outputs tabs, so we tell the CSV parser to expect tabs
    }
    
    // The rest of the function proceeds as before, but with the correct stream
    ARROW_ASSIGN_OR_RAISE(auto table, parse_tssb_stream(input, delimiter, options.has_header));
    
    AnalyticsDataFrame df(std::move(table));
    
    // IMPORTANT: Set metadata ONLY if explicitly provided in options.
    // The library should not guess.
    if (!options.date_column.empty()) {
        df.set_tssb_metadata(options.date_column, options.time_column);
    }
    
    return df;
}

arrow::Status DataFrameIO::write_tssb(
    const AnalyticsDataFrame& df,
    const std::string& file_path,
    const TSSBWriteOptions& options) {
    
    if (df.num_rows() == 0) {
        // It's not an error to write an empty file.
        // Let's create an empty file and return OK.
        std::ofstream empty_file(file_path);
        empty_file.close();
        return arrow::Status::OK();
    }
    
    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        return cpu_df_result.status();
    }
    
    const auto& cpu_df = cpu_df_result.ValueOrDie();
    auto table = cpu_df.get_cpu_table();
    if (!table) {
        return arrow::Status::Invalid("No table data available");
    }

    auto output_file_result = arrow::io::FileOutputStream::Open(file_path);
    if (!output_file_result.ok()) {
        return output_file_result.status();
    }
    auto output_stream = output_file_result.ValueOrDie();
    
    // Use Arrow's optimized, multi-threaded CSV writer. This is orders of magnitude faster.
    auto write_options = arrow::csv::WriteOptions::Defaults();
    write_options.include_header = options.write_header;
    write_options.delimiter = options.delimiter;

    // Write the table to the stream
    ARROW_RETURN_NOT_OK(arrow::csv::WriteCSV(*table, write_options, output_stream.get()));

    return arrow::Status::OK();
}

arrow::Result<AnalyticsDataFrame> DataFrameIO::read_parquet(
    const std::string& file_path) {
    
    auto input_file = arrow::io::ReadableFile::Open(file_path);
    if (!input_file.ok()) {
        return input_file.status();
    }
    
    // DIAGNOSTIC: Fixed Arrow 21.0.0 parquet API - use correct signature with memory pool
    auto parquet_reader_result = parquet::arrow::OpenFile(input_file.ValueOrDie(), arrow::default_memory_pool());
    if (!parquet_reader_result.ok()) {
        return parquet_reader_result.status();
    }
    
    auto parquet_reader = std::move(parquet_reader_result.ValueOrDie());
    std::shared_ptr<arrow::Table> table;
    auto read_status = parquet_reader->ReadTable(&table);
    if (!read_status.ok()) {
        return read_status;
    }
    
    // DIAGNOSTIC: Fixed constructor issue - use explicit constructor
    return AnalyticsDataFrame(std::move(table));
}

arrow::Status DataFrameIO::write_parquet(
    const AnalyticsDataFrame& df,
    const std::string& file_path,
    bool use_compression) {
    
    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        return cpu_df_result.status();
    }
    
    // DIAGNOSTIC: Fixed copy constructor issue - use reference
    const auto& cpu_df = cpu_df_result.ValueOrDie();
    
    if (cpu_df.num_rows() == 0) {
        return arrow::Status::Invalid("DataFrame is empty");
    }
    
    auto output_file = arrow::io::FileOutputStream::Open(file_path);
    if (!output_file.ok()) {
        return output_file.status();
    }
    
    parquet::WriterProperties::Builder props_builder;
    if (use_compression) {
        props_builder.compression(parquet::Compression::SNAPPY);
    } else {
        props_builder.compression(parquet::Compression::UNCOMPRESSED);
    }
    
    auto writer_properties = props_builder.build();
    auto arrow_writer_properties = parquet::ArrowWriterProperties::Builder().build();
    
    auto table = cpu_df.get_cpu_table();
    if (!table) {
        return arrow::Status::Invalid("No table data available");
    }
    
    auto status = parquet::arrow::WriteTable(
        *table, 
        arrow::default_memory_pool(),
        output_file.ValueOrDie(),
        table->num_rows(),
        writer_properties,
        arrow_writer_properties);
    
    return status;
}

// A helper to split a string by whitespace, respecting that multiple spaces are one delimiter.
static std::vector<std::string> split_by_whitespace(const std::string& str) {
    std::istringstream iss(str);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

char DataFrameIO::detect_delimiter(const std::string& sample_line) {
    std::vector<char> delimiters = {'\t', ',', ';', '|', ' '};
    std::vector<int> counts(delimiters.size(), 0);
    
    // For non-space delimiters, count normally
    for (size_t i = 0; i < delimiters.size() - 1; ++i) {
        counts[i] = std::count(sample_line.begin(), sample_line.end(), delimiters[i]);
    }

    // A simpler way to count space delimiters: count transitions from non-space to space
    int space_delimiters = 0;
    if (!sample_line.empty()) {
        for (size_t i = 1; i < sample_line.length(); ++i) {
            if ((sample_line[i] == ' ' || sample_line[i] == '\t') && 
                (sample_line[i-1] != ' ' && sample_line[i-1] != '\t')) {
                space_delimiters++;
            }
        }
    }
    counts[4] = space_delimiters;
    
    auto max_it = std::max_element(counts.begin(), counts.end());
    if (max_it == counts.end() || *max_it == 0) {
        return '\t'; // Default
    }
    
    return delimiters[std::distance(counts.begin(), max_it)];
}


arrow::Result<std::shared_ptr<arrow::Table>> DataFrameIO::parse_tssb_stream(
    std::shared_ptr<arrow::io::InputStream> input,
    char delimiter,
    bool has_header) {
    
    arrow::csv::ReadOptions read_options = arrow::csv::ReadOptions::Defaults();
    read_options.use_threads = true;
    read_options.autogenerate_column_names = !has_header;
    
    arrow::csv::ParseOptions parse_options = arrow::csv::ParseOptions::Defaults();
    parse_options.delimiter = delimiter;
    
    arrow::csv::ConvertOptions convert_options = arrow::csv::ConvertOptions::Defaults();
    convert_options.include_missing_columns = has_header;
    
    auto reader_result = arrow::csv::TableReader::Make(
        arrow::io::default_io_context(),
        input,
        read_options,
        parse_options,
        convert_options);
    
    if (!reader_result.ok()) {
        return reader_result.status();
    }
    
    auto reader = reader_result.ValueOrDie();
    return reader->Read();
}


} // namespace chronosflow
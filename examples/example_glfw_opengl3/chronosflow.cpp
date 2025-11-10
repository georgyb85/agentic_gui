#include "chronosflow.h"
#include <iostream>
#include <vector>
#include <iomanip>      // For std::setw, std::setfill, etc.
#include <algorithm>    // For std::min
#include <arrow/scalar.h> // Required for arrow::Scalar
#include <arrow/compute/api.h> // Required for arrow::compute functions
#include <arrow/compute/registry.h> // For function registration
#include <arrow/compute/initialize.h> // Required for Initialize()
#include <arrow/api.h> // Full Arrow API

#ifdef _DEBUG
    #pragma comment(lib, "arrow.lib")
    #pragma comment(lib, "arrow_compute.lib")
    #pragma comment(lib, "parquet.lib")
#else // Release
    #pragma comment(lib, "arrow.lib")
    #pragma comment(lib, "arrow_compute.lib")
    #pragma comment(lib, "parquet.lib")
#endif

// Use the library's namespace for cleaner code
using namespace chronosflow;

// A robust helper function that prints any data type by converting it to a string.
// A robust helper function that prints any data type by converting it to a string.
void print_dataframe(const AnalyticsDataFrame& df, const std::string& title, int max_rows = 10) {
    std::cout << "\n--- " << title << " ---\n";
    if (df.num_rows() == 0) {
        std::cout << "(DataFrame is empty)\n";
        return;
    }

    // Ensure we have the CPU version of the table for printing.
    auto cpu_df_result = df.to_cpu();
    if (!cpu_df_result.ok()) {
        std::cout << "Could not get CPU version for printing: " << cpu_df_result.status().ToString() << std::endl;
        return;
    }
    auto table = cpu_df_result.ValueOrDie().get_cpu_table();
    if (!table) {
        std::cout << "(Internal table is null)\n";
        return;
    }

    auto column_names = table->schema()->field_names();
    for (const auto& name : column_names) {
        // Adjust column width for the longer ISO timestamp
        std::cout << std::left << std::setw(22) << name;
    }
    std::cout << "\n" << std::string(column_names.size() * 22, '-') << "\n";

    for (int64_t i = 0; i < std::min((int64_t)max_rows, table->num_rows()); ++i) {
        for (int j = 0; j < table->num_columns(); ++j) {
            auto column = table->column(j);

            // FIX: Use GetScalar on the ChunkedArray directly. It handles finding
            // the right chunk automatically and avoids errors with multi-chunk tables.
            auto scalar_result = column->GetScalar(i);
            if (scalar_result.ok()) {
                auto scalar = scalar_result.ValueOrDie();
                if (scalar->is_valid) {
                    std::cout << std::left << std::setw(22) << scalar->ToString();
                }
                else {
                    std::cout << std::left << std::setw(22) << "NULL";
                }
            }
            else {
                std::cout << std::left << std::setw(22) << "[error]";
            }
        }
        std::cout << "\n";
    }

    if (table->num_rows() > max_rows) {
        std::cout << "...\n(" << table->num_rows() << " total rows)\n";
    }
}

int main() {
    std::cout << "ChronosFlow Library Test Program\n";
    std::cout << "Version: " << chronosflow::VERSION << "\n\n";
    
    // REQUIRED: Initialize Arrow compute functions to register arithmetic operations
    auto init_status = arrow::compute::Initialize();
    if (!init_status.ok()) {
        std::cerr << "Error: Failed to initialize Arrow compute functions. " 
                  << init_status.ToString() << std::endl;
        return 1;
    }

    const std::string input_file = "c:\\csv\\new\\bnb15.txt";
    std::cout << "[1] Reading TSSB data from '" << input_file << "'...\n";

    TSSBReadOptions read_options;
    read_options.has_header = false;
    read_options.date_column = "f0";
    read_options.time_column = "f1";
    read_options.auto_detect_delimiter = true;

    auto read_result = DataFrameIO::read_tssb(input_file, read_options);
    if (!read_result.ok()) {
        std::cerr << "Error: Failed to read file. " << read_result.status().ToString() << std::endl;
        return 1;
    }
    auto df = std::move(read_result).ValueOrDie();
    std::cout << "Successfully loaded " << df.num_rows() << " rows.\n";

    TSSBTimestamp start_date(20230103, 0);       // Time is 00:00:00
    TSSBTimestamp end_date(20230103, 235959); // Time is 23:59:59

    auto window_result = df.select_rows_by_timestamp(start_date, end_date);

    if (window_result.ok()) {
        AnalyticsDataFrame jan_3_df = std::move(window_result).ValueOrDie();
        print_dataframe(jan_3_df, "Data for 2023-01-03", 100);
    }

    std::cout << "\nAll tests completed successfully!\n";
    return 0;
}
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "data_matrix.h"

// Data reader for space-separated text files with headers
class StepwiseDataReader {
private:
    std::vector<std::string> split(const std::string& str, char delimiter) const;
    bool is_valid_number(const std::string& str) const;
    
public:
    struct LoadedData {
        std::unique_ptr<DataMatrix> features;
        std::vector<double> target;
        std::vector<std::string> feature_names;
        std::string target_name;
        size_t n_cases_loaded;
        size_t n_cases_total;  // Including skipped cases with missing data
    };
    
    // Load data from space-separated file
    LoadedData load_space_separated_file(
        const std::string& filename,
        const std::vector<std::string>& feature_column_names,
        const std::string& target_column_name,
        int start_row = -1,  // -1 means start from beginning
        int end_row = -1     // -1 means read until end
    ) const;
    
    // Parse header line and return column mapping
    std::map<std::string, size_t> parse_header(const std::string& header_line) const;
    
    // Validate that required columns exist
    bool validate_columns(
        const std::map<std::string, size_t>& column_map,
        const std::vector<std::string>& required_features,
        const std::string& target_name
    ) const;
};
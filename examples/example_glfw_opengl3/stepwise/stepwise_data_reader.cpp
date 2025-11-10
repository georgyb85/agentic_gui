#include "stepwise_data_reader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>

std::vector<std::string> StepwiseDataReader::split(const std::string& str, char delimiter) const {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

bool StepwiseDataReader::is_valid_number(const std::string& str) const {
    if (str.empty()) return false;
    
    try {
        std::stod(str);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

StepwiseDataReader::LoadedData StepwiseDataReader::load_space_separated_file(
    const std::string& filename,
    const std::vector<std::string>& feature_column_names,
    const std::string& target_column_name,
    int start_row,
    int end_row) const {
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    
    LoadedData result;
    result.feature_names = feature_column_names;
    result.target_name = target_column_name;
    result.n_cases_loaded = 0;
    result.n_cases_total = 0;
    
    // Read header
    std::string header_line;
    if (!std::getline(file, header_line)) {
        throw std::runtime_error("Could not read header from file");
    }
    
    auto column_map = parse_header(header_line);
    
    // Validate required columns exist
    if (!validate_columns(column_map, feature_column_names, target_column_name)) {
        throw std::runtime_error("Required columns not found in data file");
    }
    
    // Get column indices
    std::vector<size_t> feature_indices;
    for (const auto& feature_name : feature_column_names) {
        feature_indices.push_back(column_map.at(feature_name));
    }
    size_t target_index = column_map.at(target_column_name);
    
    // Determine effective row range
    int effective_start = (start_row >= 0) ? start_row : 0;
    int effective_end = end_row;  // -1 means no limit
    
    // First pass: count valid cases within range
    std::string line;
    std::streampos data_start = file.tellg();
    size_t valid_cases = 0;
    int current_row = 0;
    
    while (std::getline(file, line)) {
        result.n_cases_total++;
        
        // Check if we're within the desired row range
        bool in_range = (current_row >= effective_start) && 
                       (effective_end < 0 || current_row < effective_end);
        
        if (in_range) {
            auto values = split(line, ' ');
            if (!values.empty()) {
                bool has_valid_data = true;
                
                // Check feature columns
                for (size_t feature_idx : feature_indices) {
                    if (feature_idx >= values.size() || !is_valid_number(values[feature_idx])) {
                        has_valid_data = false;
                        break;
                    }
                }
                
                // Check target column
                if (has_valid_data && (target_index >= values.size() || !is_valid_number(values[target_index]))) {
                    has_valid_data = false;
                }
                
                if (has_valid_data) {
                    valid_cases++;
                }
            }
        }
        
        current_row++;
        
        // Early exit if we're past the end range
        if (effective_end >= 0 && current_row >= effective_end) {
            break;
        }
    }
    
    if (valid_cases == 0) {
        throw std::runtime_error("No valid data cases found in file");
    }
    
    // Allocate data structures
    result.features = std::make_unique<DataMatrix>(valid_cases, feature_column_names.size());
    result.features->set_column_names(feature_column_names);
    result.target.resize(valid_cases);
    
    // Second pass: load data
    file.clear();
    file.seekg(data_start);
    
    size_t case_idx = 0;
    current_row = 0;
    
    while (std::getline(file, line) && case_idx < valid_cases) {
        // Check if we're within the desired row range
        bool in_range = (current_row >= effective_start) && 
                       (effective_end < 0 || current_row < effective_end);
        
        if (in_range) {
            auto values = split(line, ' ');
            if (!values.empty()) {
                bool has_valid_data = true;
                std::vector<double> feature_values;
                double target_value = 0.0;
                
                // Extract feature values
                for (size_t i = 0; i < feature_indices.size(); ++i) {
                    size_t feature_idx = feature_indices[i];
                    if (feature_idx >= values.size() || !is_valid_number(values[feature_idx])) {
                        has_valid_data = false;
                        break;
                    }
                    feature_values.push_back(std::stod(values[feature_idx]));
                }
                
                // Extract target value
                if (has_valid_data) {
                    if (target_index >= values.size() || !is_valid_number(values[target_index])) {
                        has_valid_data = false;
                    } else {
                        target_value = std::stod(values[target_index]);
                    }
                }
                
                if (has_valid_data) {
                    // Store feature values
                    for (size_t i = 0; i < feature_values.size(); ++i) {
                        (*result.features)(case_idx, i) = feature_values[i];
                    }
                    result.target[case_idx] = target_value;
                    case_idx++;
                }
            }
        }
        
        current_row++;
        
        // Early exit if we're past the end range
        if (effective_end >= 0 && current_row >= effective_end) {
            break;
        }
    }
    
    result.n_cases_loaded = case_idx;
    
    file.close();
    return result;
}

std::map<std::string, size_t> StepwiseDataReader::parse_header(const std::string& header_line) const {
    auto headers = split(header_line, ' ');
    std::map<std::string, size_t> column_map;
    
    for (size_t i = 0; i < headers.size(); ++i) {
        column_map[headers[i]] = i;
    }
    
    return column_map;
}

bool StepwiseDataReader::validate_columns(
    const std::map<std::string, size_t>& column_map,
    const std::vector<std::string>& required_features,
    const std::string& target_name) const {
    
    // Check target column
    if (column_map.find(target_name) == column_map.end()) {
        std::cerr << "Target column '" << target_name << "' not found in data file" << std::endl;
        return false;
    }
    
    // Check feature columns
    for (const auto& feature_name : required_features) {
        if (column_map.find(feature_name) == column_map.end()) {
            std::cerr << "Feature column '" << feature_name << "' not found in data file" << std::endl;
            return false;
        }
    }
    
    return true;
}
#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <memory>
#include <cstring>
#include <immintrin.h>  // For SIMD intrinsics
#include <unordered_map>
#include <cctype>
#include "aligned_allocator.h"  // Use the shared aligned allocator

// Cache line size (typical for modern CPUs)
constexpr size_t CACHE_LINE_SIZE = 64;

// Structure for storing financial data in a cache-friendly, SIMD-optimized way
class TimeSeries {
private:
    // Structure-of-Arrays for better cache locality and SIMD
    struct alignas(CACHE_LINE_SIZE) Data {
        // Timestamps
        std::vector<std::string> date_strings;       // Original date strings
        AlignedVector<int64_t> timestamps;           // Unix timestamps for fast comparison

        // Financial indicators - dynamically allocated based on CSV columns
        std::vector<AlignedVector<float>> indicators;

        // Column names (excluding date column)
        std::vector<std::string> column_names;

        // Column name to index mapping for fast lookup
        std::unordered_map<std::string, size_t> column_index;

        // Date column index in the original CSV
        size_t date_column_index = 0;

        size_t num_rows = 0;        // Number of data points
        size_t num_indicators = 0;   // Number of indicator columns
    } data_;

    // Helper function to trim whitespace
    static std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    // Case-insensitive string comparison
    static bool iequals(const std::string& a, const std::string& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
            [](char a, char b) {
                return std::tolower(a) == std::tolower(b);
            });
    }

    // Split string by delimiter with proper whitespace handling
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        
        if (delimiter == ' ' || delimiter == '\t') {
            // Handle whitespace separators using stringstream for proper parsing
            std::istringstream iss(str);
            std::string token;
            
            // Use stringstream extraction which automatically handles consecutive whitespace
            while (iss >> token) {
                tokens.push_back(token);
            }
        } else {
            // Use standard delimiter splitting for non-whitespace separators
            std::string token;
            std::istringstream tokenStream(str);
            while (std::getline(tokenStream, token, delimiter)) {
                tokens.push_back(trim(token));
            }
        }
        
        return tokens;
    }

    // Detect the most likely separator in CSV data
    static char detectSeparator(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return ' '; // Default to space if file can't be opened
        }
        
        std::string line;
        if (std::getline(file, line)) {
            // Check for multiple consecutive spaces (strong indicator)
            if (line.find("  ") != std::string::npos) {
                return ' ';
            }
            // Check for single spaces
            if (line.find(' ') != std::string::npos) {
                return ' ';
            }
            // Check for commas
            if (line.find(',') != std::string::npos) {
                return ',';
            }
            // Check for tabs
            if (line.find('\t') != std::string::npos) {
                return '\t';
            }
        }
        
        // Default fallback
        return ' ';
    }

public:
    TimeSeries() = default;

    // Load data from CSV file with automatic separator detection
    bool loadFromCSV(const std::string& filename) {
        // Detect the separator first
        char separator = detectSeparator(filename);
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        std::string line;

        // Read and parse header line
        if (!std::getline(file, line)) {
            return false;
        }

        if (!parseHeader(line, separator)) {
            return false;
        }

        // Pre-allocate vectors (estimate ~5000 rows)
        const size_t estimated_rows = 5000;
        data_.date_strings.reserve(estimated_rows);
        data_.timestamps.reserve(estimated_rows);
        for (auto& indicator : data_.indicators) {
            indicator.reserve(estimated_rows);
        }

        // Read data rows
        while (std::getline(file, line)) {
            if (line.empty()) continue;

            parseDataRow(line, separator);
        }

        // Shrink to fit to save memory
        data_.date_strings.shrink_to_fit();
        data_.timestamps.shrink_to_fit();
        for (auto& indicator : data_.indicators) {
            indicator.shrink_to_fit();
        }

        return true;
    }

    // Get indicator data by name (returns pointer for zero-copy access)
    const float* getIndicator(const std::string& name) const {
        auto it = data_.column_index.find(name);
        if (it == data_.column_index.end()) {
            return nullptr;
        }
        return data_.indicators[it->second].data();
    }

    // Get mutable indicator data
    float* getIndicatorMutable(const std::string& name) {
        auto it = data_.column_index.find(name);
        if (it == data_.column_index.end()) {
            return nullptr;
        }
        return data_.indicators[it->second].data();
    }

    // Get indicator data by index
    const float* getIndicatorByIndex(size_t index) const {
        if (index >= data_.num_indicators) {
            return nullptr;
        }
        return data_.indicators[index].data();
    }

    // Get column names
    const std::vector<std::string>& getColumnNames() const {
        return data_.column_names;
    }

    // Get column name by index
    const std::string& getColumnName(size_t index) const {
        static const std::string empty;
        if (index >= data_.num_indicators) {
            return empty;
        }
        return data_.column_names[index];
    }

    // Get timestamps
    const int64_t* getTimestamps() const {
        return data_.timestamps.data();
    }

    // Get date strings
    const std::vector<std::string>& getDateStrings() const {
        return data_.date_strings;
    }

    // Get number of data points
    size_t size() const {
        return data_.num_rows;
    }

    // Get number of indicators
    size_t numIndicators() const {
        return data_.num_indicators;
    }

    // Check if column exists
    bool hasColumn(const std::string& name) const {
        return data_.column_index.find(name) != data_.column_index.end();
    }

    // Compute sliding window statistics using SIMD
    struct WindowStats {
        float mean;
        float std_dev;
        float min;
        float max;
    };

    WindowStats computeWindowStats(const std::string& indicator_name,
        size_t start_idx,
        size_t window_size) const {
        const float* data = getIndicator(indicator_name);
        if (!data || start_idx + window_size > data_.num_rows) {
            return { NAN, NAN, NAN, NAN };
        }

        return computeWindowStatsSimd(data + start_idx, window_size);
    }

    // Get data slice for a time range
    void getTimeRangeData(int64_t start_time, int64_t end_time,
        std::vector<size_t>& indices) const {
        indices.clear();
        indices.reserve(data_.num_rows / 10);  // Estimate

        const int64_t* timestamps = data_.timestamps.data();
        for (size_t i = 0; i < data_.num_rows; ++i) {
            if (timestamps[i] >= start_time && timestamps[i] <= end_time) {
                indices.push_back(i);
            }
        }
    }

    // Find index by date
    size_t findDateIndex(const std::string& date_str) const {
        int64_t target_timestamp = parseDate(date_str);
        const int64_t* timestamps = data_.timestamps.data();

        // Binary search since dates should be sorted
        auto it = std::lower_bound(timestamps, timestamps + data_.num_rows, target_timestamp);
        if (it != timestamps + data_.num_rows && *it == target_timestamp) {
            return std::distance(timestamps, it);
        }
        return std::string::npos;
    }

    // Get value for specific date and indicator
    float getValue(const std::string& date_str, const std::string& indicator_name) const {
        size_t date_idx = findDateIndex(date_str);
        if (date_idx == std::string::npos) {
            return NAN;
        }

        const float* indicator_data = getIndicator(indicator_name);
        if (!indicator_data) {
            return NAN;
        }

        return indicator_data[date_idx];
    }

private:
    // Parse header line to extract column names
    bool parseHeader(const std::string& header, char separator) {
        std::vector<std::string> columns = split(header, separator);

        if (columns.empty()) {
            return false;
        }

        // Find date column
        bool found_date = false;
        for (size_t i = 0; i < columns.size(); ++i) {
            if (iequals(columns[i], "date")) {
                data_.date_column_index = i;
                found_date = true;
                break;
            }
        }

        if (!found_date) {
            // If no "date" column found, assume first column is date
            data_.date_column_index = 0;
        }

        // Initialize indicator storage
        data_.num_indicators = columns.size() - 1;  // Exclude date column
        data_.indicators.resize(data_.num_indicators);
        data_.column_names.reserve(data_.num_indicators);

        // Build column index mapping (excluding date column)
        size_t indicator_idx = 0;
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i != data_.date_column_index) {
                data_.column_names.push_back(columns[i]);
                data_.column_index[columns[i]] = indicator_idx;
                indicator_idx++;
            }
        }

        return true;
    }

    // Parse a data row
    void parseDataRow(const std::string& line, char separator) {
        std::vector<std::string> values = split(line, separator);

        if (values.size() < data_.date_column_index + 1) {
            return;  // Invalid row
        }

        // Parse date
        std::string date_str = values[data_.date_column_index];
        if (date_str.empty()) return;

        data_.date_strings.push_back(date_str);
        data_.timestamps.push_back(parseDate(date_str));

        // Parse indicators
        size_t indicator_idx = 0;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i != data_.date_column_index && indicator_idx < data_.num_indicators) {
                float value;
                try {
                    if (values[i].empty()) {
                        value = std::numeric_limits<float>::quiet_NaN();
                    }
                    else {
                        value = std::stof(values[i]);
                    }
                }
                catch (...) {
                    value = std::numeric_limits<float>::quiet_NaN();
                }
                data_.indicators[indicator_idx].push_back(value);
                indicator_idx++;
            }
        }

        // Fill missing indicators with NaN
        while (indicator_idx < data_.num_indicators) {
            data_.indicators[indicator_idx].push_back(std::numeric_limits<float>::quiet_NaN());
            indicator_idx++;
        }

        data_.num_rows++;
    }

    // Parse date string (YYYYMMDD) to Unix timestamp
    int64_t parseDate(const std::string& date_str) const {
        if (date_str.length() != 8) {
            return 0;
        }

        int year = std::stoi(date_str.substr(0, 4));
        int month = std::stoi(date_str.substr(4, 2));
        int day = std::stoi(date_str.substr(6, 2));

        std::tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;

        return std::mktime(&tm);
    }

    // SIMD-optimized window statistics computation
    WindowStats computeWindowStatsSimd(const float* data, size_t size) const {
        WindowStats stats;

        // Use AVX2 for SIMD operations
        size_t simd_size = size - (size % 8);
        __m256 sum_vec = _mm256_setzero_ps();
        __m256 min_vec = _mm256_set1_ps(FLT_MAX);
        __m256 max_vec = _mm256_set1_ps(-FLT_MAX);

        // First pass: compute sum, min, max
        for (size_t i = 0; i < simd_size; i += 8) {
            __m256 data_vec = _mm256_load_ps(data + i);
            sum_vec = _mm256_add_ps(sum_vec, data_vec);
            min_vec = _mm256_min_ps(min_vec, data_vec);
            max_vec = _mm256_max_ps(max_vec, data_vec);
        }

        // Horizontal reduction
        float sum_arr[8], min_arr[8], max_arr[8];
        _mm256_store_ps(sum_arr, sum_vec);
        _mm256_store_ps(min_arr, min_vec);
        _mm256_store_ps(max_arr, max_vec);

        float sum = 0, min_val = FLT_MAX, max_val = -FLT_MAX;
        for (int i = 0; i < 8; ++i) {
            sum += sum_arr[i];
            min_val = std::min(min_val, min_arr[i]);
            max_val = std::max(max_val, max_arr[i]);
        }

        // Handle remaining elements
        for (size_t i = simd_size; i < size; ++i) {
            sum += data[i];
            min_val = std::min(min_val, data[i]);
            max_val = std::max(max_val, data[i]);
        }

        stats.mean = sum / size;
        stats.min = min_val;
        stats.max = max_val;

        // Second pass: compute variance
        __m256 mean_vec = _mm256_set1_ps(stats.mean);
        __m256 var_vec = _mm256_setzero_ps();

        for (size_t i = 0; i < simd_size; i += 8) {
            __m256 data_vec = _mm256_load_ps(data + i);
            __m256 diff = _mm256_sub_ps(data_vec, mean_vec);
            var_vec = _mm256_fmadd_ps(diff, diff, var_vec);
        }

        // Horizontal reduction for variance
        float var_arr[8];
        _mm256_store_ps(var_arr, var_vec);
        float variance = 0;
        for (int i = 0; i < 8; ++i) {
            variance += var_arr[i];
        }

        // Handle remaining elements
        for (size_t i = simd_size; i < size; ++i) {
            float diff = data[i] - stats.mean;
            variance += diff * diff;
        }

        stats.std_dev = std::sqrt(variance / size);

        return stats;
    }
};

// Example usage and testing
/*
#include <iostream>
#include <iomanip>

int main() {
    TimeSeries dataStore;

    if (dataStore.loadFromCSV("c:\\src\\xom.csv")) {
        std::cout << "Loaded " << dataStore.size() << " data points\n";
        std::cout << "Number of indicators: " << dataStore.numIndicators() << "\n\n";

        // Print all column names
        std::cout << "Available columns:\n";
        for (const auto& col : dataStore.getColumnNames()) {
            std::cout << "  - " << col << "\n";
        }
        std::cout << "\n";

        // Access data by column name
        if (dataStore.hasColumn("RSI_14")) {
            const float* rsi14 = dataStore.getIndicator("RSI_14");

            // Compute statistics for a 20-day window
            auto stats = dataStore.computeWindowStats("RSI_14", 0, 20);
            std::cout << "RSI_14 20-day stats:\n";
            std::cout << "  Mean: " << stats.mean << "\n";
            std::cout << "  Std Dev: " << stats.std_dev << "\n";
            std::cout << "  Min: " << stats.min << "\n";
            std::cout << "  Max: " << stats.max << "\n\n";
        }

        // Get value for specific date
        float value = dataStore.getValue("19901221", "CMMA_5_20");
        std::cout << "CMMA_5_20 on 1990-12-21: " << value << "\n";

        // Find data in a date range
        std::vector<size_t> indices;
        int64_t start_time = dataStore.parseDate("19900101");
        int64_t end_time = dataStore.parseDate("19901231");
        dataStore.getTimeRangeData(start_time, end_time, indices);
        std::cout << "Found " << indices.size() << " entries in 1990\n";
    }

    return 0;
}
*/

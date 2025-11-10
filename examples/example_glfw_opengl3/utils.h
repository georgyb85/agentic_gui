#pragma once

#include <vector>
#include <string>
#include <ctime>

// Structure to hold OHLCV data
struct OHLCVData {
    double open;
    double high;
    double low;
    double close;
    double volume;
    time_t time;
};

// Function to fetch OHLCV data from the API
std::vector<OHLCVData> fetchOHLCVData(const std::string& symbol, time_t from, time_t to, const std::string& timeframe);

// Helper function to convert date to timestamp
time_t dateToTimestamp(int year, int month, int day);
#include "utils.h"
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <sstream>
#include <iostream>
#include <chrono> // Added for time tracing

// Callback function for CURL to write received data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::vector<OHLCVData> fetchOHLCVData(const std::string& symbol, time_t from, time_t to, const std::string& timeframe) {
    std::vector<OHLCVData> data;
    auto function_start_time = std::chrono::high_resolution_clock::now();
    std::cout << "[fetchOHLCVData] Starting data fetch for symbol: " << symbol << ", timeframe: " << timeframe << std::endl;
    
    // Initialize CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return data;
    }
    
    // Construct URL
    std::stringstream url;
    url << "https://agenticresearch.info/history?symbol=" << symbol
        << "&from=" << from
        << "&to=" << to
        << "&timeframe=" << timeframe;
    
    std::string readBuffer;
    
    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout
    
    // Perform the request
    auto curl_start_time = std::chrono::high_resolution_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto curl_end_time = std::chrono::high_resolution_clock::now();
    
    // Clean up CURL
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "[fetchOHLCVData] CURL request failed: " << curl_easy_strerror(res) << std::endl;
        return data;
    }
    auto curl_duration = std::chrono::duration_cast<std::chrono::milliseconds>(curl_end_time - curl_start_time);
    std::cout << "[fetchOHLCVData] Data received from API in " << curl_duration.count() << " ms. Buffer size: " << readBuffer.size() << " bytes." << std::endl;
    
    // Parse JSON response using RapidJSON
    auto parse_start_time = std::chrono::high_resolution_clock::now();
    rapidjson::Document document;
    rapidjson::ParseResult parseResult = document.Parse(readBuffer.c_str());
    
    if (!parseResult) {
        std::cerr << "[fetchOHLCVData] JSON parse error: " << rapidjson::GetParseError_En(parseResult.Code())
                  << " at offset " << parseResult.Offset() << std::endl;
        return data;
    }
    auto parse_end_time = std::chrono::high_resolution_clock::now();
    auto parse_duration = std::chrono::duration_cast<std::chrono::milliseconds>(parse_end_time - parse_start_time);
    std::cout << "[fetchOHLCVData] JSON parsing completed in " << parse_duration.count() << " ms." << std::endl;
    
    // Check if the response is an array
    if (!document.IsArray()) {
        std::cerr << "[fetchOHLCVData] Expected JSON array response" << std::endl;
        return data;
    }
    
    // Parse each OHLCV entry
    auto processing_start_time = std::chrono::high_resolution_clock::now();
    for (rapidjson::SizeType i = 0; i < document.Size(); i++) {
        const rapidjson::Value& item = document[i];
        
        if (!item.IsObject()) {
            continue;
        }
        
        OHLCVData ohlcv;
        
        // Extract values with error checking
        if (item.HasMember("open") && item["open"].IsNumber()) {
            ohlcv.open = item["open"].GetDouble();
        }
        if (item.HasMember("high") && item["high"].IsNumber()) {
            ohlcv.high = item["high"].GetDouble();
        }
        if (item.HasMember("low") && item["low"].IsNumber()) {
            ohlcv.low = item["low"].GetDouble();
        }
        if (item.HasMember("close") && item["close"].IsNumber()) {
            ohlcv.close = item["close"].GetDouble();
        }
        if (item.HasMember("volume") && item["volume"].IsNumber()) {
            ohlcv.volume = item["volume"].GetDouble();
        }
        if (item.HasMember("time") && item["time"].IsNumber()) {
            ohlcv.time = static_cast<time_t>(item["time"].GetInt64());
        }
        
        data.push_back(ohlcv);
    }
    auto processing_end_time = std::chrono::high_resolution_clock::now();
    auto processing_duration = std::chrono::duration_cast<std::chrono::milliseconds>(processing_end_time - processing_start_time);
    std::cout << "[fetchOHLCVData] Data processing and vector population completed in " << processing_duration.count() << " ms. Parsed " << data.size() << " entries." << std::endl;

    auto function_end_time = std::chrono::high_resolution_clock::now();
    auto function_duration = std::chrono::duration_cast<std::chrono::milliseconds>(function_end_time - function_start_time);
    std::cout << "[fetchOHLCVData] Total time in fetchOHLCVData: " << function_duration.count() << " ms." << std::endl;
    
    return data;
}

time_t dateToTimestamp(int year, int month, int day) {
    struct tm timeinfo = {0};
    timeinfo.tm_year = year - 1900;  // Years since 1900
    timeinfo.tm_mon = month - 1;      // Months since January (0-11)
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = 0;
    timeinfo.tm_min = 0;
    timeinfo.tm_sec = 0;
    
    // Use UTC time
    return _mkgmtime(&timeinfo);
}

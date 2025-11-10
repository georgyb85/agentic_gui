#include "tssb_timestamp.h"
#include <sstream>
#include <iomanip>
#include <arrow/status.h>

namespace chronosflow {

arrow::Result<TSSBTimestamp> TSSBTimestamp::from_iso(const std::string& iso_string) {
    std::tm tm = {};
    std::istringstream ss(iso_string);
    
    if (iso_string.size() >= 19) {
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    } else if (iso_string.size() >= 10) {
        ss >> std::get_time(&tm, "%Y-%m-%d");
    } else {
        return arrow::Status::Invalid("Invalid ISO timestamp format: ", iso_string);
    }
    
    if (ss.fail()) {
        return arrow::Status::Invalid("Failed to parse ISO timestamp: ", iso_string);
    }
    
    int32_t date = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
    int32_t time = tm.tm_hour * 10000 + tm.tm_min * 100 + tm.tm_sec;
    
    return TSSBTimestamp(date, time);
}

arrow::Result<TSSBTimestamp> TSSBTimestamp::from_time_point(
    const std::chrono::system_clock::time_point& tp) {
    
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    // DIAGNOSTIC: Fixed deprecated gmtime warning - use gmtime_s on Windows
    std::tm tm_buf;
    std::tm* tm;
    #ifdef _WIN32
        gmtime_s(&tm_buf, &time_t);
        tm = &tm_buf;
    #else
        tm = std::gmtime(&time_t);
    #endif
    
    if (!tm) {
        return arrow::Status::Invalid("Failed to convert time_point to tm");
    }
    
    int32_t date = (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
    int32_t time = tm->tm_hour * 10000 + tm->tm_min * 100 + tm->tm_sec;
    
    return TSSBTimestamp(date, time);
}

arrow::Result<std::string> TSSBTimestamp::to_iso() const {
    if (date_ == 0) {
        return arrow::Status::Invalid("Invalid date value: 0");
    }
    
    int year = date_ / 10000;
    int month = (date_ % 10000) / 100;
    int day = date_ % 100;
    
    int hour = time_ / 10000;
    int minute = (time_ % 10000) / 100;
    int second = time_ % 100;
    
    if (year < 1900 || year > 2100 || month < 1 || month > 12 || 
        day < 1 || day > 31 || hour < 0 || hour > 23 || 
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return arrow::Status::Invalid("Invalid date/time components");
    }
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << year << "-"
        << std::setw(2) << month << "-" << std::setw(2) << day << "T"
        << std::setw(2) << hour << ":" << std::setw(2) << minute << ":"
        << std::setw(2) << second;
    
    return oss.str();
}

arrow::Result<std::chrono::system_clock::time_point> TSSBTimestamp::to_time_point() const {
    if (date_ == 0) {
        return arrow::Status::Invalid("Invalid date value: 0");
    }
    
    std::tm tm = {};
    tm.tm_year = (date_ / 10000) - 1900;
    tm.tm_mon = ((date_ % 10000) / 100) - 1;
    tm.tm_mday = date_ % 100;
    tm.tm_hour = time_ / 10000;
    tm.tm_min = (time_ % 10000) / 100;
    tm.tm_sec = time_ % 100;
    
    auto time_t = std::mktime(&tm);
    if (time_t == -1) {
        return arrow::Status::Invalid("Failed to convert tm to time_t");
    }
    
    return std::chrono::system_clock::from_time_t(time_t);
}

} // namespace chronosflow
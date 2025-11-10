#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <arrow/result.h>

namespace chronosflow {

class TSSBTimestamp {
public:
    TSSBTimestamp() = default;
    TSSBTimestamp(int32_t date, int32_t time) : date_(date), time_(time) {}

    static arrow::Result<TSSBTimestamp> from_iso(const std::string& iso_string);
    
    static arrow::Result<TSSBTimestamp> from_time_point(
        const std::chrono::system_clock::time_point& tp);

    arrow::Result<std::string> to_iso() const;
    
    arrow::Result<std::chrono::system_clock::time_point> to_time_point() const;

    int32_t date() const { return date_; }
    int32_t time() const { return time_; }

    bool operator<(const TSSBTimestamp& other) const {
        if (date_ != other.date_) {
            return date_ < other.date_;
        }
        return time_ < other.time_;
    }

    bool operator<=(const TSSBTimestamp& other) const {
        return *this < other || *this == other;
    }

    bool operator>(const TSSBTimestamp& other) const {
        return !(*this <= other);
    }

    bool operator>=(const TSSBTimestamp& other) const {
        return !(*this < other);
    }

    bool operator==(const TSSBTimestamp& other) const {
        return date_ == other.date_ && time_ == other.time_;
    }

    bool operator!=(const TSSBTimestamp& other) const {
        return !(*this == other);
    }

private:
    int32_t date_{0}; // YYYYMMDD
    int32_t time_{0}; // HHMMSS
};

} // namespace chronosflow
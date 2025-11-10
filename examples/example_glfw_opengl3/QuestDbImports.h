#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace questdb {

struct WalkforwardPredictionSeries {
    struct Entry {
        int64_t timestamp_ms = 0;
        int64_t bar_index = 0;
        int32_t fold_number = 0;
        double prediction = 0.0;
        double target = 0.0;
        double long_threshold = 0.0;
        double short_threshold = 0.0;
        double roc_threshold = 0.0;
        double short_entry_threshold = 0.0;
        double fold_score = 0.0;
        double fold_profit_factor = 0.0;
    };

    std::vector<Entry> rows;
};

bool ImportWalkforwardPredictions(const std::string& measurement,
                                  WalkforwardPredictionSeries* series,
                                  std::string* error);

} // namespace questdb

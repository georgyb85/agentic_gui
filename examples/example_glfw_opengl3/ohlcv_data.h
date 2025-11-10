#pragma once

#include <vector>
#include <ctime>   // For time_t
#include "utils.h" // For the OHLCVData struct

class OhlcvData {
public:
    OhlcvData() : data_is_processed_(false), last_processed_hide_empty_candles_state_(false) {}

    explicit OhlcvData(const std::vector<OHLCVData>& initial_raw_data)
        : raw_ohlcv_data_(initial_raw_data),
          data_is_processed_(false),
          last_processed_hide_empty_candles_state_(false) {
    }

    void setData(const std::vector<OHLCVData>& new_raw_data) {
        raw_ohlcv_data_ = new_raw_data;
        data_is_processed_ = false; // Mark data as needing reprocessing
        clearProcessedDataVectors();
    }

    // Processes the raw OHLCV data to populate the derived vectors (times, opens, etc.)
    // This method should be called after setting new data or if processing parameters change.
    void processData(bool hide_empty_candles) {
        if (data_is_processed_ && last_processed_hide_empty_candles_state_ == hide_empty_candles && !raw_ohlcv_data_.empty()) {
            // Data is already processed with the same setting and there was data.
            // If raw_ohlcv_data_ was empty and then setData made it non-empty, we need to reprocess.
            // This check ensures that if setData was called with empty data, then non-empty, it reprocesses.
            if (!opens_vec_.empty() || raw_ohlcv_data_.empty()) { // Check if processed vectors are populated or if raw data is truly empty
                 return;
            }
        }


        clearProcessedDataVectors();

        if (raw_ohlcv_data_.empty()) {
            data_is_processed_ = true;
            last_processed_hide_empty_candles_state_ = hide_empty_candles;
            return;
        }

        // Pre-allocate memory for efficiency
        opens_vec_.reserve(raw_ohlcv_data_.size());
        highs_vec_.reserve(raw_ohlcv_data_.size());
        lows_vec_.reserve(raw_ohlcv_data_.size());
        closes_vec_.reserve(raw_ohlcv_data_.size());
        volumes_vec_.reserve(raw_ohlcv_data_.size());
        times_vec_.reserve(raw_ohlcv_data_.size());
        original_times_vec_.reserve(raw_ohlcv_data_.size());

        double current_x_value = 0.0; // Used for sequential indexing if hiding empty candles

        for (const auto& candle : raw_ohlcv_data_) {
            // Define "empty" candle criteria (e.g., volume is zero).
            // This might need to match CandlestickChart's specific logic if it's more complex.
            bool is_empty_candle = (candle.volume == 0.0);

            if (hide_empty_candles && is_empty_candle) {
                continue; // Skip this candle
            }

            opens_vec_.push_back(candle.open);
            highs_vec_.push_back(candle.high);
            lows_vec_.push_back(candle.low);
            closes_vec_.push_back(candle.close);
            volumes_vec_.push_back(candle.volume);
            
            // Original times always stores the actual timestamp of the candle being processed
            original_times_vec_.push_back(static_cast<double>(candle.time));

            if (hide_empty_candles) {
                times_vec_.push_back(current_x_value);
                current_x_value += 1.0;
            } else {
                times_vec_.push_back(static_cast<double>(candle.time));
            }
        }

        data_is_processed_ = true;
        last_processed_hide_empty_candles_state_ = hide_empty_candles;
    }

    // Accessors for raw data
    const std::vector<OHLCVData>& getRawData() const { return raw_ohlcv_data_; }
    size_t getRawDataCount() const { return raw_ohlcv_data_.size(); }

    // Accessors for processed data (suitable for plotting or indicators)
    // It's assumed processData() has been called by the user of this class.
    const std::vector<double>& getTimes() const { return times_vec_; }
    const std::vector<double>& getOpens() const { return opens_vec_; }
    const std::vector<double>& getHighs() const { return highs_vec_; }
    const std::vector<double>& getLows() const { return lows_vec_; }
    const std::vector<double>& getCloses() const { return closes_vec_; }
    const std::vector<double>& getVolumes() const { return volumes_vec_; }
    const std::vector<double>& getOriginalTimes() const { return original_times_vec_; }

    size_t getProcessedDataCount() const {
        // All processed vectors (times, opens, etc.) should have the same size.
        return times_vec_.size();
    }

    bool isDataProcessed() const { return data_is_processed_; }
    bool isRawDataEmpty() const { return raw_ohlcv_data_.empty(); }

private:
    void clearProcessedDataVectors() {
        times_vec_.clear();
        opens_vec_.clear();
        highs_vec_.clear();
        lows_vec_.clear();
        closes_vec_.clear();
        volumes_vec_.clear();
        original_times_vec_.clear();
    }

    std::vector<OHLCVData> raw_ohlcv_data_;

    // Derived data vectors, populated by processData()
    std::vector<double> times_vec_;
    std::vector<double> opens_vec_;
    std::vector<double> highs_vec_;
    std::vector<double> lows_vec_;
    std::vector<double> closes_vec_;
    std::vector<double> volumes_vec_;
    std::vector<double> original_times_vec_; // Stores original timestamps corresponding to each point in times_vec_

    bool data_is_processed_;
    bool last_processed_hide_empty_candles_state_; // Stores the 'hide_empty_candles' state used for the last processing
};
#include "IndicatorEngine.hpp"
#include "IndicatorId.hpp"
#include "IndicatorRequest.hpp"
#include "Series.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Key = std::uint64_t;

Key make_key(int date, int time)
{
    return (static_cast<Key>(date) << 32) | static_cast<Key>(time);
}

struct CsvRow {
    int date{};
    int time{};
    std::vector<double> values;
};

struct Metric {
    double max_abs{0.0};
    double mse{0.0};
    std::size_t count{0};

    void push(double diff)
    {
        const double adiff = std::abs(diff);
        if (adiff > max_abs) {
            max_abs = adiff;
        }
        mse += diff * diff;
        ++count;
    }

    double rmse() const
    {
        return count == 0 ? 0.0 : std::sqrt(mse / static_cast<double>(count));
    }
};

bool read_market_series(const std::string& path, tssb::SingleMarketSeries& series,
                        std::unordered_map<Key, std::size_t>& index_map)
{
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Failed to open market data file: " << path << '\n';
        return false;
    }

    std::string date_str;
    std::string time_str;
    double open{}, high{}, low{}, close{}, volume{};
    std::size_t idx = 0;

    while (file >> date_str >> time_str >> open >> high >> low >> close >> volume) {
        const int date = std::stoi(date_str);
        const int time = std::stoi(time_str);

        series.date.push_back(date);
        series.open.push_back(open);
        series.high.push_back(high);
        series.low.push_back(low);
        series.close.push_back(close);
        series.volume.push_back(volume);

        index_map.emplace(make_key(date, time), idx);
        ++idx;
    }

    return true;
}

bool read_indicator_csv(const std::string& path,
                        std::vector<std::string>& header,
                        std::vector<CsvRow>& rows)
{
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Failed to open indicator CSV: " << path << '\n';
        return false;
    }

    std::string line;
    if (!std::getline(file, line)) {
        std::cerr << "Indicator CSV is empty: " << path << '\n';
        return false;
    }

    {
        std::istringstream header_stream(line);
        std::string token;
        while (header_stream >> token) {
            header.push_back(token);
        }
    }

    const std::size_t column_count = header.size();
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream row_stream(line);
        CsvRow row;
        std::string market;
        row.values.resize(column_count, std::numeric_limits<double>::quiet_NaN());

        std::string token;
        for (std::size_t col = 0; col < column_count; ++col) {
            if (!(row_stream >> token)) {
                std::cerr << "Malformed CSV row: " << line << '\n';
                return false;
            }

            if (col == 0) {
                row.date = std::stoi(token);
                row.values[col] = static_cast<double>(row.date);
            } else if (col == 1) {
                row.time = std::stoi(token);
                row.values[col] = static_cast<double>(row.time);
            } else if (col == 2) {
                market = token;
            } else {
                row.values[col] = std::stod(token);
            }
        }

        rows.push_back(std::move(row));
    }

    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25.txt> <BTC25 HM.csv>\n";
        return 1;
    }

    const std::string data_path = argv[1];
    const std::string csv_path = argv[2];

    tssb::SingleMarketSeries series;
    std::unordered_map<Key, std::size_t> index_map;
    if (!read_market_series(data_path, series, index_map)) {
        return 1;
    }

    std::vector<std::string> header;
    std::vector<CsvRow> csv_rows;
    if (!read_indicator_csv(csv_path, header, csv_rows)) {
        return 1;
    }

    std::unordered_map<std::string, std::size_t> column_index;
    for (std::size_t i = 0; i < header.size(); ++i) {
        column_index.emplace(header[i], i);
    }

    auto param = [](double p0 = 0.0, double p1 = 0.0, double p2 = 0.0, double p3 = 0.0) {
        tssb::IndicatorParameters params;
        params[0] = p0;
        params[1] = p1;
        params[2] = p2;
        params[3] = p3;
        return params;
    };

    std::vector<tssb::SingleIndicatorRequest> requests = {
        {tssb::SingleIndicatorId::BollingerWidth, param(20), "BOL_WIDTH_S"},
        {tssb::SingleIndicatorId::BollingerWidth, param(60), "BOL_WIDTH_M"},
        {tssb::SingleIndicatorId::BollingerWidth, param(120), "BOL_WIDTH_L"},
        {tssb::SingleIndicatorId::AtrRatio, param(10, 2.5), "ATR_RATIO_S"},
        {tssb::SingleIndicatorId::AtrRatio, param(50, 5.0), "ATR_RATIO_M"},
        {tssb::SingleIndicatorId::AtrRatio, param(120, 5.0), "ATR_RATIO_L"},
        {tssb::SingleIndicatorId::VolumeWeightedMaRatio, param(20), "VWMA_RATIO_S"},
        {tssb::SingleIndicatorId::VolumeWeightedMaRatio, param(100), "VWMA_RATIO_L"},
        {tssb::SingleIndicatorId::PriceVolumeFit, param(20), "PV_FIT_S"},
        {tssb::SingleIndicatorId::FtiLargest, param(30, 8, 5, 12), "FTI_LARGEST"},
        {tssb::SingleIndicatorId::PriceVarianceRatio, param(20, 4), "PVR_20_4"},
    };

    tssb::IndicatorEngine engine;
    auto results = engine.compute(series, requests, {.parallel = false});

    bool overall_success = true;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& request = requests[i];
        const auto& result = results[i];
        if (!result.success) {
            std::cerr << "Indicator " << result.name << " failed: " << result.error_message << '\n';
            overall_success = false;
            continue;
        }

        const auto it = column_index.find(result.name);
        if (it == column_index.end()) {
            std::cerr << "Indicator column not found in CSV: " << result.name << '\n';
            overall_success = false;
            continue;
        }

        Metric metric;
        const std::size_t col = it->second;

        for (const auto& row : csv_rows) {
            const auto map_it = index_map.find(make_key(row.date, row.time));
            if (map_it == index_map.end()) {
                continue;
            }
            const std::size_t series_index = map_it->second;
            if (series_index >= result.values.size()) {
                continue;
            }
            const double expected = row.values[col];
            const double actual = result.values[series_index];
            if (!std::isfinite(expected) || !std::isfinite(actual)) {
                continue;
            }
            metric.push(actual - expected);
        }

        std::cout << std::setw(15) << result.name << " | "
                  << "max abs diff: " << std::setw(10) << metric.max_abs
                  << " | rmse: " << std::setw(10) << metric.rmse() << '\n';
    }

    return overall_success ? 0 : 2;
}

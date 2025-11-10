#pragma once

#include <vector>
#include <cstddef>

namespace tssb {

struct SingleMarketSeries {
    std::vector<double> open;
    std::vector<double> high;
    std::vector<double> low;
    std::vector<double> close;
    std::vector<double> volume;
    std::vector<int> date;

    std::size_t size() const noexcept { return close.size(); }
};

struct MultiMarketSeries {
    std::vector<SingleMarketSeries> markets;

    std::size_t market_count() const noexcept { return markets.size(); }
    std::size_t size() const noexcept { return markets.empty() ? 0 : markets.front().size(); }
};

} // namespace tssb

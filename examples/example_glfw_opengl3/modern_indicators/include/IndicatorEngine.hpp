#pragma once

#include "Series.hpp"
#include "IndicatorRequest.hpp"
#include "IndicatorResult.hpp"
#include <vector>

namespace tssb {

struct ExecutionOptions {
    bool parallel{true};
};

class IndicatorEngine {
public:
    std::vector<IndicatorResult> compute(const SingleMarketSeries& series,
                                         const std::vector<SingleIndicatorRequest>& requests,
                                         ExecutionOptions options = {}) const;

    std::vector<IndicatorResult> compute(const MultiMarketSeries& series,
                                         const std::vector<MultiIndicatorRequest>& requests,
                                         ExecutionOptions options = {}) const;
};

} // namespace tssb

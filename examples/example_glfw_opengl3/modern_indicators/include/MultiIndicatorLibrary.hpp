#pragma once

#include "IndicatorId.hpp"
#include "IndicatorRequest.hpp"
#include "IndicatorResult.hpp"
#include "Series.hpp"

namespace tssb {

IndicatorResult compute_multi_indicator(const MultiMarketSeries& series,
                                        const MultiIndicatorRequest& request);

} // namespace tssb

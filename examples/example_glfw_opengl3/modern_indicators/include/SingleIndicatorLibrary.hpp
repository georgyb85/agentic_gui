#pragma once

#include "IndicatorId.hpp"
#include "IndicatorRequest.hpp"
#include "IndicatorResult.hpp"
#include "Series.hpp"

namespace tssb {

IndicatorResult compute_single_indicator(const SingleMarketSeries& series,
                                         const SingleIndicatorRequest& request);

} // namespace tssb

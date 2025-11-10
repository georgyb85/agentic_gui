#include "MultiIndicatorLibrary.hpp"

#include "MathUtils.hpp"

namespace tssb {

IndicatorResult compute_multi_indicator(const MultiMarketSeries&,
                                        const MultiIndicatorRequest& request)
{
    IndicatorResult result;
    result.name = request.name.empty() ? std::string(to_string(request.id)) : request.name;
    result.success = false;
    result.error_message = "Multi-market indicators have not yet been ported to the modern engine.";
    return result;
}

} // namespace tssb

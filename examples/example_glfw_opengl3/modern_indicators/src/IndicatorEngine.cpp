#include "IndicatorEngine.hpp"

#include "SingleIndicatorLibrary.hpp"
#include "MultiIndicatorLibrary.hpp"

#include <future>
#include <utility>

namespace tssb {

std::vector<IndicatorResult> IndicatorEngine::compute(
    const SingleMarketSeries& series,
    const std::vector<SingleIndicatorRequest>& requests,
    ExecutionOptions options) const
{
    std::vector<IndicatorResult> results(requests.size());

    if (options.parallel && requests.size() > 1) {
        std::vector<std::future<void>> tasks;
        tasks.reserve(requests.size());
        for (std::size_t i = 0; i < requests.size(); ++i) {
            tasks.emplace_back(std::async(std::launch::async, [&, i]() {
                results[i] = compute_single_indicator(series, requests[i]);
            }));
        }
        for (auto& task : tasks) {
            task.get();
        }
    } else {
        for (std::size_t i = 0; i < requests.size(); ++i) {
            results[i] = compute_single_indicator(series, requests[i]);
        }
    }

    return results;
}

std::vector<IndicatorResult> IndicatorEngine::compute(
    const MultiMarketSeries& series,
    const std::vector<MultiIndicatorRequest>& requests,
    ExecutionOptions options) const
{
    std::vector<IndicatorResult> results(requests.size());

    if (options.parallel && requests.size() > 1) {
        std::vector<std::future<void>> tasks;
        tasks.reserve(requests.size());
        for (std::size_t i = 0; i < requests.size(); ++i) {
            tasks.emplace_back(std::async(std::launch::async, [&, i]() {
                results[i] = compute_multi_indicator(series, requests[i]);
            }));
        }
        for (auto& task : tasks) {
            task.get();
        }
    } else {
        for (std::size_t i = 0; i < requests.size(); ++i) {
            results[i] = compute_multi_indicator(series, requests[i]);
        }
    }

    return results;
}

} // namespace tssb

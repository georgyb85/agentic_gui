#pragma once

#include "IndicatorId.hpp"
#include <array>
#include <string>

namespace tssb {

struct IndicatorParameters {
    std::array<double, 4> values{0.0, 0.0, 0.0, 0.0};

    double operator[](std::size_t idx) const noexcept { return values[idx]; }
    double& operator[](std::size_t idx) noexcept { return values[idx]; }
};

struct SingleIndicatorRequest {
    SingleIndicatorId id;
    IndicatorParameters params{};
    std::string name;
};

struct MultiIndicatorRequest {
    MultiIndicatorId id;
    IndicatorParameters params{};
    std::string name;
};

} // namespace tssb

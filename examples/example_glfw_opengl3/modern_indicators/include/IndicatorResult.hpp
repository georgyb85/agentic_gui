#pragma once

#include <vector>
#include <string>

namespace tssb {

struct IndicatorResult {
    std::string name;
    std::vector<double> values;
    bool success{true};
    std::string error_message;
};

} // namespace tssb

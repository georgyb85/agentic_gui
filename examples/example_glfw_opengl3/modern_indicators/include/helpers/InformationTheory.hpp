#pragma once

#include <span>
#include <vector>

namespace tssb::helpers {

class EntropyCalculator {
public:
    explicit EntropyCalculator(int word_length);

    [[nodiscard]] int word_length() const noexcept { return word_length_; }
    [[nodiscard]] double compute(std::span<const double> reversed_series);

private:
    int word_length_{};
    int bin_count_{};
    std::vector<int> bins_;
};

class MutualInformationCalculator {
public:
    explicit MutualInformationCalculator(int word_length);

    [[nodiscard]] int word_length() const noexcept { return word_length_; }
    [[nodiscard]] double compute(std::span<const double> reversed_series);

private:
    int word_length_{};
    int bin_count_{};
    std::vector<int> bins_;
};

} // namespace tssb::helpers

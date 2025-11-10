#include "helpers/InformationTheory.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace tssb::helpers {

namespace {
inline int pow2(int n) {
    return 1 << n;
}
} // namespace

EntropyCalculator::EntropyCalculator(int word_length)
    : word_length_(word_length),
      bin_count_(pow2(word_length)),
      bins_(static_cast<std::size_t>(bin_count_), 0)
{
    if (word_length_ < 1) {
        throw std::invalid_argument("EntropyCalculator word length must be >= 1");
    }
}

double EntropyCalculator::compute(std::span<const double> reversed_series)
{
    const int nx = static_cast<int>(reversed_series.size());
    if (nx <= word_length_) {
        return 0.0;
    }

    std::fill(bins_.begin(), bins_.end(), 0);

    for (int i = word_length_; i < nx; ++i) {
        int k = reversed_series[i - 1] > reversed_series[i] ? 1 : 0;
        for (int j = 1; j < word_length_; ++j) {
            k <<= 1;
            if (reversed_series[i - j - 1] > reversed_series[i - j]) {
                ++k;
            }
        }
        ++bins_[k];
    }

    const double total = static_cast<double>(nx - word_length_);
    const double log_bins = std::log(static_cast<double>(bin_count_));

    double entropy = 0.0;
    for (int count : bins_) {
        if (count == 0) {
            continue;
        }
        const double p = static_cast<double>(count) / total;
        entropy -= p * std::log(p);
    }

    return entropy / log_bins;
}

MutualInformationCalculator::MutualInformationCalculator(int word_length)
    : word_length_(word_length),
      bin_count_(pow2(word_length + 1)),
      bins_(static_cast<std::size_t>(bin_count_), 0)
{
    if (word_length_ < 1) {
        throw std::invalid_argument("MutualInformationCalculator word length must be >= 1");
    }
}

double MutualInformationCalculator::compute(std::span<const double> reversed_series)
{
    const int nx = static_cast<int>(reversed_series.size());
    const int n = nx - word_length_ - 1;
    if (n <= 0) {
        return 0.0;
    }

    std::fill(bins_.begin(), bins_.end(), 0);
    double dep_marg[2] = {0.0, 0.0};
    const int m = bin_count_ / 2;

    for (int i = 0; i < n; ++i) {
        int k = (reversed_series[i] > reversed_series[i + 1]) ? 1 : 0;
        ++dep_marg[k];

        for (int j = 1; j <= word_length_; ++j) {
            k <<= 1;
            if (reversed_series[i + j] > reversed_series[i + j + 1]) {
                ++k;
            }
        }
        ++bins_[k];
    }

    dep_marg[0] /= static_cast<double>(n);
    dep_marg[1] /= static_cast<double>(n);

    double mutual_information = 0.0;
    for (int i = 0; i < m; ++i) {
        const double hist_marg = static_cast<double>(bins_[i] + bins_[i + m]) / n;
        double p = static_cast<double>(bins_[i]) / n;
        if (p > 0.0) {
            mutual_information += p * std::log(p / (hist_marg * dep_marg[0]));
        }
        p = static_cast<double>(bins_[i + m]) / n;
        if (p > 0.0) {
            mutual_information += p * std::log(p / (hist_marg * dep_marg[1]));
        }
    }

    return mutual_information;
}

} // namespace tssb::helpers

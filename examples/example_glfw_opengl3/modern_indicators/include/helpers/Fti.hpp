#pragma once

#include <span>
#include <vector>

namespace tssb::helpers {

class FtiFilter {
public:
    FtiFilter(bool use_log,
              int min_period,
              int max_period,
              int half_length,
              int block_length,
              double beta,
              double noise_cut);

    void process(std::span<const double> prices, bool chronological);

    [[nodiscard]] double filtered_value(int period) const;
    [[nodiscard]] double width(int period) const;
    [[nodiscard]] double fti(int period) const;
    [[nodiscard]] int sorted_index(int rank) const;

    [[nodiscard]] int min_period() const noexcept { return min_period_; }
    [[nodiscard]] int max_period() const noexcept { return max_period_; }

private:
    void find_coefficients(int period, std::span<double> coefficients);

    bool use_log_{};
    int min_period_{};
    int max_period_{};
    int half_length_{};
    int lookback_{};
    double beta_{};
    double noise_cut_{};

    std::vector<double> y_;
    std::vector<double> coefficients_; // (period_count x (half_length+1))
    std::vector<double> filtered_;
    std::vector<double> width_;
    std::vector<double> fti_;
    std::vector<int> sorted_;

    std::vector<double> diff_work_;
    std::vector<double> leg_work_;
    std::vector<double> sort_work_;
};

} // namespace tssb::helpers

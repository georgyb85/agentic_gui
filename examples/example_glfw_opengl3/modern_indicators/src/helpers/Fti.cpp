#include "helpers/Fti.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace tssb::helpers {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

std::span<double> period_coefficients(std::vector<double>& coefs,
                                      int period_index,
                                      int half_length)
{
    const std::size_t stride = static_cast<std::size_t>(half_length + 1);
    return std::span<double>(coefs.data() + period_index * stride, stride);
}

std::span<const double> period_coefficients(const std::vector<double>& coefs,
                                            int period_index,
                                            int half_length)
{
    const std::size_t stride = static_cast<std::size_t>(half_length + 1);
    return std::span<const double>(coefs.data() + period_index * stride, stride);
}

} // namespace

FtiFilter::FtiFilter(bool use_log,
                     int min_period,
                     int max_period,
                     int half_length,
                     int block_length,
                     double beta,
                     double noise_cut)
    : use_log_(use_log),
      min_period_(min_period),
      max_period_(max_period),
      half_length_(half_length),
      lookback_(block_length),
      beta_(beta),
      noise_cut_(noise_cut)
{
    if (min_period_ < 2 || max_period_ < min_period_) {
        throw std::invalid_argument("Invalid period range for FTI filter");
    }
    if (half_length_ < 1 || lookback_ <= half_length_) {
        throw std::invalid_argument("FTI requires lookback greater than half_length");
    }

    const int period_count = max_period_ - min_period_ + 1;
    const std::size_t stride = static_cast<std::size_t>(half_length_ + 1);

    y_.resize(static_cast<std::size_t>(lookback_ + half_length_));
    coefficients_.resize(static_cast<std::size_t>(period_count) * stride);
    filtered_.resize(period_count);
    width_.resize(period_count);
    fti_.resize(period_count);
    sorted_.resize(period_count);
    diff_work_.resize(static_cast<std::size_t>(lookback_ - half_length_));
    leg_work_.resize(static_cast<std::size_t>(lookback_));
    sort_work_.resize(period_count);

    for (int period = min_period_; period <= max_period_; ++period) {
        find_coefficients(period, period_coefficients(coefficients_, period - min_period_, half_length_));
    }
}

void FtiFilter::find_coefficients(int period, std::span<double> coefficients)
{
    static constexpr double d[4] = {0.35577019, 0.2436983, 0.07211497, 0.00630165};

    const double factor = 2.0 / period;
    coefficients[0] = factor;

    const double angle = factor * kPi;
    for (int i = 1; i <= half_length_; ++i) {
        coefficients[i] = std::sin(i * angle) / (i * kPi);
    }
    coefficients[half_length_] *= 0.5;

    double sumg = coefficients[0];
    for (int i = 1; i <= half_length_; ++i) {
        double sum = d[0];
        const double fact = i * kPi / half_length_;
        for (int j = 1; j <= 3; ++j) {
            sum += 2.0 * d[j] * std::cos(j * fact);
        }
        coefficients[i] *= sum;
        sumg += 2.0 * coefficients[i];
    }

    for (int i = 0; i <= half_length_; ++i) {
        coefficients[i] /= sumg;
    }
}

void FtiFilter::process(std::span<const double> prices, bool chronological)
{
    if (prices.size() < static_cast<std::size_t>(lookback_)) {
        throw std::invalid_argument("FTI process requires at least lookback samples");
    }

    const double* data_ptr = chronological ? prices.data() + (prices.size() - 1)
                                           : prices.data();

    for (int i = lookback_ - 1; i >= 0; --i) {
        const double value = use_log_ ? std::log10(*data_ptr) : *data_ptr;
        y_[static_cast<std::size_t>(i)] = value;
        if (chronological) {
            --data_ptr;
        } else {
            ++data_ptr;
        }
    }

    const int tail = half_length_;
    double xmean = -0.5 * tail;
    double ymean = 0.0;
    for (int i = 0; i <= tail; ++i) {
        ymean += y_[static_cast<std::size_t>(lookback_ - 1 - i)];
    }
    ymean /= static_cast<double>(tail + 1);

    double xsq = 0.0;
    double xy = 0.0;
    for (int i = 0; i <= tail; ++i) {
        const double xdiff = -i - xmean;
        const double ydiff = y_[static_cast<std::size_t>(lookback_ - 1 - i)] - ymean;
        xsq += xdiff * xdiff;
        xy += xdiff * ydiff;
    }
    const double slope = xy / xsq;
    for (int i = 0; i < tail; ++i) {
        y_[static_cast<std::size_t>(lookback_ + i)] = (i + 1.0 - xmean) * slope + ymean;
    }

    const int period_count = max_period_ - min_period_ + 1;

    for (int period = min_period_; period <= max_period_; ++period) {
        const int period_idx = period - min_period_;
        const auto coefs = period_coefficients(coefficients_, period_idx, half_length_);

        int extreme_type = 0;
        double extreme_value = 0.0;
        int n_legs = 0;
        double longest_leg = 0.0;
        double prior = 0.0;

        for (int iy = half_length_; iy < lookback_ + tail; ++iy) {
            double sum = coefs[0] * y_[static_cast<std::size_t>(iy)];
            for (int ic = 1; ic <= half_length_; ++ic) {
                sum += coefs[ic] * (y_[static_cast<std::size_t>(iy - ic)] + y_[static_cast<std::size_t>(iy + ic)]);
            }

            if (iy == lookback_ - 1) {
                filtered_[period_idx] = sum;
            }
            if (iy <= lookback_ - 1) {
                diff_work_[static_cast<std::size_t>(iy - half_length_)] = std::fabs(sum - y_[static_cast<std::size_t>(iy)]);

                if (iy == half_length_) {
                    extreme_type = 0;
                    extreme_value = sum;
                    n_legs = 0;
                    longest_leg = 0.0;
                } else if (extreme_type == 0) {
                    if (sum > extreme_value) {
                        extreme_type = -1;
                    } else if (sum < extreme_value) {
                        extreme_type = 1;
                    }
                } else if (iy == lookback_ - 1) {
                    leg_work_[static_cast<std::size_t>(n_legs++)] = std::fabs(extreme_value - sum);
                    longest_leg = std::max(longest_leg, leg_work_[static_cast<std::size_t>(n_legs - 1)]);
                } else {
                    if (extreme_type == 1 && sum > prior) {
                        leg_work_[static_cast<std::size_t>(n_legs++)] = extreme_value - prior;
                        longest_leg = std::max(longest_leg, leg_work_[static_cast<std::size_t>(n_legs - 1)]);
                        extreme_type = -1;
                        extreme_value = prior;
                    } else if (extreme_type == -1 && sum < prior) {
                        leg_work_[static_cast<std::size_t>(n_legs++)] = prior - extreme_value;
                        longest_leg = std::max(longest_leg, leg_work_[static_cast<std::size_t>(n_legs - 1)]);
                        extreme_type = 1;
                        extreme_value = prior;
                    }
                }

                prior = sum;
            }
        }

        auto diff_begin = diff_work_.begin();
        auto diff_end = diff_begin + (lookback_ - half_length_);
        std::sort(diff_begin, diff_end);
        int index = static_cast<int>(beta_ * (lookback_ - half_length_ + 1)) - 1;
        if (index < 0) {
            index = 0;
        }
        width_[period_idx] = diff_work_[static_cast<std::size_t>(index)];

        const double noise_level = noise_cut_ * longest_leg;
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < n_legs; ++i) {
            const double leg = leg_work_[static_cast<std::size_t>(i)];
            if (leg > noise_level) {
                sum += leg;
                ++count;
            }
        }
        sum /= (count > 0 ? count : 1);
        fti_[period_idx] = sum / (width_[period_idx] + 1e-5);
    }

    std::vector<std::pair<double, int>> ranking;
    ranking.reserve(period_count);
    for (int i = 0; i < period_count; ++i) {
        const double value = fti_[i];
        if (i == 0 || i == period_count - 1 || (value >= fti_[i - 1] && value >= fti_[i + 1])) {
            ranking.emplace_back(-value, i);
        }
    }

    std::sort(ranking.begin(), ranking.end());
    const int limit = static_cast<int>(ranking.size());
    for (int i = 0; i < limit; ++i) {
        sorted_[i] = ranking[i].second;
    }
    for (int i = limit; i < period_count; ++i) {
        sorted_[i] = 0;
    }
}

double FtiFilter::filtered_value(int period) const
{
    return filtered_.at(period - min_period_);
}

double FtiFilter::width(int period) const
{
    return width_.at(period - min_period_);
}

double FtiFilter::fti(int period) const
{
    return fti_.at(period - min_period_);
}

int FtiFilter::sorted_index(int rank) const
{
    return sorted_.at(rank);
}

} // namespace tssb::helpers

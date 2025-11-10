#include "helpers/Janus.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace tssb::helpers {

namespace {

template <typename T>
std::span<T> span_from(std::vector<T>& data)
{
    return std::span<T>(data.data(), data.size());
}

template <typename T>
std::span<const T> span_from(const std::vector<T>& data)
{
    return std::span<const T>(data.data(), data.size());
}

} // namespace

JanusCalculator::JanusCalculator(int nbars,
                                 int n_markets,
                                 int lookback,
                                 double spread_tail,
                                 int min_cma,
                                 int max_cma)
    : nbars_(nbars),
      n_returns_(nbars - 1),
      n_markets_(n_markets),
      lookback_(lookback),
      spread_tail_(spread_tail),
      min_CMA_(min_cma),
      max_CMA_(max_cma)
{
    if (nbars_ < 2 || n_markets_ < 1 || lookback_ < 1) {
        ok_ = false;
        return;
    }

    const int buffer = std::max(lookback_, n_markets_);

    index_.assign(static_cast<std::size_t>(lookback_), 0.0);
    sorted_.assign(static_cast<std::size_t>(buffer), 0.0);
    iwork_.assign(static_cast<std::size_t>(n_markets_), 0);

    returns_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    mkt_index_returns_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    dom_index_returns_.assign(static_cast<std::size_t>(n_returns_), 0.0);

    const int cma_count = max_CMA_ - min_CMA_ + 1;
    CMA_alpha_.assign(static_cast<std::size_t>(cma_count), 0.0);
    CMA_smoothed_.assign(static_cast<std::size_t>(cma_count), 0.0);
    CMA_equity_.assign(static_cast<std::size_t>(cma_count), 0.0);

    rs_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    rs_fractile_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    rs_lagged_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    rs_leader_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    rs_laggard_.assign(static_cast<std::size_t>(n_returns_), 0.0);

    oos_avg_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    rm_leader_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    rm_laggard_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    rss_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    rss_change_.assign(static_cast<std::size_t>(n_returns_), 0.0);

    dom_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    doe_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    dom_index_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    doe_index_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    dom_sum_.assign(static_cast<std::size_t>(n_markets_), 0.0);
    doe_sum_.assign(static_cast<std::size_t>(n_markets_), 0.0);

    rm_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    rm_fractile_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);
    rm_lagged_.assign(static_cast<std::size_t>(n_returns_ * n_markets_), 0.0);

    CMA_OOS_.assign(static_cast<std::size_t>(n_returns_), 0.0);
    CMA_leader_OOS_.assign(static_cast<std::size_t>(n_returns_), 0.0);
}

void JanusCalculator::sort_values(int first, int last, std::span<double> values) const
{
    std::sort(values.begin() + first, values.begin() + last + 1);
}

void JanusCalculator::sort_with_indices(int first, int last, std::span<double> values, std::span<int> indices) const
{
    const int count = last - first + 1;
    std::vector<std::pair<double, int>> temp;
    temp.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        temp.emplace_back(values[first + i], indices[first + i]);
    }
    std::sort(temp.begin(), temp.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    for (int i = 0; i < count; ++i) {
        values[first + i] = temp[i].first;
        indices[first + i] = temp[i].second;
    }
}

void JanusCalculator::prepare(const std::vector<std::span<const double>>& prices)
{
    if (static_cast<int>(prices.size()) != n_markets_) {
        throw std::invalid_argument("JANUS prepare: unexpected number of markets");
    }

    for (int imarket = 0; imarket < n_markets_; ++imarket) {
        const auto market_prices = prices[imarket];
        if (static_cast<int>(market_prices.size()) != nbars_) {
            throw std::invalid_argument("JANUS prepare: inconsistent bar count");
        }
        for (int ibar = 1; ibar < nbars_; ++ibar) {
            returns(imarket, ibar - 1) = std::log(market_prices[ibar] / market_prices[ibar - 1]);
        }
    }

    auto sorted_span = span_from(sorted_);
    for (int ibar = 0; ibar < n_returns_; ++ibar) {
        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            sorted_span[imarket] = returns(imarket, ibar);
        }
        sort_values(0, n_markets_ - 1, sorted_span);
        if (n_markets_ % 2) {
            mkt_index_returns_[ibar] = sorted_span[n_markets_ / 2];
        } else {
            mkt_index_returns_[ibar] = 0.5 * (sorted_span[n_markets_ / 2 - 1] + sorted_span[n_markets_ / 2]);
        }
    }
}

void JanusCalculator::compute_rs(int lag)
{
    rs_lookahead = lag;
    auto sorted_span = span_from(sorted_);
    auto index_span = span_from(index_);
    auto iwork_span = span_from(iwork_);

    for (int ibar = lookback_ - 1; ibar < n_returns_; ++ibar) {
        for (int i = 0; i < lookback_; ++i) {
            index_span[i] = mkt_index_returns_[ibar - i];
        }

        for (int i = lag; i < lookback_; ++i) {
            sorted_span[i - lag] = index_span[i];
        }
        sort_values(0, lookback_ - lag - 1, sorted_span.subspan(0, lookback_ - lag));

        double median;
        const int window = lookback_ - lag;
        if (window % 2) {
            median = sorted_span[window / 2];
        } else {
            median = 0.5 * (sorted_span[window / 2 - 1] + sorted_span[window / 2]);
        }

        double index_offensive = 1e-30;
        double index_defensive = -1e-30;
        for (int i = lag; i < lookback_; ++i) {
            if (index_span[i] >= median) {
                index_offensive += index_span[i] - median;
            } else {
                index_defensive += index_span[i] - median;
            }
        }

        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            double market_offensive = 0.0;
            double market_defensive = 0.0;
            for (int i = lag; i < lookback_; ++i) {
                const double r = returns(imarket, ibar - i);
                if (index_span[i] >= median) {
                    market_offensive += r - median;
                } else {
                    market_defensive += r - median;
                }
            }

            double this_rs = 70.710678 * (market_offensive / index_offensive - market_defensive / index_defensive);
            if (this_rs > 300.0) {
                this_rs = 300.0;
            }
            if (this_rs < -300.0) {
                this_rs = -300.0;
            }
            if (lag == 0) {
                rs(ibar, imarket) = this_rs;
            } else {
                rs_lagged(ibar, imarket) = this_rs;
            }
            sorted_span[imarket] = this_rs;
            iwork_span[imarket] = imarket;
        }

        sort_with_indices(0, n_markets_ - 1, sorted_span, iwork_span);
        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            if (lag == 0) {
                rs_fractile(ibar, iwork_span[imarket]) = static_cast<double>(imarket) / (n_markets_ - 1.0);
            }
        }
    }
}

void JanusCalculator::compute_rss()
{
    auto sorted_span = span_from(sorted_);
    for (int ibar = lookback_ - 1; ibar < n_returns_; ++ibar) {
        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            sorted_span[imarket] = rs(ibar, imarket);
        }
        sort_values(0, n_markets_ - 1, sorted_span);
        int k = static_cast<int>(spread_tail_ * (n_markets_ + 1)) - 1;
        if (k < 0) {
            k = 0;
        }
        double width = 0.0;
        int count = k + 1;
        int temp = k;
        while (temp >= 0) {
            width += sorted_span[n_markets_ - 1 - temp] - sorted_span[temp];
            --temp;
        }
        width /= count;
        rss_[ibar] = width;
        rss_change_[ibar] = (ibar == lookback_ - 1) ? 0.0 : rss_[ibar] - rss_[ibar - 1];
    }
}

void JanusCalculator::compute_dom_doe()
{
    auto dom_sum_span = span_from(dom_sum_);
    auto doe_sum_span = span_from(doe_sum_);
    std::fill(dom_sum_span.begin(), dom_sum_span.end(), 0.0);
    std::fill(doe_sum_span.begin(), doe_sum_span.end(), 0.0);

    double dom_index_sum = 0.0;
    double doe_index_sum = 0.0;

    for (int ibar = lookback_ - 1; ibar < n_returns_; ++ibar) {
        if (rss_change_[ibar] > 0.0) {
            dom_index_sum += mkt_index_returns_[ibar];
            for (int imarket = 0; imarket < n_markets_; ++imarket) {
                dom_sum_span[imarket] += returns(imarket, ibar);
            }
        } else if (rss_change_[ibar] < 0.0) {
            doe_index_sum += mkt_index_returns_[ibar];
            for (int imarket = 0; imarket < n_markets_; ++imarket) {
                doe_sum_span[imarket] += returns(imarket, ibar);
            }
        }

        dom_index_[ibar] = dom_index_sum;
        doe_index_[ibar] = doe_index_sum;
        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            dom(ibar, imarket) = dom_sum_span[imarket];
            doe(ibar, imarket) = doe_sum_span[imarket];
        }
    }
}

void JanusCalculator::compute_rm(int lag)
{
    rm_lookahead = lag;
    auto sorted_span = span_from(sorted_);
    auto index_span = span_from(index_);
    auto iwork_span = span_from(iwork_);

    // Precompute median DOM returns for each bar
    for (int ibar = 0; ibar < n_returns_; ++ibar) {
        if (ibar < lookback_) {
            for (int imarket = 0; imarket < n_markets_; ++imarket) {
                sorted_span[imarket] = returns(imarket, ibar);
            }
        } else {
            for (int imarket = 0; imarket < n_markets_; ++imarket) {
                sorted_span[imarket] = dom(ibar, imarket) - dom(ibar - 1, imarket);
            }
        }
        sort_values(0, n_markets_ - 1, sorted_span);
        if (n_markets_ % 2) {
            dom_index_returns_[ibar] = sorted_span[n_markets_ / 2];
        } else {
            dom_index_returns_[ibar] = 0.5 * (sorted_span[n_markets_ / 2 - 1] + sorted_span[n_markets_ / 2]);
        }
    }

    for (int ibar = lookback_ - 1; ibar < n_returns_; ++ibar) {
        for (int i = 0; i < lookback_; ++i) {
            index_span[i] = dom_index_returns_[ibar - i];
        }

        for (int i = lag; i < lookback_; ++i) {
            sorted_span[i - lag] = index_span[i];
        }
        sort_values(0, lookback_ - lag - 1, sorted_span.subspan(0, lookback_ - lag));

        double median;
        const int window = lookback_ - lag;
        if (window % 2) {
            median = sorted_span[window / 2];
        } else {
            median = 0.5 * (sorted_span[window / 2 - 1] + sorted_span[window / 2]);
        }

        double index_offensive = 1e-30;
        double index_defensive = -1e-30;
        for (int i = lag; i < lookback_; ++i) {
            if (index_span[i] >= median) {
                index_offensive += index_span[i] - median;
            } else {
                index_defensive += index_span[i] - median;
            }
        }

        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            double market_offensive = 0.0;
            double market_defensive = 0.0;
            for (int i = lag; i < lookback_; ++i) {
                double ret = 0.0;
                if (ibar - i < lookback_) {
                    ret = returns(imarket, ibar - i);
                } else {
                    ret = dom(ibar - i, imarket) - dom(ibar - i - 1, imarket);
                }
                if (index_span[i] >= median) {
                    market_offensive += ret - median;
                } else {
                    market_defensive += ret - median;
                }
            }

            double this_rs = 70.710678 * (market_offensive / index_offensive - market_defensive / index_defensive);
            this_rs = std::clamp(this_rs, -200.0, 200.0);
            if (lag == 0) {
                rm(ibar, imarket) = this_rs;
            } else {
                rm_lagged(ibar, imarket) = this_rs;
            }
            sorted_span[imarket] = this_rs;
            iwork_span[imarket] = imarket;
        }

        sort_with_indices(0, n_markets_ - 1, sorted_span, iwork_span);
        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            if (lag == 0) {
                rm_fractile(ibar, iwork_span[imarket]) = static_cast<double>(imarket) / (n_markets_ - 1.0);
            }
        }
    }
}

void JanusCalculator::compute_rs_ps()
{
    auto sorted_span = span_from(sorted_);
    auto iwork_span = span_from(iwork_);

    for (int ibar = lookback_ - 1; ibar < n_returns_; ++ibar) {
        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            sorted_span[imarket] = rs_lagged(ibar, imarket);
            iwork_span[imarket] = imarket;
        }

        sort_with_indices(0, n_markets_ - 1, sorted_span, iwork_span);

        int k = static_cast<int>(spread_tail_ * (n_markets_ + 1)) - 1;
        if (k < 0) {
            k = 0;
        }
        const int n = k + 1;

        rs_leader_[ibar] = 0.0;
        rs_laggard_[ibar] = 0.0;
        int temp = k;
        while (temp >= 0) {
            int low_index = iwork_span[temp];
            for (int i = 0; i < rs_lookahead; ++i) {
                rs_laggard_[ibar] += returns(low_index, ibar - i);
            }
            int high_index = iwork_span[n_markets_ - 1 - temp];
            for (int i = 0; i < rs_lookahead; ++i) {
                rs_leader_[ibar] += returns(high_index, ibar - i);
            }
            --temp;
        }

        rs_leader_[ibar] /= n * rs_lookahead;
        rs_laggard_[ibar] /= n * rs_lookahead;

        double avg = 0.0;
        for (int i = 0; i < n_markets_; ++i) {
            avg += returns(i, ibar);
        }
        oos_avg_[ibar] = avg / n_markets_;
    }
}

void JanusCalculator::compute_rm_ps()
{
    auto sorted_span = span_from(sorted_);
    auto iwork_span = span_from(iwork_);

    for (int ibar = lookback_ - 1; ibar < n_returns_; ++ibar) {
        for (int imarket = 0; imarket < n_markets_; ++imarket) {
            sorted_span[imarket] = rm_lagged(ibar, imarket);
            iwork_span[imarket] = imarket;
        }

        sort_with_indices(0, n_markets_ - 1, sorted_span, iwork_span);

        int k = static_cast<int>(spread_tail_ * (n_markets_ + 1)) - 1;
        if (k < 0) {
            k = 0;
        }
        const int n = k + 1;

        rm_leader_[ibar] = 0.0;
        rm_laggard_[ibar] = 0.0;
        int temp = k;
        while (temp >= 0) {
            int low_index = iwork_span[temp];
            for (int i = 0; i < rm_lookahead; ++i) {
                rm_laggard_[ibar] += returns(low_index, ibar - i);
            }
            int high_index = iwork_span[n_markets_ - 1 - temp];
            for (int i = 0; i < rm_lookahead; ++i) {
                rm_leader_[ibar] += returns(high_index, ibar - i);
            }
            --temp;
        }

        rm_leader_[ibar] /= n * rm_lookahead;
        rm_laggard_[ibar] /= n * rm_lookahead;
    }
}

void JanusCalculator::compute_CMA()
{
    const int cma_count = max_CMA_ - min_CMA_ + 1;
    for (int i = min_CMA_; i <= max_CMA_; ++i) {
        CMA_alpha_[i - min_CMA_] = 2.0 / (i + 1.0);
        CMA_smoothed_[i - min_CMA_] = 0.0;
        CMA_equity_[i - min_CMA_] = 0.0;
    }

    std::fill(CMA_OOS_.begin(), CMA_OOS_.begin() + lookback_ + 2, 0.0);
    std::fill(CMA_leader_OOS_.begin(), CMA_leader_OOS_.begin() + lookback_ + 2, 0.0);

    auto sorted_span = span_from(sorted_);
    auto iwork_span = span_from(iwork_);

    for (int ibar = lookback_ + 2; ibar < n_returns_; ++ibar) {
        double best_equity = -1e60;
        int best_index = min_CMA_;

        for (int i = min_CMA_; i <= max_CMA_; ++i) {
            if (dom_index_[ibar - 2] > CMA_smoothed_[i - min_CMA_]) {
                CMA_equity_[i - min_CMA_] += oos_avg_[ibar - 1];
            }
            if (CMA_equity_[i - min_CMA_] > best_equity) {
                best_equity = CMA_equity_[i - min_CMA_];
                best_index = i;
            }
            CMA_smoothed_[i - min_CMA_] = CMA_alpha_[i - min_CMA_] * dom_index_[ibar - 2]
                                         + (1.0 - CMA_alpha_[i - min_CMA_]) * CMA_smoothed_[i - min_CMA_];
        }

        if (dom_index_[ibar - 1] > CMA_smoothed_[best_index - min_CMA_]) {
            CMA_OOS_[ibar] = oos_avg_[ibar];

            for (int imarket = 0; imarket < n_markets_; ++imarket) {
                sorted_span[imarket] = rm(ibar - 1, imarket);
                iwork_span[imarket] = imarket;
            }
            sort_with_indices(0, n_markets_ - 1, sorted_span, iwork_span);
            int k = static_cast<int>(spread_tail_ * (n_markets_ + 1)) - 1;
            if (k < 0) {
                k = 0;
            }
            int temp = k;
            double leader_sum = 0.0;
            while (temp >= 0) {
                int idx = iwork_span[n_markets_ - 1 - temp];
                leader_sum += returns(idx, ibar);
                --temp;
            }
            CMA_leader_OOS_[ibar] = leader_sum / (k + 1);
        }
    }
}

static void cumulative_assign(std::span<double> dest,
                              int start,
                              int nbars,
                              const std::span<const double>& source)
{
    double sum = 0.0;
    for (int i = start; i < nbars; ++i) {
        sum += source[i - 1];
        dest[i] = sum;
    }
}

void JanusCalculator::get_market_index(std::span<double> dest) const
{
    cumulative_assign(dest, lookback_, nbars_, span_from(mkt_index_returns_));
}

void JanusCalculator::get_dom_index(std::span<double> dest) const
{
    cumulative_assign(dest, lookback_, nbars_, span_from(dom_index_returns_));
}

void JanusCalculator::get_rs(std::span<double> dest, int ordinal) const
{
    if (ordinal <= 0 || ordinal > n_markets_) {
        throw std::out_of_range("JANUS get_rs ordinal out of range");
    }
    for (int i = lookback_; i < nbars_; ++i) {
        dest[i] = rs(i - 1, ordinal - 1);
    }
}

void JanusCalculator::get_rs_fractile(std::span<double> dest, int ordinal) const
{
    if (ordinal <= 0 || ordinal > n_markets_) {
        throw std::out_of_range("JANUS get_rs_fractile ordinal out of range");
    }
    for (int i = lookback_; i < nbars_; ++i) {
        dest[i] = rs_fractile(i - 1, ordinal - 1);
    }
}

void JanusCalculator::get_rss(std::span<double> dest) const
{
    for (int i = lookback_; i < nbars_; ++i) {
        dest[i] = rss_[i - 1];
    }
}

void JanusCalculator::get_rss_change(std::span<double> dest) const
{
    for (int i = lookback_; i < nbars_; ++i) {
        dest[i] = rss_change_[i - 1];
    }
}

void JanusCalculator::get_dom(std::span<double> dest, int ordinal) const
{
    if (ordinal == 0) {
        for (int i = lookback_; i < nbars_; ++i) {
            dest[i] = dom_index_[i - 1];
        }
    } else {
        for (int i = lookback_; i < nbars_; ++i) {
            dest[i] = dom(i - 1, ordinal - 1);
        }
    }
}

void JanusCalculator::get_doe(std::span<double> dest, int ordinal) const
{
    if (ordinal == 0) {
        for (int i = lookback_; i < nbars_; ++i) {
            dest[i] = doe_index_[i - 1];
        }
    } else {
        for (int i = lookback_; i < nbars_; ++i) {
            dest[i] = doe(i - 1, ordinal - 1);
        }
    }
}

void JanusCalculator::get_rm(std::span<double> dest, int ordinal) const
{
    if (ordinal <= 0 || ordinal > n_markets_) {
        throw std::out_of_range("JANUS get_rm ordinal out of range");
    }
    for (int i = lookback_; i < nbars_; ++i) {
        dest[i] = rm(i - 1, ordinal - 1);
    }
}

void JanusCalculator::get_rm_fractile(std::span<double> dest, int ordinal) const
{
    if (ordinal <= 0 || ordinal > n_markets_) {
        throw std::out_of_range("JANUS get_rm_fractile ordinal out of range");
    }
    for (int i = lookback_; i < nbars_; ++i) {
        dest[i] = rm_fractile(i - 1, ordinal - 1);
    }
}

void JanusCalculator::get_rs_leader_equity(std::span<double> dest) const
{
    cumulative_assign(dest, lookback_, nbars_, span_from(rs_leader_));
}

void JanusCalculator::get_rs_laggard_equity(std::span<double> dest) const
{
    cumulative_assign(dest, lookback_, nbars_, span_from(rs_laggard_));
}

void JanusCalculator::get_rs_ps(std::span<double> dest) const
{
    std::vector<double> series(static_cast<std::size_t>(n_returns_), 0.0);
    for (int i = 0; i < n_returns_; ++i) {
        series[i] = rs_leader_[i] - rs_laggard_[i];
    }
    cumulative_assign(dest, lookback_, nbars_, span_from(series));
}

void JanusCalculator::get_rs_leader_advantage(std::span<double> dest) const
{
    std::vector<double> series(static_cast<std::size_t>(n_returns_), 0.0);
    for (int i = 0; i < n_returns_; ++i) {
        series[i] = rs_leader_[i] - oos_avg_[i];
    }
    cumulative_assign(dest, lookback_, nbars_, span_from(series));
}

void JanusCalculator::get_rs_laggard_advantage(std::span<double> dest) const
{
    std::vector<double> series(static_cast<std::size_t>(n_returns_), 0.0);
    for (int i = 0; i < n_returns_; ++i) {
        series[i] = rs_laggard_[i] - oos_avg_[i];
    }
    cumulative_assign(dest, lookback_, nbars_, span_from(series));
}

void JanusCalculator::get_oos_avg(std::span<double> dest) const
{
    double sum = 0.0;
    for (int i = lookback_; i < nbars_; ++i) {
        sum += oos_avg_[i - 1];
        dest[i] = sum;
    }
}

void JanusCalculator::get_rm_leader_equity(std::span<double> dest) const
{
    double sum = 0.0;
    for (int i = lookback_; i < nbars_; ++i) {
        sum += rm_leader_[i - 1];
        dest[i] = sum;
    }
}

void JanusCalculator::get_rm_laggard_equity(std::span<double> dest) const
{
    double sum = 0.0;
    for (int i = lookback_; i < nbars_; ++i) {
        sum += rm_laggard_[i - 1];
        dest[i] = sum;
    }
}

void JanusCalculator::get_rm_ps(std::span<double> dest) const
{
    double sum = 0.0;
    for (int i = lookback_; i < nbars_; ++i) {
        sum += rm_leader_[i - 1] - rm_laggard_[i - 1];
        dest[i] = sum;
    }
}

void JanusCalculator::get_rm_leader_advantage(std::span<double> dest) const
{
    double sum = 0.0;
    for (int i = lookback_; i < nbars_; ++i) {
        sum += rm_leader_[i - 1] - oos_avg_[i - 1];
        dest[i] = sum;
    }
}

void JanusCalculator::get_rm_laggard_advantage(std::span<double> dest) const
{
    double sum = 0.0;
    for (int i = lookback_; i < nbars_; ++i) {
        sum += rm_laggard_[i - 1] - oos_avg_[i - 1];
        dest[i] = sum;
    }
}

void JanusCalculator::get_CMA_OOS(std::span<double> dest) const
{
    cumulative_assign(dest, lookback_, nbars_, span_from(CMA_OOS_));
}

void JanusCalculator::get_leader_CMA_OOS(std::span<double> dest) const
{
    cumulative_assign(dest, lookback_, nbars_, span_from(CMA_leader_OOS_));
}

} // namespace tssb::helpers

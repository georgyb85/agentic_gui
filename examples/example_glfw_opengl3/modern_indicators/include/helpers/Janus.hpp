#pragma once

#include <span>
#include <vector>

namespace tssb::helpers {

class JanusCalculator {
public:
    JanusCalculator(int nbars,
                    int n_markets,
                    int lookback,
                    double spread_tail,
                    int min_cma,
                    int max_cma);

    void prepare(const std::vector<std::span<const double>>& prices);
    void compute_rs(int lag);
    void compute_rss();
    void compute_dom_doe();
    void compute_rm(int lag);
    void compute_rs_ps();
    void compute_rm_ps();
    void compute_CMA();

    void get_market_index(std::span<double> dest) const;
    void get_dom_index(std::span<double> dest) const;
    void get_rs(std::span<double> dest, int ordinal) const;
    void get_rs_fractile(std::span<double> dest, int ordinal) const;
    void get_rss(std::span<double> dest) const;
    void get_rss_change(std::span<double> dest) const;
    void get_dom(std::span<double> dest, int ordinal) const;
    void get_doe(std::span<double> dest, int ordinal) const;
    void get_rm(std::span<double> dest, int ordinal) const;
    void get_rm_fractile(std::span<double> dest, int ordinal) const;
    void get_rs_leader_equity(std::span<double> dest) const;
    void get_rs_laggard_equity(std::span<double> dest) const;
    void get_rs_ps(std::span<double> dest) const;
    void get_rs_leader_advantage(std::span<double> dest) const;
    void get_rs_laggard_advantage(std::span<double> dest) const;
    void get_oos_avg(std::span<double> dest) const;
    void get_rm_leader_equity(std::span<double> dest) const;
    void get_rm_laggard_equity(std::span<double> dest) const;
    void get_rm_ps(std::span<double> dest) const;
    void get_rm_leader_advantage(std::span<double> dest) const;
    void get_rm_laggard_advantage(std::span<double> dest) const;
    void get_CMA_OOS(std::span<double> dest) const;
    void get_leader_CMA_OOS(std::span<double> dest) const;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    int nbars_{};
    int n_returns_{};
    int n_markets_{};
    int lookback_{};
    double spread_tail_{};
    int min_CMA_{};
    int max_CMA_{};

    int rs_lookahead{};
    int rm_lookahead{};

    bool ok_{true};

    std::vector<double> index_;
    std::vector<double> sorted_;
    std::vector<int> iwork_;

    std::vector<double> returns_;
    std::vector<double> mkt_index_returns_;
    std::vector<double> dom_index_returns_;

    std::vector<double> CMA_alpha_;
    std::vector<double> CMA_smoothed_;
    std::vector<double> CMA_equity_;

    std::vector<double> rs_;
    std::vector<double> rs_fractile_;
    std::vector<double> rs_lagged_;
    std::vector<double> rs_leader_;
    std::vector<double> rs_laggard_;

    std::vector<double> oos_avg_;
    std::vector<double> rm_leader_;
    std::vector<double> rm_laggard_;
    std::vector<double> rss_;
    std::vector<double> rss_change_;

    std::vector<double> dom_;
    std::vector<double> doe_;
    std::vector<double> dom_index_;
    std::vector<double> doe_index_;
    std::vector<double> dom_sum_;
    std::vector<double> doe_sum_;

    std::vector<double> rm_;
    std::vector<double> rm_fractile_;
    std::vector<double> rm_lagged_;

    std::vector<double> CMA_OOS_;
    std::vector<double> CMA_leader_OOS_;

    void sort_values(int first, int last, std::span<double> values) const;
    void sort_with_indices(int first, int last, std::span<double> values, std::span<int> indices) const;

    [[nodiscard]] double& returns(int market, int bar) {
        return returns_[static_cast<std::size_t>(market) * n_returns_ + bar];
    }
    [[nodiscard]] double returns(int market, int bar) const {
        return returns_[static_cast<std::size_t>(market) * n_returns_ + bar];
    }

    [[nodiscard]] double& rs(int bar, int market) {
        return rs_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double rs(int bar, int market) const {
        return rs_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }

    [[nodiscard]] double& rs_fractile(int bar, int market) {
        return rs_fractile_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double rs_fractile(int bar, int market) const {
        return rs_fractile_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }

    [[nodiscard]] double& rs_lagged(int bar, int market) {
        return rs_lagged_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double rs_lagged(int bar, int market) const {
        return rs_lagged_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }

    [[nodiscard]] double& dom(int bar, int market) {
        return dom_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double dom(int bar, int market) const {
        return dom_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }

    [[nodiscard]] double& doe(int bar, int market) {
        return doe_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double doe(int bar, int market) const {
        return doe_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }

    [[nodiscard]] double& rm(int bar, int market) {
        return rm_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double rm(int bar, int market) const {
        return rm_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }

    [[nodiscard]] double& rm_fractile(int bar, int market) {
        return rm_fractile_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double rm_fractile(int bar, int market) const {
        return rm_fractile_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }

    [[nodiscard]] double& rm_lagged(int bar, int market) {
        return rm_lagged_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
    [[nodiscard]] double rm_lagged(int bar, int market) const {
        return rm_lagged_[static_cast<std::size_t>(bar) * n_markets_ + market];
    }
};

} // namespace tssb::helpers

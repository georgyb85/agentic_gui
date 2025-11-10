#include "PerformanceStressTests.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>

namespace simulation {

namespace {

double ComputeSharpe(const std::vector<double>& returns_pct) {
    if (returns_pct.size() < 2) {
        return 0.0;
    }
    double mean = std::accumulate(returns_pct.begin(), returns_pct.end(), 0.0) /
                  static_cast<double>(returns_pct.size());
    double variance = 0.0;
    for (double r : returns_pct) {
        double diff = r - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(returns_pct.size() - 1);
    double std_dev = std::sqrt(std::max(variance, 0.0));
    if (std_dev <= 0.0) {
        return 0.0;
    }
    // Annualise assuming 252 trading days equivalent per trade series; approximate only.
    return (mean / std_dev) * std::sqrt(252.0);
}

double ComputeProfitFactor(const std::vector<double>& pnls) {
    double gross_profit = 1e-9; // avoid zero division
    double gross_loss = 1e-9;
    for (double pnl : pnls) {
        if (pnl > 0.0) {
            gross_profit += pnl;
        } else if (pnl < 0.0) {
            gross_loss += std::abs(pnl);
        }
    }
    return gross_profit / gross_loss;
}

double ComputeMaxDrawdownPct(const std::vector<double>& pnls, double position_size) {
    if (pnls.empty()) {
        return 0.0;
    }
    double cumulative = 0.0;
    double peak_equity = position_size;
    double max_dd_pct = 0.0;
    for (double pnl : pnls) {
        cumulative += pnl;
        double equity = position_size + cumulative;
        if (equity > peak_equity) {
            peak_equity = equity;
        }
        double dd_pct = (peak_equity > 0.0) ? ((peak_equity - equity) / peak_equity) * 100.0 : 0.0;
        if (dd_pct > max_dd_pct) {
            max_dd_pct = dd_pct;
        }
    }
    return max_dd_pct;
}

BootstrapInterval ComputeInterval(const std::vector<double>& samples, double estimate) {
    BootstrapInterval interval;
    if (samples.empty()) {
        interval.estimate = estimate;
        return interval;
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    auto percentile = [&](double prob) -> double {
        if (sorted.empty()) return 0.0;
        double index = prob * (static_cast<double>(sorted.size() - 1));
        std::size_t lo = static_cast<std::size_t>(std::floor(index));
        std::size_t hi = static_cast<std::size_t>(std::ceil(index));
        if (hi >= sorted.size()) {
            return sorted.back();
        }
        double weight = index - static_cast<double>(lo);
        return sorted[lo] * (1.0 - weight) + sorted[hi] * weight;
    };

    interval.estimate = estimate;
    interval.lower_90 = percentile(0.05);
    interval.upper_90 = percentile(0.95);
    interval.lower_95 = percentile(0.025);
    interval.upper_95 = percentile(0.975);
    return interval;
}

DrawdownQuantiles ComputeDrawdownQuantiles(const std::vector<double>& samples) {
    DrawdownQuantiles q;
    if (samples.empty()) {
        return q;
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](double prob) {
        if (sorted.empty()) return 0.0;
        double index = prob * (static_cast<double>(sorted.size() - 1));
        std::size_t lo = static_cast<std::size_t>(std::floor(index));
        std::size_t hi = static_cast<std::size_t>(std::ceil(index));
        if (hi >= sorted.size()) return sorted.back();
        double weight = index - static_cast<double>(lo);
        return sorted[lo] * (1.0 - weight) + sorted[hi] * weight;
    };
    q.q50 = pct(0.50);
    q.q90 = pct(0.90);
    q.q95 = pct(0.95);
    q.q99 = pct(0.99);
    return q;
}

} // namespace

StressTestReport RunStressTests(const std::vector<double>& trade_returns_pct,
                                const std::vector<double>& trade_pnls,
                                double position_size,
                                const StressTestConfig& config) {
    StressTestReport report;
    
    if (!config.enable) {
        return report;
    }
    if (trade_returns_pct.empty() || trade_pnls.empty() || position_size <= 0.0) {
        return report;
    }
    const std::size_t sample_size = trade_returns_pct.size();
    if (sample_size != trade_pnls.size()) {
        return report;
    }
    if (config.bootstrap_iterations <= 0) {
        return report;
    }

    report.sample_size = static_cast<int>(sample_size);
    report.bootstrap_iterations = config.bootstrap_iterations;
    report.mcpt_iterations = std::max(0, config.mcpt_iterations);

    std::mt19937_64 rng(config.seed);
    std::uniform_int_distribution<std::size_t> dist(0, sample_size - 1);
    std::uniform_int_distribution<int> sign_dist(0, 1);

    const double observed_sharpe = ComputeSharpe(trade_returns_pct);
    const double observed_profit_factor = ComputeProfitFactor(trade_pnls);
    double observed_total_return_pct = (std::accumulate(trade_pnls.begin(), trade_pnls.end(), 0.0) / position_size) * 100.0;
    const double observed_drawdown_pct = ComputeMaxDrawdownPct(trade_pnls, position_size);

    std::vector<double> sharpe_samples;
    std::vector<double> pf_samples;
    std::vector<double> total_return_samples;
    std::vector<double> drawdown_samples;

    sharpe_samples.reserve(config.bootstrap_iterations);
    pf_samples.reserve(config.bootstrap_iterations);
    total_return_samples.reserve(config.bootstrap_iterations);
    drawdown_samples.reserve(config.bootstrap_iterations);

    std::vector<double> sampled_returns(sample_size);
    std::vector<double> sampled_pnls(sample_size);

    for (int iter = 0; iter < config.bootstrap_iterations; ++iter) {
        for (std::size_t i = 0; i < sample_size; ++i) {
            std::size_t idx = dist(rng);
            sampled_returns[i] = trade_returns_pct[idx];
            sampled_pnls[i] = trade_pnls[idx];
        }

        sharpe_samples.push_back(ComputeSharpe(sampled_returns));
        pf_samples.push_back(ComputeProfitFactor(sampled_pnls));

        double total_return_pct = (std::accumulate(sampled_pnls.begin(), sampled_pnls.end(), 0.0) / position_size) * 100.0;
        total_return_samples.push_back(total_return_pct);

        drawdown_samples.push_back(ComputeMaxDrawdownPct(sampled_pnls, position_size));
    }

    report.sharpe_ci = ComputeInterval(sharpe_samples, observed_sharpe);
    report.profit_factor_ci = ComputeInterval(pf_samples, observed_profit_factor);
    report.total_return_ci = ComputeInterval(total_return_samples, observed_total_return_pct);
    report.drawdown_quantiles = ComputeDrawdownQuantiles(drawdown_samples);

    std::vector<double> mcpt_sharpe_samples;
    std::vector<double> mcpt_pf_samples;
    std::vector<double> mcpt_total_return_samples;
    std::vector<double> mcpt_drawdown_samples;

    if (config.mcpt_iterations > 0) {
        mcpt_sharpe_samples.reserve(config.mcpt_iterations);
        mcpt_pf_samples.reserve(config.mcpt_iterations);
        mcpt_total_return_samples.reserve(config.mcpt_iterations);
        mcpt_drawdown_samples.reserve(config.mcpt_iterations);

        std::vector<std::size_t> indices(sample_size);
        std::iota(indices.begin(), indices.end(), 0);
        std::vector<double> permuted_returns(sample_size);
        std::vector<double> permuted_pnls(sample_size);

        for (int iter = 0; iter < config.mcpt_iterations; ++iter) {
            std::shuffle(indices.begin(), indices.end(), rng);
            for (std::size_t i = 0; i < sample_size; ++i) {
                const std::size_t idx = indices[i];
                const double sign = (sign_dist(rng) == 0) ? -1.0 : 1.0;
                permuted_returns[i] = trade_returns_pct[idx] * sign;
                permuted_pnls[i] = trade_pnls[idx] * sign;
            }

            mcpt_sharpe_samples.push_back(ComputeSharpe(permuted_returns));
            mcpt_pf_samples.push_back(ComputeProfitFactor(permuted_pnls));
            const double total_return_pct = (std::accumulate(permuted_pnls.begin(), permuted_pnls.end(), 0.0) / position_size) * 100.0;
            mcpt_total_return_samples.push_back(total_return_pct);
            mcpt_drawdown_samples.push_back(ComputeMaxDrawdownPct(permuted_pnls, position_size));
        }
    }

    auto p_value_upper = [](const std::vector<double>& samples, double observed) -> double {
        if (samples.empty()) return 1.0;
        int count = 0;
        for (double value : samples) {
            if (value >= observed) {
                ++count;
            }
        }
        return static_cast<double>(count + 1) / static_cast<double>(samples.size() + 1);
    };

    const std::vector<double>& sharpe_source = mcpt_sharpe_samples.empty() ? sharpe_samples : mcpt_sharpe_samples;
    const std::vector<double>& pf_source = mcpt_pf_samples.empty() ? pf_samples : mcpt_pf_samples;
    const std::vector<double>& tr_source = mcpt_total_return_samples.empty() ? total_return_samples : mcpt_total_return_samples;
    const std::vector<double>& dd_source = mcpt_drawdown_samples.empty() ? drawdown_samples : mcpt_drawdown_samples;

    report.monte_carlo.total_return_pvalue = p_value_upper(tr_source, observed_total_return_pct);
    report.monte_carlo.max_drawdown_pvalue = p_value_upper(dd_source, observed_drawdown_pct);
    report.monte_carlo.sharpe_pvalue = p_value_upper(sharpe_source, observed_sharpe);
    report.monte_carlo.profit_factor_pvalue = p_value_upper(pf_source, observed_profit_factor);

    report.computed = true;
    return report;
}

} // namespace simulation

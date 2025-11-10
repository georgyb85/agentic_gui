#pragma once

#include <cstdint>
#include <vector>

namespace simulation {

struct StressTestConfig {
    bool enable = true;
    int bootstrap_iterations = 1000;
    int mcpt_iterations = 1000; // Monte Carlo permutation iterations (0 disables MCPT)
    std::uint64_t seed = 123456789ULL;
};

struct BootstrapInterval {
    double estimate = 0.0;
    double lower_90 = 0.0;
    double upper_90 = 0.0;
    double lower_95 = 0.0;
    double upper_95 = 0.0;
};

struct DrawdownQuantiles {
    double q50 = 0.0;
    double q90 = 0.0;
    double q95 = 0.0;
    double q99 = 0.0;
};

struct MonteCarloStats {
    double total_return_pvalue = 1.0;
    double max_drawdown_pvalue = 1.0;
    double sharpe_pvalue = 1.0;
    double profit_factor_pvalue = 1.0;
};

struct StressTestReport {
    bool computed = false;
    int sample_size = 0;
    int bootstrap_iterations = 0;
    int mcpt_iterations = 0;
    BootstrapInterval sharpe_ci;
    BootstrapInterval profit_factor_ci;
    BootstrapInterval total_return_ci;
    DrawdownQuantiles drawdown_quantiles;
    MonteCarloStats monte_carlo;
};

StressTestReport RunStressTests(const std::vector<double>& trade_returns_pct,
                                const std::vector<double>& trade_pnls,
                                double position_size,
                                const StressTestConfig& config);

} // namespace simulation

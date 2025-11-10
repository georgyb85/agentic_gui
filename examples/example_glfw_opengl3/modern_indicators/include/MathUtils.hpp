#pragma once

#include <vector>
#include <cmath>
#include <numeric>
#include <span>

namespace tssb {

constexpr double kPi = 3.141592653589793238462643383279502884;

double normal_cdf(double z) noexcept;
double inverse_normal_cdf(double p) noexcept;
double igamma(double a, double x) noexcept;
double F_CDF(int ndf1, int ndf2, double F) noexcept;

double atr(bool use_log, std::span<const double> open, std::span<const double> high,
           std::span<const double> low, std::span<const double> close,
           std::size_t index, int length) noexcept;

double variance(bool use_change, std::span<const double> prices, std::size_t index, int length);

void legendre_linear(int n, std::vector<double>& c1, std::vector<double>& c2, std::vector<double>& c3);

/**
 * @brief Compute median of a vector of values
 * @param values Input values (may contain NaN which are skipped)
 * @return Median value, or NaN if no valid values
 */
double compute_median(std::vector<double> values);

/**
 * @brief Compute interquartile range (IQR = Q3 - Q1)
 * @param values Input values (may contain NaN which are skipped)
 * @return IQR value, or NaN if insufficient valid values
 */
double compute_iqr(std::vector<double> values);

/**
 * @brief Apply SCALING compression: V = 100 * Φ(c * X / IQR) - 50
 *
 * SCALING divides by IQR but does NOT subtract median (sign preserved).
 * Used when the sign of the indicator is meaningful.
 *
 * @param raw_value Raw indicator value to compress
 * @param iqr Historical IQR for scaling
 * @param c Compression constant (typically 1.0 for TSSB indicators)
 * @return Compressed value in approximately [-50, 50] range
 */
double compress_scaling(double raw_value, double iqr, double c = 1.0) noexcept;

/**
 * @brief Apply NORMALIZATION compression: V = 100 * Φ(c * (X - median) / IQR) - 50
 *
 * NORMALIZATION both centers (subtracts median) and scales (divides by IQR).
 * Used for strong stationarity when sign is not critical.
 *
 * @param raw_value Raw indicator value to compress
 * @param median Historical median for centering
 * @param iqr Historical IQR for scaling
 * @param c Compression constant (default 0.25, controls compression strength)
 * @return Compressed value in approximately [-50, 50] range
 */
double compress_to_range(double raw_value, double median, double iqr, double c = 0.25) noexcept;

} // namespace tssb

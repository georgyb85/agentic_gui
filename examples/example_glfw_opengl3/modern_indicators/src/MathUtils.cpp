#include "MathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace tssb {

namespace {

constexpr double kSqrt2Pi = 2.5066282746310005024157652848110452530069867406099;

double log_gamma(double x)
{
    if (x <= 0.0) {
        return 0.0;
    }

    double result = 0.0;
    if (x < 7.0) {
        double prod = 1.0;
        for (double z = x; z < 7.0; z += 1.0) {
            prod *= z;
            x = z;
        }
        x += 1.0;
        result = -std::log(prod);
    }

    const double z = 1.0 / (x * x);
    return result + (x - 0.5) * std::log(x) - x + 0.918938533204673
         + (((-0.000595238095238 * z + 0.000793650793651) * z - 0.002777777777778) * z + 0.083333333333333) / x;
}

double ibeta(double p, double q, double x)
{
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }
    if (p <= 0.0 || q <= 0.0) {
        return 0.0;
    }

    bool switched = false;
    if (x > 0.5) {
        std::swap(p, q);
        x = 1.0 - x;
        switched = true;
    }

    double ps = q - std::floor(q);
    if (ps == 0.0) {
        ps = 1.0;
    }

    const double px = p * std::log(x);
    const double pq = log_gamma(p + q);
    const double p1 = log_gamma(p);
    const double d4 = std::log(p);

    double term = px + log_gamma(ps + p) - log_gamma(ps) - d4 - p1;
    const double eps = 1e-12;
    const double eps1 = 1e-98;
    const double aleps1 = std::log(eps1);

    double infsum = 0.0;
    if (static_cast<int>(term / aleps1) == 0) {
        infsum = std::exp(term);
        double cnt = infsum * p;
        for (double wh = 1.0;; wh += 1.0) {
            cnt *= (wh - ps) * x / wh;
            double delta = cnt / (p + wh);
            infsum += delta;
            if (delta < eps * infsum) {
                break;
            }
        }
    }

    double finsum = 0.0;
    if (q > 1.0) {
        double xb = px + q * std::log(1.0 - x) + pq - p1 - std::log(q) - log_gamma(q);
        int ib = static_cast<int>(xb / aleps1);
        if (ib < 0) {
            ib = 0;
        }

        const double xfac = 1.0 / (1.0 - x);
        term = std::exp(xb - ib * aleps1);
        ps = q;

        for (double wh = q - 1.0; wh > 0.0; wh -= 1.0) {
            double px_term = ps * xfac / (p + wh);
            if (px_term <= 1.0 && ((term <= eps1 / px_term) || (term / eps <= finsum))) {
                break;
            }

            ps = wh;
            term *= px_term;
            if (term > 1.0) {
                --ib;
                term *= eps1;
            }

            if (ib == 0) {
                finsum += term;
            }
        }
    }

    double prob = finsum + infsum;
    return switched ? 1.0 - prob : prob;
}

} // namespace

double normal_cdf(double z) noexcept
{
    const double zz = std::fabs(z);
    const double pdf = std::exp(-0.5 * zz * zz) / kSqrt2Pi;
    const double t = 1.0 / (1.0 + zz * 0.2316419);
    const double poly = ((((1.330274429 * t - 1.821255978) * t + 1.781477937) * t - 0.356563782) * t + 0.319381530) * t;
    return z > 0.0 ? 1.0 - pdf * poly : pdf * poly;
}

double inverse_normal_cdf(double p) noexcept
{
    const double pp = (p <= 0.5) ? p : 1.0 - p;
    const double t = std::sqrt(std::log(1.0 / (pp * pp)));
    const double numer = (0.010328 * t + 0.802853) * t + 2.515517;
    const double denom = ((0.001308 * t + 0.189269) * t + 1.432788) * t + 1.0;
    const double x = t - numer / denom;
    return (p <= 0.5) ? -x : x;
}

double igamma(double a, double x) noexcept
{
    constexpr double eps = 1e-8;
    constexpr double fpm = 1e-30;

    if (x <= 0.0) {
        return 0.0;
    }

    if (x < (a + 1.0)) {
        double ap = a;
        double del = 1.0 / a;
        double sum = del;
        while (true) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * eps) {
                break;
            }
        }
        return sum * std::exp(a * std::log(x) - x - log_gamma(a));
    }

    double b = x + 1.0 - a;
    double c = 1.0 / fpm;
    double d = 1.0 / b;
    double h = d;

    for (int i = 1; i < 1000; ++i) {
        double an = static_cast<double>(i) * (a - i);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < fpm) {
            d = fpm;
        }
        c = b + an / c;
        if (std::fabs(c) < fpm) {
            c = fpm;
        }
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < eps) {
            break;
        }
    }

    return 1.0 - h * std::exp(a * std::log(x) - x - log_gamma(a));
}

double F_CDF(int ndf1, int ndf2, double F) noexcept
{
    double prob = 1.0 - ibeta(0.5 * ndf2, 0.5 * ndf1, ndf2 / (ndf2 + ndf1 * F));
    if (prob < 0.0) prob = 0.0;
    if (prob > 1.0) prob = 1.0;
    return prob;
}

double atr(bool use_log, std::span<const double> open, std::span<const double> high,
           std::span<const double> low, std::span<const double> close,
           std::size_t index, int length) noexcept
{
    if (length <= 0) {
        const double denom = use_log ? std::log(high[index] / low[index])
                                     : (high[index] - low[index]);
        return denom;
    }

    double sum = 0.0;
    const std::size_t start = index + 1 - static_cast<std::size_t>(length);
    for (std::size_t i = start; i <= index; ++i) {
        if (use_log) {
            double term = high[i] / low[i];
            term = std::max({term, high[i] / close[i - 1], close[i - 1] / low[i]});
            sum += std::log(term);
        } else {
            double term = high[i] - low[i];
            term = std::max({term, high[i] - close[i - 1], close[i - 1] - low[i]});
            sum += term;
        }
    }

    return sum / static_cast<double>(length);
}

double variance(bool use_change, std::span<const double> prices, std::size_t index, int length)
{
    double sum = 0.0;
    const std::size_t start = index + 1 - static_cast<std::size_t>(length);
    for (std::size_t i = start; i <= index; ++i) {
        const double term = use_change ? std::log(prices[i] / prices[i - 1])
                                       : std::log(prices[i]);
        sum += term;
    }
    const double mean = sum / static_cast<double>(length);

    double accum = 0.0;
    for (std::size_t i = start; i <= index; ++i) {
        const double term = use_change ? std::log(prices[i] / prices[i - 1]) - mean
                                       : std::log(prices[i]) - mean;
        accum += term * term;
    }
    return accum / static_cast<double>(length);
}

void legendre_linear(int n, std::vector<double>& c1, std::vector<double>& c2, std::vector<double>& c3)
{
    c1.resize(n);
    c2.resize(n);
    c3.resize(n);

    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        c1[i] = 2.0 * i / (n - 1.0) - 1.0;
        sum += c1[i] * c1[i];
    }
    sum = std::sqrt(sum);
    for (auto& v : c1) {
        v /= sum;
    }

    sum = 0.0;
    for (int i = 0; i < n; ++i) {
        c2[i] = c1[i] * c1[i];
        sum += c2[i];
    }
    double mean = sum / n;
    sum = 0.0;
    for (int i = 0; i < n; ++i) {
        c2[i] -= mean;
        sum += c2[i] * c2[i];
    }
    sum = std::sqrt(sum);
    for (auto& v : c2) {
        v /= sum;
    }

    sum = 0.0;
    for (int i = 0; i < n; ++i) {
        c3[i] = c1[i] * c1[i] * c1[i];
        sum += c3[i];
    }
    mean = sum / n;
    sum = 0.0;
    for (int i = 0; i < n; ++i) {
        c3[i] -= mean;
        sum += c3[i] * c3[i];
    }
    sum = std::sqrt(sum);
    for (auto& v : c3) {
        v /= sum;
    }

    double proj = 0.0;
    for (int i = 0; i < n; ++i) {
        proj += c1[i] * c3[i];
    }
    sum = 0.0;
    for (int i = 0; i < n; ++i) {
        c3[i] -= proj * c1[i];
        sum += c3[i] * c3[i];
    }
    sum = std::sqrt(sum);
    for (auto& v : c3) {
        v /= sum;
    }
}

double compute_median(std::vector<double> values)
{
    // Remove NaN values
    values.erase(
        std::remove_if(values.begin(), values.end(),
                      [](double v) { return !std::isfinite(v); }),
        values.end()
    );

    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Sort values
    std::sort(values.begin(), values.end());

    // Compute median
    const size_t n = values.size();
    if (n % 2 == 0) {
        return (values[n / 2 - 1] + values[n / 2]) / 2.0;
    } else {
        return values[n / 2];
    }
}

double compute_iqr(std::vector<double> values)
{
    // Remove NaN values
    values.erase(
        std::remove_if(values.begin(), values.end(),
                      [](double v) { return !std::isfinite(v); }),
        values.end()
    );

    if (values.size() < 4) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Sort values
    std::sort(values.begin(), values.end());

    // Compute quartiles
    const size_t n = values.size();
    const size_t q1_idx = n / 4;
    const size_t q3_idx = (3 * n) / 4;

    // Linear interpolation for quartiles
    double q1, q3;

    if (n % 4 == 0) {
        q1 = (values[q1_idx - 1] + values[q1_idx]) / 2.0;
    } else {
        q1 = values[q1_idx];
    }

    if ((3 * n) % 4 == 0) {
        q3 = (values[q3_idx - 1] + values[q3_idx]) / 2.0;
    } else {
        q3 = values[q3_idx];
    }

    return q3 - q1;
}

double compress_scaling(double raw_value, double iqr, double c) noexcept
{
    // Handle invalid inputs
    if (!std::isfinite(raw_value) || !std::isfinite(iqr)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Avoid division by zero
    if (iqr < 1e-10) {
        return 0.0;  // If no variation, return neutral value
    }

    // SCALING: NO median subtraction, just divide by IQR
    const double normalized = raw_value / iqr;

    // Apply compression: c * X
    const double compressed = c * normalized;

    // Apply normal CDF: Φ(compressed)
    const double cdf_value = normal_cdf(compressed);

    // Scale to [-50, 50] range: V = 100 * Φ - 50
    return 100.0 * cdf_value - 50.0;
}

double compress_to_range(double raw_value, double median, double iqr, double c) noexcept
{
    // Handle invalid inputs
    if (!std::isfinite(raw_value) || !std::isfinite(median) || !std::isfinite(iqr)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Avoid division by zero
    if (iqr < 1e-10) {
        return 0.0;  // If no variation, return neutral value
    }

    // NORMALIZATION: Subtract median then divide by IQR
    const double normalized = (raw_value - median) / iqr;

    // Apply compression: c * X
    const double compressed = c * normalized;

    // Apply normal CDF: Φ(compressed)
    const double cdf_value = normal_cdf(compressed);

    // Scale to [-50, 50] range: V = 100 * Φ - 50
    return 100.0 * cdf_value - 50.0;
}

} // namespace tssb

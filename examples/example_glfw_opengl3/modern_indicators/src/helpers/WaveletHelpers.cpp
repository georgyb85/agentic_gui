#include "helpers/WaveletHelpers.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tssb {
namespace helpers {

// ============================================================================
// FFT Implementation
// ============================================================================

FFT::FFT(int n) : n_(n), valid_(false)
{
    // Verify n is power of 2
    if (n <= 0 || !wavelet_utils::is_power_of_2(n)) {
        return;
    }

    // Pre-compute twiddle factors for efficiency
    cos_table_.resize(n / 2);
    sin_table_.resize(n / 2);

    for (int i = 0; i < n / 2; ++i) {
        double angle = -2.0 * M_PI * i / n;
        cos_table_[i] = std::cos(angle);
        sin_table_[i] = std::sin(angle);
    }

    valid_ = true;
}

void FFT::bit_reverse(double* real, double* imag)
{
    int j = 0;
    for (int i = 0; i < n_ - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        int m = n_ / 2;
        while (m >= 1 && j >= m) {
            j -= m;
            m /= 2;
        }
        j += m;
    }
}

void FFT::transform(double* real, double* imag, int direction)
{
    if (!valid_) {
        return;
    }

    // Bit-reverse permutation
    bit_reverse(real, imag);

    // Cooley-Tukey FFT
    for (int len = 2; len <= n_; len *= 2) {
        int half = len / 2;
        int step = n_ / len;

        for (int i = 0; i < n_; i += len) {
            for (int j = 0; j < half; ++j) {
                int k = i + j;
                int l = k + half;
                int twiddle_idx = j * step;

                // Get twiddle factor
                double wr = cos_table_[twiddle_idx];
                double wi = sin_table_[twiddle_idx];

                // Adjust for inverse transform
                if (direction < 0) {
                    wi = -wi;
                }

                // Butterfly operation
                double temp_r = real[l] * wr - imag[l] * wi;
                double temp_i = real[l] * wi + imag[l] * wr;

                real[l] = real[k] - temp_r;
                imag[l] = imag[k] - temp_i;
                real[k] = real[k] + temp_r;
                imag[k] = imag[k] + temp_i;
            }
        }
    }
}

// ============================================================================
// Morlet Transform Implementation
// ============================================================================

double MorletTransform::frequency_weight(double f, double w, double r, bool is_real)
{
    double term1, term2, term3;

    // First Gaussian centered at (f - w)
    double x = std::fabs(f - w) / r;
    term1 = (x < 20.0) ? std::exp(-x * x) : 0.0;

    // Second Gaussian centered at (f + w)
    x = (f + w) / r;
    term2 = (x < 20.0) ? std::exp(-x * x) : 0.0;

    if (is_real) {
        // Real part: symmetric function
        x = (f * f + w * w) / (r * r);
        term3 = (x < 20.0) ? std::exp(-x * x) : 0.0;
        return term1 + term2 - 2.0 * term3;
    }
    else {
        // Imaginary part: antisymmetric function
        return term1 - term2;
    }
}

MorletTransform::MorletTransform(int period, int width, int time_lag, bool real_imag)
    : period_(period)
    , width_(width)
    , lag_(time_lag)
    , real_vs_imag_(real_imag)
    , valid_(false)
{
    // Validate parameters
    if (period < 2 || width < period || time_lag < 0 || time_lag > width) {
        return;
    }

    npts_ = 2 * width + 1;
    freq_ = 1.0 / period;
    fwidth_ = 1.0 / width;

    // Calculate required FFT size with padding
    int pad = width - time_lag;
    n_ = 2;
    while (n_ < npts_ + pad && n_ < (1 << 30)) {  // Safety limit
        n_ *= 2;
    }

    // Allocate work arrays
    xr_.resize(n_);
    xi_.resize(n_);
    yr_.resize(n_);
    yi_.resize(n_);

    // Create FFT object
    fft_ = std::make_unique<FFT>(n_);
    if (!fft_ || !fft_->is_valid()) {
        return;
    }

    valid_ = true;
}

double MorletTransform::transform(const double* x, int n_input)
{
    if (!valid_ || n_input < npts_) {
        return 0.0;
    }

    // Step 1: Copy data and compute mean
    double mean = 0.0;
    for (int i = 0; i < npts_; ++i) {
        xr_[i] = x[i];
        xi_[i] = 0.0;
        mean += xr_[i];
    }
    mean /= npts_;

    // Step 2: Center the data
    for (int i = 0; i < npts_; ++i) {
        xr_[i] -= mean;
    }

    // Step 3: Pad with zeros
    for (int i = npts_; i < n_; ++i) {
        xr_[i] = 0.0;
        xi_[i] = 0.0;
    }

    // Step 4: Forward FFT
    fft_->transform(xr_.data(), xi_.data(), 1);

    // Step 5: Apply frequency-domain filter
    int half_n = n_ / 2;

    // Compute normalizer
    double normalizer = frequency_weight(freq_, freq_, fwidth_, real_vs_imag_);
    if (normalizer < 1.e-140) {
        normalizer = 1.e-140;
    }

    // Apply weights to all frequencies
    for (int i = 1; i < half_n; ++i) {
        double f = static_cast<double>(i) / static_cast<double>(n_);
        double wt = frequency_weight(f, freq_, fwidth_, real_vs_imag_) / normalizer;

        if (real_vs_imag_) {
            // Real transform: multiply by symmetric real function
            yr_[i] = xr_[i] * wt;
            yi_[i] = xi_[i] * wt;
            yr_[n_ - i] = xr_[n_ - i] * wt;
            yi_[n_ - i] = xi_[n_ - i] * wt;
        }
        else {
            // Imaginary transform: multiply by -i (antisymmetric)
            yr_[i] = -xi_[i] * wt;
            yi_[i] = xr_[i] * wt;
            yr_[n_ - i] = xi_[n_ - i] * wt;
            yi_[n_ - i] = -xr_[n_ - i] * wt;
        }
    }

    // Handle DC and Nyquist components
    yr_[0] = 0.0;
    yi_[0] = 0.0;
    yi_[half_n] = 0.0;

    if (real_vs_imag_) {
        double wt = frequency_weight(0.5, freq_, fwidth_, real_vs_imag_) / normalizer;
        yr_[half_n] = xr_[half_n] * wt;
    }
    else {
        yr_[half_n] = 0.0;
    }

    // Step 6: Inverse FFT
    fft_->transform(yr_.data(), yi_.data(), -1);

    // Step 7: Extract value at lag and normalize by n
    double value = yr_[lag_] / n_;

    return value;
}

// ============================================================================
// Daubechies Transform Implementation
// ============================================================================

void DaubechiesTransform::single_level(std::span<double> data, bool forward)
{
    const int n = data.size();
    const int nh = n / 2;

    if (work_.size() < static_cast<size_t>(n)) {
        work_.resize(n);
    }

    int j = 0;  // Declare j outside if/else blocks

    if (forward) {
        // Forward transform
        for (int i = 0; i < nh - 1; ++i) {
            work_[i] = C0 * data[j] + C1 * data[j + 1] + C2 * data[j + 2] + C3 * data[j + 3];
            work_[i + nh] = C3 * data[j] - C2 * data[j + 1] + C1 * data[j + 2] - C0 * data[j + 3];
            j += 2;
        }
        // Wrap-around for last coefficient
        work_[nh - 1] = C0 * data[n - 2] + C1 * data[n - 1] + C2 * data[0] + C3 * data[1];
        work_[nh - 1 + nh] = C3 * data[n - 2] - C2 * data[n - 1] + C1 * data[0] - C0 * data[1];
    }
    else {
        // Reverse transform
        work_[0] = C2 * data[nh - 1] + C1 * data[n - 1] + C0 * data[0] + C3 * data[nh];
        work_[1] = C3 * data[nh - 1] - C0 * data[n - 1] + C1 * data[0] - C2 * data[nh];
        j = 2;
        for (int i = 0; i < nh - 1; ++i) {
            work_[j++] = C2 * data[i] + C1 * data[i + nh] + C0 * data[i + 1] + C3 * data[i + nh + 1];
            work_[j++] = C3 * data[i] - C0 * data[i + nh] + C1 * data[i + 1] - C2 * data[i + nh + 1];
        }
    }

    // Copy back
    std::copy(work_.begin(), work_.begin() + n, data.begin());
}

void DaubechiesTransform::forward(std::span<double> data, int level)
{
    int n_reduced = data.size();

    for (int j = 0; j < level; ++j) {
        single_level(data.subspan(0, n_reduced), true);
        n_reduced /= 2;
    }
}

void DaubechiesTransform::inverse(std::span<double> data, int level)
{
    int n = data.size();
    int n_reduced = n;

    // Find starting size
    for (int j = 1; j < level; ++j) {
        n_reduced /= 2;
    }

    // Apply inverse transforms
    for (int j = 0; j < level; ++j) {
        single_level(data.subspan(0, n_reduced), false);
        n_reduced *= 2;
    }
}

double DaubechiesTransform::compute_mean(std::span<double> data, int level)
{
    // Make a copy since forward transform modifies data
    std::vector<double> temp(data.begin(), data.end());

    forward(temp, level);

    // Number of parent coefficients
    int nn = data.size();
    for (int j = 0; j < level; ++j) {
        nn /= 2;
    }

    // Compute mean
    double sum = 0.0;
    for (int i = 0; i < nn; ++i) {
        sum += temp[i];
    }

    return sum / nn;
}

double DaubechiesTransform::compute_min(std::span<double> data, int level)
{
    std::vector<double> temp(data.begin(), data.end());
    forward(temp, level);

    int nn = data.size();
    for (int j = 0; j < level; ++j) {
        nn /= 2;
    }

    double min_val = temp[0];
    for (int i = 1; i < nn; ++i) {
        if (temp[i] < min_val) {
            min_val = temp[i];
        }
    }

    return min_val;
}

double DaubechiesTransform::compute_max(std::span<double> data, int level)
{
    std::vector<double> temp(data.begin(), data.end());
    forward(temp, level);

    int nn = data.size();
    for (int j = 0; j < level; ++j) {
        nn /= 2;
    }

    double max_val = temp[0];
    for (int i = 1; i < nn; ++i) {
        if (temp[i] > max_val) {
            max_val = temp[i];
        }
    }

    return max_val;
}

double DaubechiesTransform::compute_std(std::span<double> data, int level)
{
    std::vector<double> temp(data.begin(), data.end());
    forward(temp, level);

    int nn = data.size();
    for (int j = 0; j < level; ++j) {
        nn /= 2;
    }

    // Compute mean
    double sum = 0.0;
    for (int i = 0; i < nn; ++i) {
        sum += temp[i];
    }
    double mean = sum / nn;

    // Compute variance
    sum = 0.0;
    for (int i = 0; i < nn; ++i) {
        double diff = temp[i] - mean;
        sum += diff * diff;
    }

    return std::sqrt(sum / nn);
}

double DaubechiesTransform::compute_energy(std::span<double> data, int level)
{
    std::vector<double> temp(data.begin(), data.end());
    forward(temp, level);

    int nn = data.size();
    for (int j = 0; j < level; ++j) {
        nn /= 2;
    }

    double sum = 0.0;
    for (int i = 0; i < nn; ++i) {
        sum += temp[i] * temp[i];
    }

    return sum / nn;
}

double DaubechiesTransform::compute_nl_energy(std::span<double> data, int level)
{
    std::vector<double> temp(data.begin(), data.end());
    forward(temp, level);

    int nn = data.size();
    for (int j = 0; j < level; ++j) {
        nn /= 2;
    }

    // Non-linear energy: sum of |x[i]^2 - x[i-1]*x[i+1]|
    double sum = 0.0;
    for (int i = 1; i < nn - 1; ++i) {
        sum += std::fabs(temp[i] * temp[i] - temp[i - 1] * temp[i + 1]);
    }

    return std::sqrt(sum / nn);
}

double DaubechiesTransform::compute_curve(std::span<double> data, int level)
{
    std::vector<double> temp(data.begin(), data.end());
    forward(temp, level);

    int nn = data.size();
    for (int j = 0; j < level; ++j) {
        nn /= 2;
    }

    // Curve: sum of absolute differences between neighbors
    double sum = 0.0;
    for (int i = 1; i < nn; ++i) {
        sum += std::fabs(temp[i] - temp[i - 1]);
    }

    return sum / nn;
}

// ============================================================================
// Utility Functions
// ============================================================================

namespace wavelet_utils {

int next_power_of_2(int n)
{
    if (n <= 0) {
        return 1;
    }

    int power = 1;
    while (power < n && power < (1 << 30)) {  // Safety limit
        power *= 2;
    }

    return power;
}

bool is_power_of_2(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

int log2_int(int n)
{
    if (n <= 0) {
        return 0;
    }

    int log = 0;
    while (n > 1) {
        n /= 2;
        ++log;
    }

    return log;
}

} // namespace wavelet_utils

} // namespace helpers
} // namespace tssb

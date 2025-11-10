#pragma once

#include <complex>
#include <memory>
#include <span>
#include <vector>

namespace tssb {
namespace helpers {

/**
 * @brief Simple FFT implementation using Cooley-Tukey algorithm
 *
 * This class provides forward and inverse FFT for complex data.
 * The size must be a power of 2.
 */
class FFT {
public:
    /**
     * @brief Construct FFT for given size
     * @param n Size (must be power of 2)
     */
    explicit FFT(int n);

    /**
     * @brief Check if FFT is properly initialized
     */
    bool is_valid() const { return valid_; }

    /**
     * @brief Perform complex FFT
     * @param real Real part of data (in/out)
     * @param imag Imaginary part of data (in/out)
     * @param direction 1 for forward, -1 for inverse
     */
    void transform(double* real, double* imag, int direction);

private:
    int n_;
    bool valid_;
    std::vector<double> cos_table_;
    std::vector<double> sin_table_;

    void bit_reverse(double* real, double* imag);
};

/**
 * @brief Morlet wavelet transform
 *
 * Based on MORLET.CPP from TSSB. Provides frequency-domain wavelet
 * analysis with excellent time-frequency localization.
 *
 * The Morlet wavelet is complex (has real and imaginary components).
 * - Real component measures position within periodic cycle
 * - Imaginary component measures velocity within periodic cycle
 */
class MorletTransform {
public:
    /**
     * @brief Construct Morlet wavelet transformer
     *
     * @param period Wavelet period (must be >= 2, Nyquist limit)
     * @param width Half-width of filter (should be >= period, typically 2*period)
     * @param time_lag Time lag (0 to width, typically equals width)
     * @param real_imag true for real component, false for imaginary
     *
     * Error conditions:
     * - period < 2 (Nyquist limit)
     * - width < period (insufficient sample points)
     * - time_lag < 0 or time_lag > width (invalid lag)
     */
    MorletTransform(int period, int width, int time_lag, bool real_imag);

    /**
     * @brief Check if transform is properly initialized
     */
    bool is_valid() const { return valid_; }

    /**
     * @brief Perform Morlet wavelet transform
     *
     * @param x Input data in REVERSE time order (x[0] = most recent)
     * @param n_input Number of input points
     * @return Transformed value at the specified lag
     *
     * The transform:
     * 1. Copies and centers input data
     * 2. Pads to power of 2
     * 3. Applies forward FFT
     * 4. Multiplies by frequency-domain Morlet weight function
     * 5. Applies inverse FFT
     * 6. Extracts value at lag position
     */
    double transform(const double* x, int n_input);

    /**
     * @brief Get parameters for verification
     */
    int get_period() const { return period_; }
    int get_width() const { return width_; }
    int get_lag() const { return lag_; }
    bool is_real() const { return real_vs_imag_; }

private:
    int period_;      // Period parameter
    int width_;       // Width parameter (half-width)
    int lag_;         // Time lag
    bool real_vs_imag_;  // true=real, false=imaginary

    int npts_;        // 2 * width + 1
    int n_;           // FFT size (power of 2)
    double freq_;     // 1.0 / period
    double fwidth_;   // 1.0 / width

    bool valid_;

    std::unique_ptr<FFT> fft_;
    std::vector<double> xr_, xi_, yr_, yi_;  // Work arrays

    /**
     * @brief Compute Morlet frequency-domain weight
     *
     * This is the core of the Morlet wavelet - a Gaussian envelope
     * in the frequency domain.
     *
     * @param f Frequency (0 to 0.5)
     * @param w Center frequency
     * @param r Frequency-domain width
     * @param is_real true for real part, false for imaginary
     */
    static double frequency_weight(double f, double w, double r, bool is_real);
};

/**
 * @brief Daubechies-4 wavelet transform
 *
 * Based on DAUBECHI.CPP from TSSB. Provides orthogonal wavelet
 * decomposition with zero redundancy.
 *
 * The Daubechies-4 wavelet has:
 * - Perfect reconstruction (forward + reverse = identity)
 * - Zero redundancy (minimal number of coefficients)
 * - Poor time-frequency localization (trade-off vs Morlet)
 */
class DaubechiesTransform {
public:
    /**
     * @brief Construct Daubechies transformer
     *
     * No initialization needed - stateless transform
     */
    DaubechiesTransform() = default;

    /**
     * @brief Perform multi-level Daubechies-4 forward transform
     *
     * @param data Input data (will be modified in-place)
     * @param n Number of points (MUST be power of 2)
     * @param level Number of decomposition levels (1-4 typical)
     *
     * After J levels, the first n/(2^J) elements contain the
     * parent (smooth) wavelet coefficients. The rest are detail
     * coefficients at various scales.
     *
     * Constraint: 2^(level+1) <= n
     */
    void forward(std::span<double> data, int level);

    /**
     * @brief Perform multi-level Daubechies-4 inverse transform
     *
     * Reverses the forward transform to reconstruct original data.
     *
     * @param data Wavelet coefficients (will be modified in-place)
     * @param n Number of points (MUST be power of 2)
     * @param level Number of decomposition levels
     */
    void inverse(std::span<double> data, int level);

    /**
     * @brief Extract mean of parent wavelet coefficients
     *
     * Performs forward transform and returns mean of smooth coefficients.
     * This is the DAUB MEAN indicator.
     *
     * @param data Input data (log close ratios)
     * @param level Decomposition level
     * @return Mean of parent coefficients
     */
    double compute_mean(std::span<double> data, int level);

    /**
     * @brief Extract minimum of parent wavelet coefficients
     */
    double compute_min(std::span<double> data, int level);

    /**
     * @brief Extract maximum of parent wavelet coefficients
     */
    double compute_max(std::span<double> data, int level);

    /**
     * @brief Extract standard deviation of parent wavelet coefficients
     */
    double compute_std(std::span<double> data, int level);

    /**
     * @brief Extract energy (mean squared) of parent coefficients
     */
    double compute_energy(std::span<double> data, int level);

    /**
     * @brief Extract non-linear energy of parent coefficients
     *
     * Measures squared differences between neighbors:
     * sum(|x[i]^2 - x[i-1]*x[i+1]|)
     */
    double compute_nl_energy(std::span<double> data, int level);

    /**
     * @brief Extract curve (total variation) of parent coefficients
     *
     * Measures absolute differences between neighbors:
     * sum(|x[i] - x[i-1]|)
     */
    double compute_curve(std::span<double> data, int level);

private:
    std::vector<double> work_;  // Work array for transforms

    /**
     * @brief Single-level Daubechies-4 transform
     *
     * @param data Input/output data
     * @param n Size of data
     * @param forward true for forward, false for reverse
     */
    void single_level(std::span<double> data, bool forward);

    // Daubechies-4 wavelet coefficients
    static constexpr double C0 = 0.4829629131445341;
    static constexpr double C1 = 0.8365163037378079;
    static constexpr double C2 = 0.2241438680420134;
    static constexpr double C3 = -0.1294095225512604;
};

/**
 * @brief Utility functions for wavelet indicators
 */
namespace wavelet_utils {

/**
 * @brief Round up to next power of 2
 */
int next_power_of_2(int n);

/**
 * @brief Check if n is a power of 2
 */
bool is_power_of_2(int n);

/**
 * @brief Compute log base 2 of power-of-2 integer
 */
int log2_int(int n);

} // namespace wavelet_utils

} // namespace helpers
} // namespace tssb

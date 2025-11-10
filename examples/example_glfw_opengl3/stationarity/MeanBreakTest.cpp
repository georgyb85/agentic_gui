#include "MeanBreakTest.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace stationarity {

namespace {

double betacf(double a, double b, double x) {
    const int MAX_ITER = 300;
    const double EPS = 1e-13;
    const double FPMIN = std::numeric_limits<double>::min() / EPS;

    double qab = a + b;
    double qap = a + 1.0;
    double qam = a - 1.0;

    double c = 1.0;
    double d = 1.0 - (qab * x) / qap;
    if (std::fabs(d) < FPMIN) {
        d = FPMIN;
    }
    d = 1.0 / d;
    double h = d;

    for (int m = 1; m <= MAX_ITER; ++m) {
        int m2 = 2 * m;

        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c;
        if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        h *= d * c;

        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c;
        if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < EPS) {
            break;
        }
    }
    return h;
}

double regularizedIncompleteBeta(double a, double b, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;

    double lnBeta = std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
    double front = std::exp(a * std::log(x) + b * std::log(1.0 - x) - lnBeta);

    if (x < (a + 1.0) / (a + b + 2.0)) {
        return front * betacf(a, b, x) / a;
    } else {
        return 1.0 - front * betacf(b, a, 1.0 - x) / b;
    }
}

double fCDF(int d1, int d2, double F) {
    if (F <= 0.0) {
        return 0.0;
    }
    double x = d2 / (d2 + d1 * F);
    return 1.0 - regularizedIncompleteBeta(0.5 * d2, 0.5 * d1, x);
}

} // namespace

MeanBreakTest::MeanBreakTest(MeanBreakConfig config)
    : m_config(std::move(config)) {
    if (m_config.minSegmentLength < 2) {
        m_config.minSegmentLength = 2;
    }
}

MeanBreakResult MeanBreakTest::run(const Eigen::VectorXd& series,
                                   std::function<void(double)> progressCallback) const {
    const int n = static_cast<int>(series.size());
    if (n < 2 * m_config.minSegmentLength + 1) {
        throw std::invalid_argument("Series is too short for the requested minimum segment length");
    }

    Eigen::VectorXd data = series;
    if (m_config.standardize) {
        double mean = data.mean();
        Eigen::ArrayXd centered = data.array() - mean;
        double stddev = std::sqrt(centered.square().mean());
        if (stddev > 0.0) {
            data = centered / stddev;
        } else {
            data = Eigen::VectorXd::Zero(n);
        }
    }

    std::vector<double> prefixSum(n + 1, 0.0);
    std::vector<double> prefixSq(n + 1, 0.0);
    for (int i = 0; i < n; ++i) {
        prefixSum[i + 1] = prefixSum[i] + data[i];
        prefixSq[i + 1] = prefixSq[i] + data[i] * data[i];
    }

    double totalSum = prefixSum[n];
    double totalSq = prefixSq[n];
    double overallMean = totalSum / n;
    double sseSingle = totalSq - (totalSum * totalSum) / n;
    if (sseSingle < 0.0) sseSingle = 0.0;

    double bestF = -std::numeric_limits<double>::infinity();
    int bestIndex = -1;
    double bestMean1 = 0.0;
    double bestMean2 = 0.0;
    double bestSSE1 = 0.0;
    double bestSSE2 = 0.0;

    const int start = m_config.minSegmentLength;
    const int end = n - m_config.minSegmentLength;
    const int totalCandidates = end - start + 1;

    for (int k = start; k <= end; ++k) {
        int n1 = k;
        int n2 = n - k;
        double sum1 = prefixSum[k];
        double sum2 = totalSum - sum1;
        double sse1 = prefixSq[k] - (sum1 * sum1) / n1;
        double sse2 = (totalSq - prefixSq[k]) - (sum2 * sum2) / n2;
        sse1 = std::max(0.0, sse1);
        sse2 = std::max(0.0, sse2);

        double denom = sse1 + sse2;
        if (denom <= 0.0) {
            if (progressCallback) {
                double fraction = static_cast<double>(k - start + 1) / static_cast<double>(totalCandidates);
                progressCallback(fraction);
            }
            continue;
        }

        double numerator = sseSingle - denom;
        if (numerator <= 0.0) {
            if (progressCallback) {
                double fraction = static_cast<double>(k - start + 1) / static_cast<double>(totalCandidates);
                progressCallback(fraction);
            }
            continue;
        }

        double F = (numerator) / (denom / (n - 2));
        if (F > bestF) {
            bestF = F;
            bestIndex = k;
            bestMean1 = sum1 / n1;
            bestMean2 = sum2 / n2;
            bestSSE1 = sse1;
            bestSSE2 = sse2;
        }

        if (progressCallback) {
            double fraction = static_cast<double>(k - start + 1) / static_cast<double>(totalCandidates);
            progressCallback(fraction);
        }
    }

    MeanBreakResult result;
    if (bestIndex < 0 || !std::isfinite(bestF)) {
        return result;
    }

    result.valid = true;
    result.breakIndex = bestIndex;
    result.meanBefore = bestMean1;
    result.meanAfter = bestMean2;
    result.overallMean = overallMean;
    result.sseBefore = bestSSE1;
    result.sseAfter = bestSSE2;
    result.sseCombined = bestSSE1 + bestSSE2;
    result.sseSingle = sseSingle;
    result.effectSize = bestMean2 - bestMean1;
    result.fStatistic = bestF;

    double cdf = fCDF(1, n - 2, bestF);
    result.pValue = 1.0 - cdf;
    if (result.pValue < 0.0) result.pValue = 0.0;
    if (result.pValue > 1.0) result.pValue = 1.0;

    if (progressCallback) {
        progressCallback(1.0);
    }

    return result;
}

} // namespace stationarity


#pragma once

#include "HmmModel.h"

#include <stdexcept>

#ifdef HMM_WITH_CUDA
#include <functional>
#include <random>

namespace hmm {

struct HmmGpuLimits {
    static constexpr int kMaxStates = 8;
    static constexpr int kMaxFeatures = 6;
};

bool HmmGpuAvailable();
bool HmmGpuSupports(int numStates, int numFeatures);

HmmFitResult FitHmmGpu(const Eigen::MatrixXd& observations,
                       const HmmModelConfig& config,
                       std::mt19937_64& rng,
                       const std::function<void(int, double)>& progressCallback);

} // namespace hmm

#else

namespace hmm {
struct HmmGpuLimits {
    static constexpr int kMaxStates = 0;
    static constexpr int kMaxFeatures = 0;
};

inline bool HmmGpuAvailable() { return false; }
inline bool HmmGpuSupports(int, int) { return false; }

template<typename... Args>
inline HmmFitResult FitHmmGpu(Args&&...) {
    throw std::runtime_error("GPU support not compiled (HMM_WITH_CUDA undefined)");
}

} // namespace hmm

#endif // HMM_WITH_CUDA

#ifdef HMM_WITH_CUDA

#include "HmmGpu.h"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstddef>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace hmm {

namespace {

constexpr double LOG_TWO_PI = 1.83787706640934548356065947281123539;
constexpr int MAX_GPU_FEATURES = HmmGpuLimits::kMaxFeatures;
constexpr int MAX_GPU_STATES = HmmGpuLimits::kMaxStates;

#define CUDA_CHECK(expr)                                                                     \
    do {                                                                                     \
        cudaError_t _err = (expr);                                                           \
        if (_err != cudaSuccess) {                                                           \
            throw std::runtime_error(std::string("CUDA error: ") + cudaGetErrorString(_err));\
        }                                                                                    \
    } while (false)

__device__ double device_log_sum_exp(const double* values, int count) {
    double max_val = values[0];
    for (int i = 1; i < count; ++i) {
        max_val = values[i] > max_val ? values[i] : max_val;
    }
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        sum += exp(values[i] - max_val);
    }
    return max_val + log(sum);
}

__global__ void compute_log_emission_kernel(const double* __restrict__ data,
                                            const double* __restrict__ means,
                                            const double* __restrict__ inv_cov,
                                            const double* __restrict__ log_det,
                                            double* __restrict__ log_emission,
                                            int num_observations,
                                            int num_states,
                                            int num_features) {
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    int state = blockIdx.y;
    if (t >= num_observations || state >= num_states) {
        return;
    }

    const double* obs = data + t * num_features;
    const double* mean = means + state * num_features;
    const double* inv = inv_cov + state * num_features * num_features;

    double diff[MAX_GPU_FEATURES];
    #pragma unroll
    for (int f = 0; f < num_features; ++f) {
        diff[f] = obs[f] - mean[f];
    }

    double quad = 0.0;
    #pragma unroll
    for (int row = 0; row < num_features; ++row) {
        double inner = 0.0;
        const double* inv_row = inv + row * num_features;
        #pragma unroll
        for (int col = 0; col < num_features; ++col) {
            inner += inv_row[col] * diff[col];
        }
        quad += diff[row] * inner;
    }

    double value = -0.5 * (num_features * LOG_TWO_PI + log_det[state] + quad);
    log_emission[state * num_observations + t] = value;
}

__global__ void forward_kernel(const double* __restrict__ log_init,
                               const double* __restrict__ log_transition,
                               const double* __restrict__ log_emission,
                               double* __restrict__ alpha,
                               int num_observations,
                               int num_states,
                               double* __restrict__ log_likelihood_out) {
    int state = threadIdx.x;
    if (state >= num_states) {
        return;
    }

    alpha[state] = log_init[state] + log_emission[state * num_observations];
    __syncthreads();

    extern __shared__ double shared_row[];

    for (int t = 1; t < num_observations; ++t) {
        // gather values needed for log-sum-exp
        for (int i = threadIdx.x; i < num_states; i += blockDim.x) {
            shared_row[i] = alpha[(t - 1) * num_states + i];
        }
        __syncthreads();

        double candidates[MAX_GPU_STATES];
        #pragma unroll
        for (int i = 0; i < num_states; ++i) {
            candidates[i] = shared_row[i] + log_transition[i * num_states + state];
        }
        double log_sum = device_log_sum_exp(candidates, num_states);
        alpha[t * num_states + state] = log_emission[state * num_observations + t] + log_sum;
        __syncthreads();
    }

    if (state == 0) {
        double final_values[MAX_GPU_STATES];
        #pragma unroll
        for (int i = 0; i < num_states; ++i) {
            final_values[i] = alpha[(num_observations - 1) * num_states + i];
        }
        *log_likelihood_out = device_log_sum_exp(final_values, num_states);
    }
}

__global__ void backward_kernel(const double* __restrict__ log_transition,
                                const double* __restrict__ log_emission,
                                double* __restrict__ beta,
                                int num_observations,
                                int num_states) {
    int state = threadIdx.x;
    if (state >= num_states) {
        return;
    }

    beta[(num_observations - 1) * num_states + state] = 0.0;
    __syncthreads();

    extern __shared__ double shared_row[];

    for (int t = num_observations - 2; t >= 0; --t) {
        for (int j = threadIdx.x; j < num_states; j += blockDim.x) {
            shared_row[j] = beta[(t + 1) * num_states + j] + log_emission[j * num_observations + (t + 1)];
        }
        __syncthreads();

        double candidates[MAX_GPU_STATES];
        #pragma unroll
        for (int j = 0; j < num_states; ++j) {
            candidates[j] = log_transition[state * num_states + j] + shared_row[j];
        }
        beta[t * num_states + state] = device_log_sum_exp(candidates, num_states);
        __syncthreads();
    }
}

__global__ void compute_gamma_kernel(const double* __restrict__ alpha,
                                     const double* __restrict__ beta,
                                     double log_likelihood,
                                     double* __restrict__ gamma,
                                     int num_observations,
                                     int num_states) {
    int state = blockIdx.y;
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (state >= num_states || t >= num_observations) {
        return;
    }

    double value = alpha[t * num_states + state] + beta[t * num_states + state] - log_likelihood;
    gamma[t * num_states + state] = exp(value);
}

__global__ void normalize_gamma_kernel(double* __restrict__ gamma,
                                       double* __restrict__ gamma_sums,
                                       int num_observations,
                                       int num_states) {
    int t = blockIdx.x;
    int state = threadIdx.x;
    extern __shared__ double shared[];

    if (state < num_states) {
        shared[state] = gamma[t * num_states + state];
    }
    __syncthreads();

    double row_sum = 0.0;
    if (state == 0) {
        for (int i = 0; i < num_states; ++i) {
            row_sum += shared[i];
        }
        shared[num_states] = row_sum;
    }
    __syncthreads();

    row_sum = shared[num_states];
    if (row_sum <= 0.0) {
        row_sum = 1.0;
    }
    if (state < num_states) {
        double normalized = shared[state] / row_sum;
        gamma[t * num_states + state] = normalized;
        atomicAdd(&gamma_sums[state], normalized);
    }
}

__global__ void compute_xi_kernel(const double* __restrict__ alpha,
                                  const double* __restrict__ beta,
                                  const double* __restrict__ log_transition,
                                  const double* __restrict__ log_emission,
                                  double log_likelihood,
                                  double* __restrict__ xi_sum,
                                  int num_observations,
                                  int num_states) {
    int j = blockIdx.x;
    int i = blockIdx.y;
    if (i >= num_states || j >= num_states) {
        return;
    }

    double accum = 0.0;
    for (int t = 0; t < num_observations - 1; ++t) {
        double value = alpha[t * num_states + i]
                     + log_transition[i * num_states + j]
                     + log_emission[j * num_observations + (t + 1)]
                     + beta[(t + 1) * num_states + j]
                     - log_likelihood;
        accum += exp(value);
    }
    xi_sum[i * num_states + j] = accum;
}

struct DeviceBuffers {
    double* d_observations = nullptr;
    double* d_means = nullptr;
    double* d_inv_cov = nullptr;
    double* d_log_det = nullptr;
    double* d_log_transition = nullptr;
    double* d_log_init = nullptr;
    double* d_log_emission = nullptr;
    double* d_alpha = nullptr;
    double* d_beta = nullptr;
    double* d_gamma = nullptr;
    double* d_gamma_sums = nullptr;
    double* d_xi_sum = nullptr;
    double* d_log_likelihood = nullptr;

    DeviceBuffers(int num_observations, int num_states, int num_features) {
        std::size_t obs_bytes = sizeof(double) * num_observations * num_features;
        std::size_t state_feature = sizeof(double) * num_states * num_features;
        std::size_t cov_bytes = sizeof(double) * num_states * num_features * num_features;
        std::size_t state_bytes = sizeof(double) * num_states;
        std::size_t state_sq_bytes = sizeof(double) * num_states * num_states;
        std::size_t seq_bytes = sizeof(double) * num_observations * num_states;

        CUDA_CHECK(cudaMalloc(&d_observations, obs_bytes));
        CUDA_CHECK(cudaMalloc(&d_means, state_feature));
        CUDA_CHECK(cudaMalloc(&d_inv_cov, cov_bytes));
        CUDA_CHECK(cudaMalloc(&d_log_det, state_bytes));
        CUDA_CHECK(cudaMalloc(&d_log_transition, state_sq_bytes));
        CUDA_CHECK(cudaMalloc(&d_log_init, state_bytes));
        CUDA_CHECK(cudaMalloc(&d_log_emission, sizeof(double) * num_states * num_observations));
        CUDA_CHECK(cudaMalloc(&d_alpha, seq_bytes));
        CUDA_CHECK(cudaMalloc(&d_beta, seq_bytes));
        CUDA_CHECK(cudaMalloc(&d_gamma, seq_bytes));
        CUDA_CHECK(cudaMalloc(&d_gamma_sums, state_bytes));
        CUDA_CHECK(cudaMalloc(&d_xi_sum, state_sq_bytes));
        CUDA_CHECK(cudaMalloc(&d_log_likelihood, sizeof(double)));
    }

    ~DeviceBuffers() {
        cudaFree(d_observations);
        cudaFree(d_means);
        cudaFree(d_inv_cov);
        cudaFree(d_log_det);
        cudaFree(d_log_transition);
        cudaFree(d_log_init);
        cudaFree(d_log_emission);
        cudaFree(d_alpha);
        cudaFree(d_beta);
        cudaFree(d_gamma);
        cudaFree(d_gamma_sums);
        cudaFree(d_xi_sum);
        cudaFree(d_log_likelihood);
    }
};

inline void initialize_parameters_host(const Eigen::MatrixXd& observations,
                                       const HmmModelConfig& config,
                                       HmmModelParameters& params,
                                       std::mt19937_64& rng) {
    int num_observations = static_cast<int>(observations.rows());
    int num_features = static_cast<int>(observations.cols());
    int num_states = config.numStates;

    params.initialProbabilities = Eigen::VectorXd::Constant(num_states, 1.0 / num_states);
    params.transitionMatrix = Eigen::MatrixXd::Constant(num_states, num_states, 1.0 / num_states);
    params.means.resize(num_states, num_features);
    params.covariances.assign(num_states, Eigen::MatrixXd::Identity(num_features, num_features));

    std::uniform_int_distribution<int> dist(0, num_observations - 1);
    for (int s = 0; s < num_states; ++s) {
        params.means.row(s) = observations.row(dist(rng));
    }

    Eigen::MatrixXd centered = observations.rowwise() - observations.colwise().mean();
    Eigen::MatrixXd shared_cov = (centered.transpose() * centered) / static_cast<double>(num_observations);
    shared_cov += Eigen::MatrixXd::Identity(num_features, num_features) * config.regularization;
    for (auto& cov : params.covariances) {
        cov = shared_cov;
    }
}

inline double ensure_positive_definite_host(Eigen::MatrixXd& matrix, double min_det) {
    double min_eps = min_det;
    for (int attempt = 0; attempt < 10; ++attempt) {
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(matrix);
        if (solver.eigenvalues().minCoeff() > 0.0) {
            return matrix.determinant();
        }
        matrix += Eigen::MatrixXd::Identity(matrix.rows(), matrix.cols()) * min_eps;
        min_eps *= 10.0;
    }
    return matrix.determinant();
}

} // namespace

bool HmmGpuAvailable() {
    static bool checked = false;
    static bool available = false;
    if (!checked) {
        int count = 0;
        available = (cudaGetDeviceCount(&count) == cudaSuccess) && (count > 0);
        checked = true;
    }
    return available;
}

bool HmmGpuSupports(int numStates, int numFeatures) {
    return numStates > 0 && numStates <= MAX_GPU_STATES &&
           numFeatures > 0 && numFeatures <= MAX_GPU_FEATURES;
}

HmmFitResult FitHmmGpu(const Eigen::MatrixXd& observations,
                       const HmmModelConfig& config,
                       std::mt19937_64& rng,
                       const std::function<void(int, double)>& progressCallback) {
    int num_observations = static_cast<int>(observations.rows());
    int num_features = static_cast<int>(observations.cols());
    int num_states = config.numStates;

    if (!HmmGpuAvailable() || !HmmGpuSupports(num_states, num_features)) {
        throw std::runtime_error("Requested HMM configuration exceeds GPU implementation limits");
    }

    DeviceBuffers buffers(num_observations, num_states, num_features);

    std::vector<double> host_observations(num_observations * num_features);
    for (int t = 0; t < num_observations; ++t) {
        for (int f = 0; f < num_features; ++f) {
            host_observations[t * num_features + f] = observations(t, f);
        }
    }
    CUDA_CHECK(cudaMemcpy(buffers.d_observations,
                          host_observations.data(),
                          sizeof(double) * host_observations.size(),
                          cudaMemcpyHostToDevice));

    HmmFitResult best_result;
    best_result.logLikelihood = -std::numeric_limits<double>::infinity();
    best_result.iterations = 0;
    best_result.converged = false;

    for (int restart = 0; restart < config.numRestarts; ++restart) {
        HmmModelParameters params;
        initialize_parameters_host(observations, config, params, rng);

        double previous_log_likelihood = -std::numeric_limits<double>::infinity();
        bool converged = false;
        int iteration = 0;

        std::vector<double> gamma_host(num_observations * num_states);
        std::vector<double> gamma_sums_host(num_states);
        std::vector<double> xi_sum_host(num_states * num_states);

        for (; iteration < config.maxIterations; ++iteration) {
            // Prepare device inputs
            Eigen::MatrixXd log_transition_eigen = params.transitionMatrix.array().max(1e-18).log().matrix();
            Eigen::VectorXd log_init = params.initialProbabilities.array().max(1e-18).log().matrix();

            std::vector<double> means_row_major(num_states * num_features);
            for (int s = 0; s < num_states; ++s) {
                for (int f = 0; f < num_features; ++f) {
                    means_row_major[s * num_features + f] = params.means(s, f);
                }
            }

            std::vector<double> inv_cov_host(num_states * num_features * num_features, 0.0);
            std::vector<double> log_det_host(num_states);

            for (int s = 0; s < num_states; ++s) {
                Eigen::MatrixXd cov = params.covariances[s];
                double det = ensure_positive_definite_host(cov, config.regularization);
                Eigen::MatrixXd inv = cov.inverse();
                log_det_host[s] = std::log(det);
                for (int r = 0; r < num_features; ++r) {
                    for (int c = 0; c < num_features; ++c) {
                        inv_cov_host[s * num_features * num_features + r * num_features + c] = inv(r, c);
                    }
                }
            }

            std::vector<double> log_transition_row_major(num_states * num_states);
            for (int i = 0; i < num_states; ++i) {
                for (int j = 0; j < num_states; ++j) {
                    log_transition_row_major[i * num_states + j] = log_transition_eigen(i, j);
                }
            }

            CUDA_CHECK(cudaMemcpy(buffers.d_means,
                                  means_row_major.data(),
                                  sizeof(double) * means_row_major.size(),
                                  cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(buffers.d_inv_cov,
                                  inv_cov_host.data(),
                                  sizeof(double) * inv_cov_host.size(),
                                  cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(buffers.d_log_det,
                                  log_det_host.data(),
                                  sizeof(double) * log_det_host.size(),
                                  cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(buffers.d_log_transition,
                                  log_transition_row_major.data(),
                                  sizeof(double) * log_transition_row_major.size(),
                                  cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(buffers.d_log_init,
                                  log_init.data(),
                                  sizeof(double) * num_states,
                                  cudaMemcpyHostToDevice));

            dim3 block_emission(256);
            dim3 grid_emission((num_observations + block_emission.x - 1) / block_emission.x, num_states);
            compute_log_emission_kernel<<<grid_emission, block_emission>>>(
                buffers.d_observations,
                buffers.d_means,
                buffers.d_inv_cov,
                buffers.d_log_det,
                buffers.d_log_emission,
                num_observations,
                num_states,
                num_features);
            CUDA_CHECK(cudaPeekAtLastError());

            dim3 block_states(num_states);
            size_t shared_forward = sizeof(double) * num_states;
            forward_kernel<<<1, block_states, shared_forward>>>(
                buffers.d_log_init,
                buffers.d_log_transition,
                buffers.d_log_emission,
                buffers.d_alpha,
                num_observations,
                num_states,
                buffers.d_log_likelihood);
            CUDA_CHECK(cudaPeekAtLastError());

            double log_likelihood_host = 0.0;
            CUDA_CHECK(cudaMemcpy(&log_likelihood_host,
                                  buffers.d_log_likelihood,
                                  sizeof(double),
                                  cudaMemcpyDeviceToHost));

            backward_kernel<<<1, block_states, shared_forward>>>(
                buffers.d_log_transition,
                buffers.d_log_emission,
                buffers.d_beta,
                num_observations,
                num_states);
            CUDA_CHECK(cudaPeekAtLastError());

            dim3 grid_gamma((num_observations + 255) / 256, num_states);
            compute_gamma_kernel<<<grid_gamma, 256>>>(
                buffers.d_alpha,
                buffers.d_beta,
                log_likelihood_host,
                buffers.d_gamma,
                num_observations,
                num_states);
            CUDA_CHECK(cudaPeekAtLastError());

            CUDA_CHECK(cudaMemset(buffers.d_gamma_sums, 0, sizeof(double) * num_states));
            size_t shared_gamma = sizeof(double) * (num_states + 1);
            normalize_gamma_kernel<<<num_observations, block_states, shared_gamma>>>(
                buffers.d_gamma,
                buffers.d_gamma_sums,
                num_observations,
                num_states);
            CUDA_CHECK(cudaPeekAtLastError());

            dim3 grid_xi(num_states, num_states);
            compute_xi_kernel<<<grid_xi, 1>>>(
                buffers.d_alpha,
                buffers.d_beta,
                buffers.d_log_transition,
                buffers.d_log_emission,
                log_likelihood_host,
                buffers.d_xi_sum,
                num_observations,
                num_states);
            CUDA_CHECK(cudaPeekAtLastError());

            CUDA_CHECK(cudaMemcpy(gamma_host.data(),
                                  buffers.d_gamma,
                                  sizeof(double) * gamma_host.size(),
                                  cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaMemcpy(gamma_sums_host.data(),
                                  buffers.d_gamma_sums,
                                  sizeof(double) * gamma_sums_host.size(),
                                  cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaMemcpy(xi_sum_host.data(),
                                  buffers.d_xi_sum,
                                  sizeof(double) * xi_sum_host.size(),
                                  cudaMemcpyDeviceToHost));

            if (progressCallback) {
                progressCallback(iteration, log_likelihood_host);
            }

            double improvement = log_likelihood_host - previous_log_likelihood;
            if (iteration > 0 && std::abs(improvement) < config.tolerance) {
                previous_log_likelihood = log_likelihood_host;
                converged = true;
                break;
            }
            previous_log_likelihood = log_likelihood_host;

            // M-step on host using gamma information
            Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
                gamma_matrix(gamma_host.data(), num_observations, num_states);

            params.initialProbabilities = gamma_matrix.row(0).transpose();
            double init_sum = params.initialProbabilities.sum();
            if (init_sum > 0.0) {
                params.initialProbabilities /= init_sum;
            }

            for (int s = 0; s < num_states; ++s) {
                double gamma_sum = gamma_sums_host[s];
                if (gamma_sum <= config.regularization) {
                    params.means.row(s) = observations.colwise().mean();
                    params.covariances[s] =
                        Eigen::MatrixXd::Identity(num_features, num_features) * config.regularization;
                    continue;
                }

                Eigen::VectorXd gamma_col = gamma_matrix.col(s);
                Eigen::RowVectorXd mean = (gamma_col.transpose() * observations) / gamma_sum;
                params.means.row(s) = mean;

                Eigen::MatrixXd centered = observations.rowwise() - mean;
                Eigen::MatrixXd weighted = centered.transpose() * gamma_col.asDiagonal() * centered;
                Eigen::MatrixXd cov = weighted / gamma_sum;
                cov += Eigen::MatrixXd::Identity(num_features, num_features) * config.regularization;
                ensure_positive_definite_host(cov, config.regularization);
                params.covariances[s] = cov;
            }

            Eigen::MatrixXd xi_sum = Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
                xi_sum_host.data(), num_states, num_states);
            for (int i = 0; i < num_states; ++i) {
                double row_sum = xi_sum.row(i).sum();
                if (row_sum > 0.0) {
                    params.transitionMatrix.row(i) = xi_sum.row(i) / row_sum;
                } else {
                    params.transitionMatrix.row(i).setConstant(1.0 / num_states);
                }
            }
        }

        double final_log_likelihood = previous_log_likelihood;
        if (final_log_likelihood > best_result.logLikelihood) {
            best_result.logLikelihood = final_log_likelihood;
            best_result.iterations = iteration + 1;
            best_result.converged = converged;
            best_result.parameters = params;
            best_result.statePosterior = Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
                gamma_host.data(), num_observations, num_states);
        }
    }

    return best_result;
}

} // namespace hmm

#endif // HMM_WITH_CUDA

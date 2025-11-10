#include "SimulationUtils.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace simulation {
namespace utils {

// Statistics implementation
TransformParams Statistics::CalculateTransformParams(const std::vector<float>& data) {
    TransformParams params;
    
    if (data.empty()) {
        return params;
    }
    
    // Calculate mean
    float sum = std::accumulate(data.begin(), data.end(), 0.0f);
    params.mean = sum / data.size();
    
    // Calculate standard deviation
    float sq_sum = std::inner_product(data.begin(), data.end(), data.begin(), 0.0f);
    params.std_dev = std::sqrt(sq_sum / data.size() - params.mean * params.mean);
    
    // Avoid division by zero
    if (params.std_dev < 1e-8f) {
        params.std_dev = 1.0f;
    }
    
    return params;
}

float Statistics::CalculateQuantile(const std::vector<float>& data, float quantile) {
    if (data.empty()) {
        return 0.0f;
    }
    
    std::vector<float> sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end());
    
    size_t index = static_cast<size_t>(quantile * (sorted_data.size() - 1));
    return sorted_data[index];
}

float Statistics::CalculateMedian(const std::vector<float>& data) {
    return CalculateQuantile(data, 0.5f);
}

float Statistics::CalculateStdDev(const std::vector<float>& data, float mean) {
    if (data.size() <= 1) {
        return 0.0f;
    }
    
    float sum_sq_diff = 0.0f;
    for (float value : data) {
        float diff = value - mean;
        sum_sq_diff += diff * diff;
    }
    
    return std::sqrt(sum_sq_diff / (data.size() - 1));
}

// Transform implementation
std::vector<float> Transform::TransformTargets(
    const std::vector<float>& targets,
    const TransformParams& params,
    bool use_tanh,
    bool use_standardization,
    float tanh_scaling_factor) {
    
    std::vector<float> transformed = targets;
    
    // IMPORTANT: The correct order is:
    // 1. Standardize (if requested)
    // 2. Scale by tanh_scaling_factor
    // 3. Apply tanh
    // This matches the old SimulationWindow implementation exactly
    
    if (use_standardization || use_tanh) {
        for (float& value : transformed) {
            // Step 1: Standardize
            if (use_standardization) {
                value = (value - params.mean) / params.std_dev;
            }
            
            // Step 2 & 3: Scale and apply tanh
            if (use_tanh) {
                // CRITICAL: multiply by scaling factor BEFORE tanh, not inside!
                value = std::tanh(value * tanh_scaling_factor);
            }
        }
    }
    
    return transformed;
}

float Transform::InverseTransformPrediction(
    float prediction,
    const TransformParams& params,
    bool use_tanh,
    bool use_standardization,
    float tanh_scaling_factor) {
    
    float result = prediction;
    
    // Inverse tanh transform
    if (use_tanh) {
        // atanh(x) = 0.5 * ln((1+x)/(1-x))
        // Clamp to avoid numerical issues
        result = std::max(-0.999f, std::min(0.999f, result));
        result = std::atanh(result) / tanh_scaling_factor;
    }
    
    // Inverse standardization
    if (use_standardization) {
        result = result * params.std_dev + params.mean;
    }
    
    return result;
}

// DataUtils implementation
std::vector<float> DataUtils::ExtractColumn(
    const float* data,
    int num_rows,
    int num_cols,
    int col_index) {
    
    std::vector<float> column(num_rows);
    for (int i = 0; i < num_rows; ++i) {
        column[i] = data[i * num_cols + col_index];
    }
    return column;
}

std::vector<float> DataUtils::ExtractRows(
    const float* data,
    int total_rows,
    int num_cols,
    int start_row,
    int end_row) {
    
    int num_rows = end_row - start_row;
    std::vector<float> result(num_rows * num_cols);
    
    for (int i = 0; i < num_rows; ++i) {
        for (int j = 0; j < num_cols; ++j) {
            result[i * num_cols + j] = data[(start_row + i) * num_cols + j];
        }
    }
    
    return result;
}

} // namespace utils
} // namespace simulation
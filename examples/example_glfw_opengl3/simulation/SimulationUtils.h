#pragma once

#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "SimulationTypes.h"

namespace simulation {
namespace utils {

// Statistical utilities
class Statistics {
public:
    static float CalculateMedian(std::vector<float>& values) {
        if (values.empty()) return 0.0f;
        
        std::sort(values.begin(), values.end());
        size_t n = values.size();
        
        if (n % 2 == 0) {
            return (values[n / 2 - 1] + values[n / 2]) / 2.0f;
        } else {
            return values[n / 2];
        }
    }
    
    static float CalculateStdDev(const std::vector<float>& values, float mean) {
        if (values.size() <= 1) return 0.0f;
        
        float sum_sq_diff = 0.0f;
        for (float val : values) {
            float diff = val - mean;
            sum_sq_diff += diff * diff;
        }
        return std::sqrt(sum_sq_diff / (values.size() - 1));
    }
    
    static float CalculateQuantile(std::vector<float> values, float quantile) {
        if (values.empty()) return 0.0f;
        
        std::sort(values.begin(), values.end());
        size_t n = values.size();
        float pos = quantile * (n - 1);
        size_t lower = static_cast<size_t>(std::floor(pos));
        size_t upper = static_cast<size_t>(std::ceil(pos));
        
        if (lower == upper) {
            return values[lower];
        }
        
        float weight = pos - lower;
        return values[lower] * (1.0f - weight) + values[upper] * weight;
    }
    
    static TransformParams CalculateTransformParams(const std::vector<float>& data) {
        TransformParams params;
        
        if (data.empty()) {
            params.mean = 0.0f;
            params.std_dev = 1.0f;
            return params;
        }
        
        float sum = std::accumulate(data.begin(), data.end(), 0.0f);
        params.mean = sum / data.size();
        
        float sq_sum = 0.0f;
        for (float val : data) {
            float diff = val - params.mean;
            sq_sum += diff * diff;
        }
        params.std_dev = std::sqrt(sq_sum / data.size());
        
        if (params.std_dev == 0.0f) {
            params.std_dev = 1.0f;
        }
        
        return params;
    }
};

// Data transformation utilities
class Transform {
public:
    static std::vector<float> ApplyTanhTransform(
        const std::vector<float>& data,
        const TransformParams& params,
        float scaling_factor) {
        
        std::vector<float> transformed;
        transformed.reserve(data.size());
        
        for (float val : data) {
            float standardized = (val - params.mean) / params.std_dev;
            float scaled = standardized * scaling_factor;
            transformed.push_back(std::tanh(scaled));
        }
        
        return transformed;
    }
    
    static std::vector<float> ApplyStandardization(
        const std::vector<float>& data,
        const TransformParams& params) {
        
        std::vector<float> transformed;
        transformed.reserve(data.size());
        
        for (float val : data) {
            transformed.push_back((val - params.mean) / params.std_dev);
        }
        
        return transformed;
    }
    
    static float InverseTanhTransform(
        float transformed_value,
        const TransformParams& params,
        float scaling_factor) {
        
        float clamped = std::max(-0.9999f, std::min(0.9999f, transformed_value));
        float atanh_val = 0.5f * std::log((1.0f + clamped) / (1.0f - clamped));
        return (atanh_val / scaling_factor) * params.std_dev + params.mean;
    }
    
    static float InverseStandardization(
        float standardized_value,
        const TransformParams& params) {
        
        return standardized_value * params.std_dev + params.mean;
    }
    
    static std::vector<float> TransformTargets(
        const std::vector<float>& targets,
        const TransformParams& params,
        bool use_tanh,
        bool use_standardization,
        float tanh_scaling_factor) {
        
        if (use_tanh) {
            return ApplyTanhTransform(targets, params, tanh_scaling_factor);
        } else if (use_standardization) {
            return ApplyStandardization(targets, params);
        } else {
            return targets;  // No transformation
        }
    }
    
    static float InverseTransformPrediction(
        float prediction,
        const TransformParams& params,
        bool use_tanh,
        bool use_standardization,
        float tanh_scaling_factor) {
        
        if (use_tanh) {
            return InverseTanhTransform(prediction, params, tanh_scaling_factor);
        } else if (use_standardization) {
            return InverseStandardization(prediction, params);
        } else {
            return prediction;  // No transformation
        }
    }
};

// Metrics calculation
class Metrics {
public:
    static void CalculateTradingMetrics(
        const std::vector<float>& predictions,
        const std::vector<float>& actuals,
        float threshold,
        FoldResult& result) {
        
        result.n_signals = 0;
        result.signal_sum = 0.0f;
        int correct_signals = 0;
        std::vector<float> returns_on_signals;
        result.avg_predicted_return_on_signals = 0.0f;
        
        for (size_t i = 0; i < predictions.size(); ++i) {
            if (predictions[i] > threshold) {
                result.n_signals++;
                result.signal_sum += actuals[i];
                returns_on_signals.push_back(actuals[i]);
                
                if (actuals[i] > 0) {
                    correct_signals++;
                }
                result.avg_predicted_return_on_signals += predictions[i];
            }
        }
        
        if (result.n_signals > 0) {
            result.signal_rate = (float)result.n_signals / predictions.size();
            result.avg_return_on_signals = result.signal_sum / result.n_signals;
            result.median_return_on_signals = Statistics::CalculateMedian(returns_on_signals);
            result.std_return_on_signals = Statistics::CalculateStdDev(
                returns_on_signals, result.avg_return_on_signals);
            result.hit_rate = (float)correct_signals / result.n_signals;
            result.avg_predicted_return_on_signals /= result.n_signals;
        } else {
            result.signal_rate = 0.0f;
            result.avg_return_on_signals = 0.0f;
            result.median_return_on_signals = 0.0f;
            result.std_return_on_signals = 0.0f;
            result.hit_rate = 0.0f;
            result.avg_predicted_return_on_signals = 0.0f;
        }
    }
};

} // namespace utils
} // namespace simulation
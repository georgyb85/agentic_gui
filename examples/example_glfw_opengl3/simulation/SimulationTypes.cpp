#include "SimulationTypes.h"
#include "ISimulationModel_v2.h"
#include <iomanip>
#include <sstream>

namespace simulation {

void FoldResult::UpdateCache() const {
    if (!cache_dirty) return;
    
    fold_str = std::to_string(fold_number);
    signals_str = std::to_string(n_signals);
    
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.2f%%", signal_rate * 100.0f);
    rate_str = buffer;
    
    snprintf(buffer, sizeof(buffer), "%.6f", avg_return_on_signals);
    return_str = buffer;
    
    snprintf(buffer, sizeof(buffer), "%.2f%%", hit_rate * 100.0f);
    hit_str = buffer;
    
    snprintf(buffer, sizeof(buffer), "%.6f", running_sum);
    sum_str = buffer;
    
    cache_dirty = false;
}

void ModelCache::CacheModel(
    const ISimulationModel& model,
    const TransformParams& params,
    float pred_thresh_scaled,
    float pred_thresh_orig,
    float dyn_pos_thresh,
    int fold_number) {
    
    m_cached.is_valid = true;
    m_cached.model_type = model.GetModelType();
    m_cached.model_buffer = model.Serialize();
    m_cached.transform_params = params;
    m_cached.prediction_threshold_scaled = pred_thresh_scaled;
    m_cached.prediction_threshold_original = pred_thresh_orig;
    m_cached.dynamic_positive_threshold = dyn_pos_thresh;
    m_cached.source_fold = fold_number;
}

bool ModelCache::LoadCachedModel(
    ISimulationModel& model,
    TransformParams& params,
    float& pred_thresh_scaled,
    float& pred_thresh_orig,
    float& dyn_pos_thresh) const {
    
    if (!m_cached.is_valid) {
        return false;
    }
    
    // Verify model type matches
    if (model.GetModelType() != m_cached.model_type) {
        return false;
    }
    
    // Load model
    if (!model.Deserialize(m_cached.model_buffer)) {
        return false;
    }
    
    // Load parameters
    params = m_cached.transform_params;
    pred_thresh_scaled = m_cached.prediction_threshold_scaled;
    pred_thresh_orig = m_cached.prediction_threshold_original;
    dyn_pos_thresh = m_cached.dynamic_positive_threshold;
    
    return true;
}

} // namespace simulation
#include "ISimulationModel_v2.h"
#include <map>
#include <memory>
#include <iostream>

namespace simulation {

// ModelCache implementation
void ModelCache::CacheModel(
    const ISimulationModel& model,
    const TransformParams& params,
    float pred_thresh_scaled,
    float pred_thresh_orig,
    float dyn_pos_thresh,
    int fold_number) {
    
    try {
        m_cached.model_type = model.GetModelType();
        m_cached.model_buffer = model.Serialize();
        m_cached.transform_params = params;
        m_cached.prediction_threshold_scaled = pred_thresh_scaled;
        m_cached.prediction_threshold_original = pred_thresh_orig;
        m_cached.dynamic_positive_threshold = dyn_pos_thresh;
        m_cached.source_fold = fold_number;
        m_cached.is_valid = true;
        
        std::cout << "Cached " << model.GetModelType() << " model from fold " 
                  << fold_number << " for reuse" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to cache model: " << e.what() << std::endl;
        m_cached.is_valid = false;
    }
}

bool ModelCache::LoadCachedModel(
    ISimulationModel& model,
    TransformParams& params,
    float& pred_thresh_scaled,
    float& pred_thresh_orig,
    float& dyn_pos_thresh) const {
    
    if (!m_cached.is_valid || m_cached.model_buffer.empty()) {
        return false;
    }
    
    // Check model type compatibility
    if (model.GetModelType() != m_cached.model_type) {
        std::cerr << "Model type mismatch: cached " << m_cached.model_type 
                  << " vs current " << model.GetModelType() << std::endl;
        return false;
    }
    
    try {
        if (model.Deserialize(m_cached.model_buffer)) {
            params = m_cached.transform_params;
            pred_thresh_scaled = m_cached.prediction_threshold_scaled;
            pred_thresh_orig = m_cached.prediction_threshold_original;
            dyn_pos_thresh = m_cached.dynamic_positive_threshold;
            
            std::cout << "Loaded cached " << m_cached.model_type 
                      << " model from fold " << m_cached.source_fold << std::endl;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load cached model: " << e.what() << std::endl;
    }
    
    return false;
}

// ModelFactory implementation
std::map<std::string, ModelFactory::ModelRegistration>& ModelFactory::GetRegistry() {
    static std::map<std::string, ModelRegistration> registry;
    return registry;
}

void ModelFactory::RegisterModel(
    const std::string& model_type,
    const ModelRegistration& registration) {
    GetRegistry()[model_type] = registration;
}

std::unique_ptr<ISimulationModel> ModelFactory::CreateModel(const std::string& model_type) {
    auto& registry = GetRegistry();
    auto it = registry.find(model_type);
    if (it != registry.end() && it->second.create_model) {
        return it->second.create_model();
    }
    return nullptr;
}

std::unique_ptr<IModelConfigWidget> ModelFactory::CreateWidget(const std::string& model_type) {
    auto& registry = GetRegistry();
    auto it = registry.find(model_type);
    if (it != registry.end() && it->second.create_widget) {
        return it->second.create_widget();
    }
    return nullptr;
}

std::map<std::string, std::vector<std::string>> ModelFactory::GetModelsByCategory() {
    std::map<std::string, std::vector<std::string>> result;
    for (const auto& [model_type, registration] : GetRegistry()) {
        result[registration.category].push_back(model_type);
    }
    return result;
}

std::vector<std::string> ModelFactory::GetAllModels() {
    std::vector<std::string> result;
    for (const auto& [model_type, registration] : GetRegistry()) {
        result.push_back(model_type);
    }
    return result;
}

bool ModelFactory::IsModelAvailable(const std::string& model_type) {
    auto& registry = GetRegistry();
    auto it = registry.find(model_type);
    if (it != registry.end() && it->second.create_model) {
        auto model = it->second.create_model();
        return model && model->IsAvailable();
    }
    return false;
}

} // namespace simulation
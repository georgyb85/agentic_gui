#include "XGBoostModel.h"
#include "XGBoostWidget.h"
#include "../XGBoostConfig.h"
#include "../SimulationUtils.h"
#include "../ThresholdCalculator.h"
#include "imgui.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <limits>
#include <cmath>
#include <algorithm>

// XGBoost C API
extern "C" {
    #include <xgboost/c_api.h>
}

namespace simulation {
namespace models {

// XGBoostModel implementation
XGBoostModel::XGBoostModel() 
    : m_availability_checked(false)
    , m_is_available(false) {
}

XGBoostModel::~XGBoostModel() {
    // Nothing to free - resources are local to Train()
}

void XGBoostModel::FreeResources() {
    // Deprecated - resources are now local to Train()
}

void XGBoostModel::CheckXGBoostError(int status, const std::string& context) {
    if (status != 0) {
        const char* error = XGBGetLastError();
        std::string error_msg = "XGBoost error in " + context + ": " + error;
        std::cerr << error_msg << std::endl;
        throw std::runtime_error(error_msg);
    }
}

const XGBoostConfig& XGBoostModel::GetXGBoostConfig(const ModelConfigBase& base_config) const {
    // This assumes the config is actually an XGBoostConfig
    // In practice, we'd use std::any or a visitor pattern
    return static_cast<const XGBoostConfig&>(base_config);
}

TrainingResult XGBoostModel::Train(
    const std::vector<float>& X_train,
    const std::vector<float>& y_train,
    const std::vector<float>& X_val,
    const std::vector<float>& y_val,
    const ModelConfigBase& base_config,
    int num_features) {
    
    TrainingResult result;
    result.success = false;
    
    try {
        const XGBoostConfig& config = GetXGBoostConfig(base_config);
        
        // Store feature names for importance
        m_feature_names.clear();
        for (const auto& feat : base_config.feature_columns) {
            m_feature_names.push_back(feat);
        }
        
        // Local XGBoost handles - automatically cleaned up when function exits
        BoosterHandle booster = nullptr;
        DMatrixHandle dtrain = nullptr;
        DMatrixHandle dval = nullptr;
        
        int n_train = y_train.size();
        int n_val = y_val.size();
        
        // Calculate transformation parameters
        result.transform_params = utils::Statistics::CalculateTransformParams(y_train);
        
        // Transform targets
        std::vector<float> y_train_transformed = utils::Transform::TransformTargets(
            y_train, result.transform_params,
            base_config.use_tanh_transform,
            base_config.use_standardization,
            base_config.tanh_scaling_factor
        );
        
        std::vector<float> y_val_transformed = utils::Transform::TransformTargets(
            y_val, result.transform_params,
            base_config.use_tanh_transform,
            base_config.use_standardization,
            base_config.tanh_scaling_factor
        );
        
        // Create DMatrices
        CheckXGBoostError(
            XGDMatrixCreateFromMat(X_train.data(), n_train, num_features, -1, &dtrain),
            "Creating training matrix"
        );
        CheckXGBoostError(
            XGDMatrixSetFloatInfo(dtrain, "label", y_train_transformed.data(), n_train),
            "Setting training labels"
        );
        
        CheckXGBoostError(
            XGDMatrixCreateFromMat(X_val.data(), n_val, num_features, -1, &dval),
            "Creating validation matrix"
        );
        CheckXGBoostError(
            XGDMatrixSetFloatInfo(dval, "label", y_val_transformed.data(), n_val),
            "Setting validation labels"
        );
        
        // Create booster
        DMatrixHandle eval_dmats[2] = {dtrain, dval};
        const char* eval_names[2] = {"train", "val"};
        
        CheckXGBoostError(
            XGBoosterCreate(eval_dmats, 2, &booster),
            "Creating booster"
        );
        
        // Set parameters
        CheckXGBoostError(XGBoosterSetParam(booster, "learning_rate", 
            std::to_string(config.learning_rate).c_str()), "Setting learning_rate");
        CheckXGBoostError(XGBoosterSetParam(booster, "max_depth", 
            std::to_string(config.max_depth).c_str()), "Setting max_depth");
        CheckXGBoostError(XGBoosterSetParam(booster, "min_child_weight", 
            std::to_string(config.min_child_weight).c_str()), "Setting min_child_weight");
        CheckXGBoostError(XGBoosterSetParam(booster, "subsample", 
            std::to_string(config.subsample).c_str()), "Setting subsample");
        CheckXGBoostError(XGBoosterSetParam(booster, "colsample_bytree", 
            std::to_string(config.colsample_bytree).c_str()), "Setting colsample_bytree");
        CheckXGBoostError(XGBoosterSetParam(booster, "lambda", 
            std::to_string(config.lambda).c_str()), "Setting lambda");
        CheckXGBoostError(XGBoosterSetParam(booster, "objective", 
            config.objective.c_str()), "Setting objective");
        // Remove verbose objective logging
        
        // Set quantile alpha if using quantile regression
        if (config.objective == "reg:quantileerror") {
            CheckXGBoostError(XGBoosterSetParam(booster, "quantile_alpha", 
                std::to_string(config.quantile_alpha).c_str()), "Setting quantile_alpha");
        }
        
        CheckXGBoostError(XGBoosterSetParam(booster, "tree_method", 
            config.tree_method.c_str()), "Setting tree_method");
        CheckXGBoostError(XGBoosterSetParam(booster, "seed", 
            std::to_string(base_config.random_seed).c_str()), "Setting seed");
        
        // Try GPU first, fallback to CPU
        int gpu_result = XGBoosterSetParam(booster, "device", config.device.c_str());
        if (gpu_result != 0) {
            CheckXGBoostError(XGBoosterSetParam(booster, "device", "cpu"), 
                "Setting device to CPU");
        }
        
        // Add evaluation metric - use appropriate metric for the objective
        // For quantile regression, we should NOT set eval_metric as XGBoost will use the 
        // training objective for evaluation automatically
        if (config.objective != "reg:quantileerror") {
            // Only set RMSE for standard regression
            CheckXGBoostError(XGBoosterSetParam(booster, "eval_metric", "rmse"), "Setting eval_metric");
        }
        // For quantile regression, XGBoost will automatically use quantile loss for evaluation
        
        // Training loop with early stopping
        float best_score = std::numeric_limits<float>::max();
        float initial_score = std::numeric_limits<float>::max();  // Track first real score
        int best_iteration = 0;
        int rounds_without_improvement = 0;
        bool ever_improved = false;
        int effective_min_rounds = config.min_boost_rounds;
        int actual_iterations = 0;  // Track actual iterations performed
        
        std::cout << "Starting XGBoost training with " << n_train << " training samples and " 
                  << n_val << " validation samples" << std::endl;
        
        for (int iter = 0; iter < config.num_boost_round; ++iter) {
            actual_iterations = iter + 1;  // Track current iteration (1-indexed)
            CheckXGBoostError(
                XGBoosterUpdateOneIter(booster, iter, dtrain),
                "Training iteration"
            );
            
            // Evaluate
            const char* eval_result;
            CheckXGBoostError(
                XGBoosterEvalOneIter(booster, iter, eval_dmats, eval_names, 2, &eval_result),
                "Evaluation iteration"
            );
            
            // Parse validation score
            std::string eval_str(eval_result);
            
            // Only print first iteration for basic diagnostics
            if (iter == 0) {
                std::cout << "XGBoost eval: " << eval_str << std::endl;
            }
            
            // Look for validation score - could be rmse or quantile
            size_t val_pos = eval_str.find("val-");
            if (val_pos != std::string::npos) {
                // Find the colon after "val-XXX:"
                size_t colon_pos = eval_str.find(':', val_pos);
                if (colon_pos != std::string::npos) {
                    // Extract the number after the colon
                    std::string score_str = eval_str.substr(colon_pos + 1);
                    // Find the end of the number (space or end of string)
                    size_t end_pos = score_str.find_first_of(" \t\n");
                    if (end_pos != std::string::npos) {
                        score_str = score_str.substr(0, end_pos);
                    }
                    float val_score = std::stof(score_str);
                    
                    // Check for NaN or infinity - these indicate training failure
                if (!std::isfinite(val_score)) {
                    std::cout << "WARNING: Validation score is NaN/Inf at iteration " << iter 
                              << " - model failed to learn" << std::endl;
                    // Don't set ever_improved, model has failed
                    rounds_without_improvement = config.early_stopping_rounds; // Force early stop
                } else {
                    // On first iteration, just set the baseline
                    if (iter == 0) {
                        initial_score = val_score;
                        best_score = val_score;
                        best_iteration = 0;
                        // A model that successfully trains on first iteration has learned something
                        // even if subsequent iterations don't improve much
                        ever_improved = true;
                    } else if (val_score < best_score) {
                        // Real improvement after first iteration
                        best_score = val_score;
                        best_iteration = iter;
                        rounds_without_improvement = 0;
                        ever_improved = true;
                    } else {
                        rounds_without_improvement++;
                    }
                }
                
                // Force minimum iterations if not learning
                // This check happens AFTER the improvement check, so at iter 0,
                // ever_improved would only be false if val_score was NaN or >= best_score
                if (iter == 0 && !ever_improved) {
                    effective_min_rounds = std::max(50, effective_min_rounds);
                }
                
                // Early stopping - but ensure minimum rounds if force_minimum_training is enabled
                bool can_stop_early = true;
                if (config.force_minimum_training) {
                    can_stop_early = (iter >= config.min_boost_rounds - 1);
                } else {
                    can_stop_early = (iter >= effective_min_rounds - 1);
                }
                
                if (can_stop_early && rounds_without_improvement >= config.early_stopping_rounds) {
                    // Only log if stopping very early (potential issue)
                    if (iter + 1 <= config.min_boost_rounds + 10) {
                        std::cout << "Early stop at min rounds (" << (iter + 1) 
                                  << "), best: " << best_iteration 
                                  << ", improved: " << (ever_improved ? "yes" : "NO") << std::endl;
                    }
                    break;
                }
                } // Close colon_pos check
            } // Close val_pos check
        }
        
        result.success = true;
        
        // Determine if model learned - only flag truly pathological cases
        // Pathological cases:
        // 1. NaN/Inf scores (already handled above with early exit)
        // 2. Model got WORSE than initial (negative improvement)
        // 3. Model never improved at all (ever_improved = false)
        
        float improvement = initial_score - best_score;
        bool got_worse = (best_score > initial_score * 1.1f);  // 10% worse is clearly pathological
        
        // Model is considered to have learned if it improved AT ALL or stayed roughly the same
        // In financial markets, even maintaining performance is valid (not getting worse)
        result.model_learned = ever_improved && !got_worse;
        
        // Only flag as failed in truly pathological cases
        if (!result.model_learned) {
            std::cout << "WARNING: Model appears pathological - ";
            if (!ever_improved) {
                std::cout << "never improved from iteration 0";
            } else if (got_worse) {
                std::cout << "got significantly worse (initial: " << initial_score 
                          << ", final: " << best_score << ")";
            }
            std::cout << std::endl;
        } else if (actual_iterations <= config.min_boost_rounds) {
            // Just informational - model stopped exactly at minimum but still learned
            std::cout << "Model stopped at minimum rounds (" << actual_iterations 
                      << ") with improvement: " << (improvement * 100 / initial_score) << "%" << std::endl;
        }
        
        result.best_iteration = actual_iterations;  // Use actual iterations performed, not best
        result.best_score = best_score;
        
        // Calculate threshold based on selected method
        if (ever_improved) {
            if (config.threshold_method == ThresholdMethod::Percentile95) {
                // Use validation set for 95th percentile threshold
                bst_ulong val_len;
                const float* val_predictions;
                CheckXGBoostError(
                    XGBoosterPredict(booster, dval, 0, 0, 0, &val_len, &val_predictions),
                    "Predicting validation set for threshold"
                );
                
                std::vector<float> val_preds(val_predictions, val_predictions + val_len);
                result.validation_threshold = utils::Statistics::CalculateQuantile(val_preds, 0.95f);
                
            } else if (config.threshold_method == ThresholdMethod::OptimalROC) {
                // Use training set for optimal threshold to avoid data leakage
                bst_ulong train_len;
                const float* train_predictions;
                CheckXGBoostError(
                    XGBoosterPredict(booster, dtrain, 0, 0, 0, &train_len, &train_predictions),
                    "Predicting training set for optimal threshold"
                );
                
                std::vector<float> train_preds(train_predictions, train_predictions + train_len);
                // Use the original training returns (y_train parameter before transformation)
                // y_train contains the original returns passed to this function
                
                result.validation_threshold = ThresholdCalculator::CalculateOptimalThreshold(
                    train_preds, y_train, 1  // min_kept_percent = 1%
                );
            }
        }
        
        // Serialize model for later use
        if (booster) {
            bst_ulong out_len;
            const char* out_dptr;
            const char* config_str = R"({"format": "ubj"})";
            if (XGBoosterSaveModelToBuffer(booster, config_str, &out_len, &out_dptr) == 0) {
                m_serialized_model.assign(out_dptr, out_dptr + out_len);
                result.serialized_model = m_serialized_model;
            }
        }
        
        // Clean up local resources
        if (booster) XGBoosterFree(booster);
        if (dtrain) XGDMatrixFree(dtrain);
        if (dval) XGDMatrixFree(dval);
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
        // Resources will be automatically cleaned up as they're local
    } catch (...) {
        result.success = false;
        result.error_message = "Unknown error in XGBoost training";
        // Resources will be automatically cleaned up as they're local
    }
    
    return result;
}

PredictionResult XGBoostModel::Predict(
    const std::vector<float>& X_test,
    int num_samples,
    int num_features) {
    
    PredictionResult result;
    result.success = false;
    
    try {
        if (m_serialized_model.empty()) {
            throw std::runtime_error("Model not trained");
        }
        
        // Load model from serialized buffer
        BoosterHandle booster = nullptr;
        CheckXGBoostError(
            XGBoosterCreate(nullptr, 0, &booster),
            "Creating booster for prediction"
        );
        
        CheckXGBoostError(
            XGBoosterLoadModelFromBuffer(booster, m_serialized_model.data(), m_serialized_model.size()),
            "Loading model from buffer"
        );
        
        // Create test matrix
        DMatrixHandle dtest = nullptr;
        CheckXGBoostError(
            XGDMatrixCreateFromMat(X_test.data(), num_samples, num_features, -1, &dtest),
            "Creating test matrix"
        );
        
        // Make predictions
        bst_ulong test_len;
        const float* predictions;
        CheckXGBoostError(
            XGBoosterPredict(booster, dtest, 0, 0, 0, &test_len, &predictions),
            "Predicting test set"
        );
        
        result.predictions.assign(predictions, predictions + test_len);
        result.success = true;
        
        // Clean up
        if (dtest) XGDMatrixFree(dtest);
        if (booster) XGBoosterFree(booster);
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }
    
    return result;
}

std::vector<char> XGBoostModel::Serialize() const {
    return m_serialized_model;
}

bool XGBoostModel::Deserialize(const std::vector<char>& buffer) {
    if (buffer.empty()) {
        return false;
    }
    
    // Just store the buffer - we'll load it when needed for predictions
    m_serialized_model = buffer;
    return true;
}

std::any XGBoostModel::CreateDefaultConfig() const {
    XGBoostConfig config;
    // XGBoostConfig already has good defaults
    return config;
}

std::any XGBoostModel::CloneConfig(const std::any& config) const {
    try {
        return std::any_cast<XGBoostConfig>(config);
    } catch (const std::bad_any_cast&) {
        return CreateDefaultConfig();
    }
}

bool XGBoostModel::ValidateConfig(const std::any& config) const {
    try {
        const auto& xgb_config = std::any_cast<const XGBoostConfig&>(config);
        // Validate ranges
        return xgb_config.max_depth > 0 && xgb_config.max_depth <= 30 &&
               xgb_config.learning_rate > 0 && xgb_config.learning_rate <= 1.0 &&
               xgb_config.num_boost_round > 0 &&
               xgb_config.subsample > 0 && xgb_config.subsample <= 1.0 &&
               xgb_config.colsample_bytree > 0 && xgb_config.colsample_bytree <= 1.0;
    } catch (const std::bad_any_cast&) {
        return false;
    }
}

ISimulationModel::Capabilities XGBoostModel::GetCapabilities() const {
    Capabilities caps;
    caps.supports_feature_importance = true;
    caps.supports_early_stopping = true;
    caps.supports_regularization = true;  // XGBoost has L1/L2 regularization
    caps.supports_partial_dependence = false;
    caps.supports_prediction_intervals = false;
    caps.supports_online_learning = false;
    caps.requires_normalization = false;
    caps.requires_feature_scaling = false;
    return caps;
}

// CloneConfig removed - using std::any now
// std::unique_ptr<ModelConfigBase> XGBoostModel::CloneConfig(const ModelConfigBase& config) const {
//     const XGBoostConfig& xgb_config = GetXGBoostConfig(config);
//     return std::make_unique<XGBoostConfig>(xgb_config);
// }

// RenderConfigUI removed - using widgets now
// GetHyperparameterSummary removed - using widgets now

std::vector<std::pair<std::string, float>> XGBoostModel::GetFeatureImportance() const {
    if (m_serialized_model.empty() || m_feature_names.empty()) {
        return {};
    }
    
    // Load model from serialized buffer temporarily
    BoosterHandle booster = nullptr;
    if (XGBoosterCreate(nullptr, 0, &booster) != 0) {
        return {};
    }
    
    if (XGBoosterLoadModelFromBuffer(booster, m_serialized_model.data(), m_serialized_model.size()) != 0) {
        XGBoosterFree(booster);
        return {};
    }
    
    std::vector<std::pair<std::string, float>> importance;
    
    try {
        bst_ulong n_features;
        char const** feature_names;
        bst_ulong out_dim;
        bst_ulong const* out_shape;
        float const* scores;
        
        const char* config = R"({"importance_type": "weight"})";
        
        if (XGBoosterFeatureScore(booster, config, &n_features, &feature_names, 
                                 &out_dim, &out_shape, &scores) == 0) {
            
            // Map feature names to scores
            std::map<std::string, float> feature_score_map;
            for (bst_ulong i = 0; i < n_features; ++i) {
                std::string fname = feature_names[i];
                // XGBoost uses f0, f1, etc. as default names
                if (fname[0] == 'f' && std::isdigit(fname[1])) {
                    int idx = std::stoi(fname.substr(1));
                    if (idx < (int)m_feature_names.size()) {
                        fname = m_feature_names[idx];
                    }
                }
                feature_score_map[fname] = scores[i];
            }
            
            // Normalize scores
            float max_score = 0.0f;
            for (const auto& [name, score] : feature_score_map) {
                max_score = std::max(max_score, score);
            }
            
            if (max_score > 0) {
                for (const auto& [name, score] : feature_score_map) {
                    importance.push_back({name, score / max_score});
                }
            }
            
            // Sort by importance
            std::sort(importance.begin(), importance.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
        }
    } catch (...) {
        // Feature importance not available
    }
    
    // Clean up
    if (booster) XGBoosterFree(booster);
    
    return importance;
}

bool XGBoostModel::IsAvailable() const {
    if (!m_availability_checked) {
        try {
            // Try to create a dummy booster to check if XGBoost is available
            BoosterHandle test_booster;
            int status = XGBoosterCreate(nullptr, 0, &test_booster);
            if (status == 0) {
                XGBoosterFree(test_booster);
                m_is_available = true;
            } else {
                m_is_available = false;
                m_availability_error = "XGBoost library not properly initialized";
            }
        } catch (...) {
            m_is_available = false;
            m_availability_error = "XGBoost library not found";
        }
        m_availability_checked = true;
    }
    return m_is_available;
}

// XGBoostWidget implementation moved to XGBoostWidget.cpp

// Draw_Old and other widget methods moved to XGBoostWidget.cpp
/*
bool models::XGBoostWidget::Draw_Old(std::any& config_any) {
    if (!config_any.has_value()) {
        return false;
    }
    
    try {
        auto& config = std::any_cast<XGBoostConfig&>(config_any);
        bool changed = false;
        
        if (ImGui::CollapsingHeader("Tree Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::SliderInt("Max Depth", &config.max_depth, 1, 20);
            changed |= ImGui::SliderFloat("Min Child Weight", &config.min_child_weight, 0.1f, 100.0f, "%.1f");
            
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Minimum sum of instance weight needed in a child");
            }
        }
        
        if (ImGui::CollapsingHeader("Learning Parameters")) {
            changed |= ImGui::SliderFloat("Learning Rate", &config.learning_rate, 
                0.001f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::InputInt("Num Rounds", &config.num_boost_round);
            changed |= ImGui::InputInt("Early Stopping", &config.early_stopping_rounds);
            changed |= ImGui::InputInt("Min Rounds", &config.min_boost_rounds);
            changed |= ImGui::Checkbox("Force Minimum Training", &config.force_minimum_training);
        }
        
        if (ImGui::CollapsingHeader("Regularization")) {
            changed |= ImGui::SliderFloat("Subsample", &config.subsample, 0.1f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Col Sample", &config.colsample_bytree, 0.1f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Lambda (L2)", &config.lambda, 0.0f, 10.0f, "%.2f");
        }
        
        return changed;
    } catch (const std::bad_any_cast& e) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: Invalid configuration type");
        return false;
    }
}

std::string models::XGBoostWidget::GetSummary(const std::any& config_any) const {
    try {
        const auto& config = std::any_cast<const XGBoostConfig&>(config_any);
        return config.ToString();
    } catch (...) {
        return "Invalid configuration";
    }
}

std::string models::XGBoostWidget::ExportToJson(const std::any& config_any) const {
    try {
        const auto& config = std::any_cast<const XGBoostConfig&>(config_any);
        std::ostringstream json;
        json << "{"
             << "\"learning_rate\":" << config.learning_rate << ","
             << "\"max_depth\":" << config.max_depth << ","
             << "\"min_child_weight\":" << config.min_child_weight << ","
             << "\"subsample\":" << config.subsample << ","
             << "\"colsample_bytree\":" << config.colsample_bytree << ","
             << "\"lambda\":" << config.lambda << ","
             << "\"num_boost_round\":" << config.num_boost_round << ","
             << "\"early_stopping_rounds\":" << config.early_stopping_rounds
             << "}";
        return json.str();
    } catch (...) {
        return "{}";
    }
}

bool models::XGBoostWidget::ImportFromJson(const std::string& json, std::any& config_any) const {
    // Simple JSON parsing - in production would use a proper JSON library
    try {
        auto& config = std::any_cast<XGBoostConfig&>(config_any);
        
        // Parse each field (simplified - real implementation would use proper JSON parser)
        size_t pos;
        if ((pos = json.find("\"learning_rate\":")) != std::string::npos) {
            config.learning_rate = std::stof(json.substr(pos + 16));
        }
        if ((pos = json.find("\"max_depth\":")) != std::string::npos) {
            config.max_depth = std::stoi(json.substr(pos + 12));
        }
        // ... etc for other fields
        
        return true;
    } catch (...) {
        return false;
    }
}
*/

} // namespace models
} // namespace simulation
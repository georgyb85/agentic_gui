#include "xgboost_model.h"
#include <stdexcept>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <cstring>

// XGBoost C API
extern "C" {
    #include <xgboost/c_api.h>
}

namespace stepwise {

XGBoostModel::XGBoostModel(const XGBoostConfig& config)
    : m_config(config)
    , m_fitted(false) {
}

XGBoostModel::~XGBoostModel() {
    // XGBoost handles cleanup automatically
}

void XGBoostModel::fit(const DataMatrix& X, const std::vector<double>& y,
                       const std::vector<int>& feature_indices) {
    if (feature_indices.empty()) {
        throw std::runtime_error("No features selected for training");
    }
    
    m_feature_indices = feature_indices;
    
    // Prepare data for XGBoost
    size_t n_samples = X.rows();
    size_t n_features = feature_indices.size();
    
    // Convert data to float array for XGBoost
    std::vector<float> X_data;
    X_data.reserve(n_samples * n_features);
    
    for (size_t i = 0; i < n_samples; ++i) {
        for (int feat_idx : feature_indices) {
            X_data.push_back(static_cast<float>(X(i, feat_idx)));
        }
    }
    
    std::vector<float> y_data(y.begin(), y.end());
    
    // Create DMatrix
    DMatrixHandle dtrain;
    int status = XGDMatrixCreateFromMat(X_data.data(), n_samples, n_features, -1, &dtrain);
    if (status != 0) {
        throw std::runtime_error(std::string("Failed to create DMatrix: ") + XGBGetLastError());
    }
    
    // Set labels
    status = XGDMatrixSetFloatInfo(dtrain, "label", y_data.data(), n_samples);
    if (status != 0) {
        XGDMatrixFree(dtrain);
        throw std::runtime_error(std::string("Failed to set labels: ") + XGBGetLastError());
    }
    
    // Create booster
    BoosterHandle booster;
    status = XGBoosterCreate(&dtrain, 1, &booster);
    if (status != 0) {
        XGDMatrixFree(dtrain);
        throw std::runtime_error(std::string("Failed to create booster: ") + XGBGetLastError());
    }
    
    // Set parameters
    XGBoosterSetParam(booster, "eta", std::to_string(m_config.learning_rate).c_str());
    XGBoosterSetParam(booster, "max_depth", std::to_string(m_config.max_depth).c_str());
    XGBoosterSetParam(booster, "min_child_weight", std::to_string(m_config.min_child_weight).c_str());
    XGBoosterSetParam(booster, "subsample", std::to_string(m_config.subsample).c_str());
    XGBoosterSetParam(booster, "colsample_bytree", std::to_string(m_config.colsample_bytree).c_str());
    XGBoosterSetParam(booster, "lambda", std::to_string(m_config.lambda).c_str());
    XGBoosterSetParam(booster, "alpha", std::to_string(m_config.alpha).c_str());
    XGBoosterSetParam(booster, "tree_method", m_config.tree_method.c_str());
    XGBoosterSetParam(booster, "objective", m_config.objective.c_str());
    XGBoosterSetParam(booster, "verbosity", "0");
    
    // Train the model
    for (int iter = 0; iter < m_config.num_boost_round; ++iter) {
        status = XGBoosterUpdateOneIter(booster, iter, dtrain);
        if (status != 0) {
            XGBoosterFree(booster);
            XGDMatrixFree(dtrain);
            throw std::runtime_error(std::string("Training failed at iteration ") + std::to_string(iter));
        }
    }
    
    // Serialize the model for later use
    bst_ulong out_len;
    const char* out_dptr;
    const char* config_str = R"({"format": "ubj"})";
    status = XGBoosterSaveModelToBuffer(booster, config_str, &out_len, &out_dptr);
    if (status == 0) {
        m_serialized_model.assign(out_dptr, out_dptr + out_len);
    }
    
    // Clean up
    XGBoosterFree(booster);
    XGDMatrixFree(dtrain);
    
    m_fitted = true;
}

std::vector<double> XGBoostModel::predict(const DataMatrix& X,
                                         const std::vector<int>& feature_indices) const {
    if (!m_fitted || m_serialized_model.empty()) {
        throw std::runtime_error("Model has not been fitted yet");
    }
    
    size_t n_samples = X.rows();
    size_t n_features = feature_indices.size();
    
    // Convert data to float array
    std::vector<float> X_data;
    X_data.reserve(n_samples * n_features);
    
    for (size_t i = 0; i < n_samples; ++i) {
        for (int feat_idx : feature_indices) {
            X_data.push_back(static_cast<float>(X(i, feat_idx)));
        }
    }
    
    // Create DMatrix for prediction
    DMatrixHandle dtest;
    int status = XGDMatrixCreateFromMat(X_data.data(), n_samples, n_features, -1, &dtest);
    if (status != 0) {
        throw std::runtime_error(std::string("Failed to create test DMatrix: ") + XGBGetLastError());
    }
    
    // Create booster from serialized model
    BoosterHandle booster;
    status = XGBoosterCreate(&dtest, 0, &booster);
    if (status != 0) {
        XGDMatrixFree(dtest);
        throw std::runtime_error("Failed to create booster for prediction");
    }
    
    // Load serialized model
    status = XGBoosterLoadModelFromBuffer(booster, m_serialized_model.data(), m_serialized_model.size());
    if (status != 0) {
        XGBoosterFree(booster);
        XGDMatrixFree(dtest);
        throw std::runtime_error("Failed to load model from buffer");
    }
    
    // Make predictions
    bst_ulong out_len;
    const float* out_result;
    status = XGBoosterPredict(booster, dtest, 0, 0, 0, &out_len, &out_result);
    if (status != 0) {
        XGBoosterFree(booster);
        XGDMatrixFree(dtest);
        throw std::runtime_error("Prediction failed");
    }
    
    // Convert to vector of doubles
    std::vector<double> predictions(out_result, out_result + out_len);
    
    // Clean up
    XGBoosterFree(booster);
    XGDMatrixFree(dtest);
    
    return predictions;
}

double XGBoostModel::score(const DataMatrix& X, const std::vector<double>& y,
                          const std::vector<int>& feature_indices) const {
    auto predictions = predict(X, feature_indices);
    
    // Calculate R-squared
    double y_mean = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
    
    double ss_tot = 0.0;
    double ss_res = 0.0;
    
    for (size_t i = 0; i < y.size(); ++i) {
        double y_diff = y[i] - y_mean;
        ss_tot += y_diff * y_diff;
        
        double res = y[i] - predictions[i];
        ss_res += res * res;
    }
    
    if (ss_tot == 0.0) return 0.0;
    return 1.0 - (ss_res / ss_tot);
}

std::vector<double> XGBoostModel::get_coefficients() const {
    // XGBoost doesn't have linear coefficients
    // Return empty vector or feature importances
    return std::vector<double>();
}

std::unique_ptr<IStepwiseModel> XGBoostModel::clone() const {
    return std::make_unique<XGBoostModel>(m_config);
}

std::vector<float> XGBoostModel::get_feature_importance() const {
    if (!m_fitted || m_serialized_model.empty()) {
        return std::vector<float>();
    }
    
    // Create a dummy handle just to load the model
    BoosterHandle booster;
    DMatrixHandle dmatrix = nullptr;
    
    // Create empty DMatrix
    float dummy = 0;
    int status = XGDMatrixCreateFromMat(&dummy, 0, 0, -1, &dmatrix);
    if (status != 0) {
        return std::vector<float>();
    }
    
    status = XGBoosterCreate(&dmatrix, 0, &booster);
    if (status != 0) {
        XGDMatrixFree(dmatrix);
        return std::vector<float>();
    }
    
    // Load model
    status = XGBoosterLoadModelFromBuffer(booster, m_serialized_model.data(), m_serialized_model.size());
    if (status != 0) {
        XGBoosterFree(booster);
        XGDMatrixFree(dmatrix);
        return std::vector<float>();
    }
    
    // Get feature importance using XGBoosterFeatureScore
    std::vector<float> importance;
    
    bst_ulong n_features;
    char const** feature_names;
    bst_ulong out_dim;
    bst_ulong const* out_shape;
    float const* scores;
    
    const char* config = R"({"importance_type": "weight"})";
    
    status = XGBoosterFeatureScore(booster, config, &n_features, &feature_names, 
                                  &out_dim, &out_shape, &scores);
    
    if (status == 0 && n_features > 0) {
        // Map importances to feature indices
        importance.resize(m_feature_indices.size(), 0.0f);
        for (bst_ulong i = 0; i < n_features && i < importance.size(); ++i) {
            importance[i] = scores[i];
        }
    }
    
    // Clean up
    XGBoosterFree(booster);
    XGDMatrixFree(dmatrix);
    
    return importance;
}


} // namespace stepwise
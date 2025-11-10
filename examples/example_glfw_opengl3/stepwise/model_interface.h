#pragma once

#include <vector>
#include <memory>
#include "data_matrix.h"

namespace stepwise {

// Feature set information
struct FeatureSet {
    std::vector<int> feature_indices;
    double cv_score;        // Cross-validation score
    double train_score;     // Training score
    int n_features;         // Number of features
    
    bool operator<(const FeatureSet& other) const {
        return cv_score < other.cv_score;
    }
};

// Abstract interface for models used in stepwise selection
class IStepwiseModel {
public:
    virtual ~IStepwiseModel() = default;
    
    // Fit the model with given features
    virtual void fit(const DataMatrix& X, const std::vector<double>& y,
                    const std::vector<int>& feature_indices) = 0;
    
    // Predict using fitted model
    virtual std::vector<double> predict(const DataMatrix& X,
                                       const std::vector<int>& feature_indices) const = 0;
    
    // Calculate R-squared score
    virtual double score(const DataMatrix& X, const std::vector<double>& y,
                        const std::vector<int>& feature_indices) const = 0;
    
    // Get model coefficients (if applicable)
    virtual std::vector<double> get_coefficients() const = 0;
    
    // Clone the model (for thread safety in cross-validation)
    virtual std::unique_ptr<IStepwiseModel> clone() const = 0;
    
    // Get model type name
    virtual std::string get_model_type() const = 0;
    
    // Check if model supports coefficient extraction
    virtual bool has_coefficients() const = 0;
};

// Model types available for stepwise selection
enum class ModelType {
    LINEAR_QUADRATIC,
    XGBOOST
};

} // namespace stepwise
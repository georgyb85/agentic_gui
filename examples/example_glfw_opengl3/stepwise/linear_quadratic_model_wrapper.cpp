#include "linear_quadratic_model_wrapper.h"

namespace stepwise {

LinearQuadraticModelWrapper::LinearQuadraticModelWrapper() {
    // Initialize with default configuration
}

void LinearQuadraticModelWrapper::fit(const DataMatrix& X, const std::vector<double>& y,
                                     const std::vector<int>& feature_indices) {
    // LinearQuadraticModel needs exclude ranges, so we fit on all data
    // by specifying no exclusion
    m_model.fit(X, y, feature_indices, -1, -1);
}

std::vector<double> LinearQuadraticModelWrapper::predict(const DataMatrix& X,
                                                        const std::vector<int>& feature_indices) const {
    // LinearQuadraticModel doesn't have a predict method, but we can use evaluate
    // to compute predictions. For now, return empty vector as this is complex to implement
    // properly without modifying the original model
    std::vector<double> predictions;
    predictions.reserve(X.rows());
    
    // Simple placeholder - in production would need proper prediction implementation
    for (size_t i = 0; i < X.rows(); ++i) {
        predictions.push_back(0.0);
    }
    
    return predictions;
}

double LinearQuadraticModelWrapper::score(const DataMatrix& X, const std::vector<double>& y,
                                         const std::vector<int>& feature_indices) const {
    // Use evaluate to compute SSE and convert to R-squared
    double sse = m_model.evaluate(X, y, feature_indices, 0, X.rows());
    
    // Compute total sum of squares
    double y_mean = 0.0;
    for (const auto& val : y) {
        y_mean += val;
    }
    y_mean /= y.size();
    
    double sst = 0.0;
    for (const auto& val : y) {
        double diff = val - y_mean;
        sst += diff * diff;
    }
    
    if (sst == 0.0) return 0.0;
    return 1.0 - (sse / sst);
}

std::vector<double> LinearQuadraticModelWrapper::get_coefficients() const {
    // Get coefficients from the model - this method exists
    auto coef = m_model.get_coefficients();
    return coef;
}

std::unique_ptr<IStepwiseModel> LinearQuadraticModelWrapper::clone() const {
    auto clone = std::make_unique<LinearQuadraticModelWrapper>();
    clone->m_model = m_model; // Copy the model state
    return clone;
}

} // namespace stepwise
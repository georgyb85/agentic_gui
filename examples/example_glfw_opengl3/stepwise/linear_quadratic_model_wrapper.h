#pragma once

#include "model_interface.h"
#include "linear_quadratic_model.h"
#include <memory>

namespace stepwise {

// Wrapper for LinearQuadraticModel to implement IStepwiseModel interface
class LinearQuadraticModelWrapper : public IStepwiseModel {
public:
    LinearQuadraticModelWrapper();
    ~LinearQuadraticModelWrapper() = default;
    
    // IStepwiseModel interface
    void fit(const DataMatrix& X, const std::vector<double>& y,
            const std::vector<int>& feature_indices) override;
    
    std::vector<double> predict(const DataMatrix& X,
                               const std::vector<int>& feature_indices) const override;
    
    double score(const DataMatrix& X, const std::vector<double>& y,
                const std::vector<int>& feature_indices) const override;
    
    std::vector<double> get_coefficients() const override;
    
    std::unique_ptr<IStepwiseModel> clone() const override;
    
    std::string get_model_type() const override { return "Linear-Quadratic"; }
    
    bool has_coefficients() const override { return true; }
    
    // Access to underlying model
    LinearQuadraticModel& get_model() { return m_model; }
    const LinearQuadraticModel& get_model() const { return m_model; }
    
private:
    LinearQuadraticModel m_model;
};

} // namespace stepwise
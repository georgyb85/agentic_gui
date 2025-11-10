#include "ISimulationModel_v2.h"
#include "models/XGBoostModel.h"
// #include "models/LinearRegressionModel.h"
// #include "models/NeuralNetworkModel.h"

namespace simulation {

// Called once at application startup to register all available models
void InitializeModels() {
    // Register XGBoost
    ModelFactory::RegisterModel("XGBoost", {
        .create_model = []() { 
            return std::make_unique<models::XGBoostModel>(); 
        },
        .create_widget = []() { 
            return std::make_unique<models::XGBoostWidget>(); 
        },
        .category = "Tree-Based",
        .description = "Gradient boosting with XGBoost library"
    });
    
    // Register Linear Regression (when implemented)
    /*
    ModelFactory::RegisterModel("Linear Regression", {
        .create_model = []() { 
            return std::make_unique<models::LinearRegressionModel>(); 
        },
        .create_widget = []() { 
            return std::make_unique<models::LinearRegressionWidget>(); 
        },
        .category = "Linear Models",
        .description = "Linear/polynomial regression with regularization"
    });
    */
    
    // Register Neural Network (when implemented)
    /*
    ModelFactory::RegisterModel("Neural Network", {
        .create_model = []() { 
            return std::make_unique<models::NeuralNetworkModel>(); 
        },
        .create_widget = []() { 
            return std::make_unique<models::NeuralNetworkWidget>(); 
        },
        .category = "Neural Networks",
        .description = "Feedforward neural network"
    });
    */
    
    // Future models can be registered here
    // Random Forest, SVM, ARIMA, etc.
}

} // namespace simulation
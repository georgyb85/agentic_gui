#pragma once

#include "../ISimulationModel_v2.h"
#include <vector>
#include <memory>

namespace simulation {
namespace models {

// Configuration for Neural Network
struct NeuralNetworkConfig {
    // Network architecture
    std::vector<int> hidden_layers = {64, 32};  // Number of neurons per hidden layer
    
    // Activation functions
    enum ActivationType {
        RELU,
        TANH,
        SIGMOID,
        LEAKY_RELU,
        ELU,
        SWISH
    };
    ActivationType hidden_activation = RELU;
    ActivationType output_activation = RELU;  // Linear for regression
    
    // Training parameters
    float learning_rate = 0.001f;
    int batch_size = 32;
    int epochs = 100;
    float validation_split = 0.2f;  // From training data
    
    // Optimizer
    enum OptimizerType {
        SGD,
        ADAM,
        RMSPROP,
        ADAGRAD
    };
    OptimizerType optimizer = ADAM;
    float momentum = 0.9f;         // For SGD with momentum
    float beta1 = 0.9f;            // For Adam
    float beta2 = 0.999f;          // For Adam
    float epsilon = 1e-7f;         // For numerical stability
    
    // Regularization
    float dropout_rate = 0.0f;     // 0 = no dropout
    float l2_regularization = 0.0f;
    float l1_regularization = 0.0f;
    
    // Early stopping
    bool use_early_stopping = true;
    int patience = 10;             // Epochs to wait before stopping
    float min_delta = 1e-4f;       // Minimum improvement to continue
    
    // Advanced options
    bool use_batch_normalization = false;
    float gradient_clip_value = 0.0f;  // 0 = no clipping
    int random_seed = 42;
    
    std::string ToString() const {
        std::string arch = "NN[";
        for (size_t i = 0; i < hidden_layers.size(); ++i) {
            if (i > 0) arch += "-";
            arch += std::to_string(hidden_layers[i]);
        }
        arch += "]";
        
        std::string opt_str;
        switch (optimizer) {
            case SGD: opt_str = "SGD"; break;
            case ADAM: opt_str = "Adam"; break;
            case RMSPROP: opt_str = "RMSprop"; break;
            case ADAGRAD: opt_str = "Adagrad"; break;
        }
        
        return arch + " " + opt_str + " LR=" + std::to_string(learning_rate);
    }
};

class NeuralNetworkModel : public ISimulationModel {
public:
    NeuralNetworkModel();
    ~NeuralNetworkModel() override;
    
    // ISimulationModel interface
    std::string GetModelType() const override { return "Neural Network"; }
    std::string GetDescription() const override { 
        return "Feedforward neural network with configurable architecture";
    }
    std::string GetModelFamily() const override { return "neural"; }
    
    TrainingResult Train(
        const std::vector<float>& X_train,
        const std::vector<float>& y_train,
        const std::vector<float>& X_val,
        const std::vector<float>& y_val,
        const ModelConfigBase& config,
        int num_features
    ) override;
    
    PredictionResult Predict(
        const std::vector<float>& X_test,
        int num_samples,
        int num_features
    ) override;
    
    std::vector<char> Serialize() const override;
    bool Deserialize(const std::vector<char>& buffer) override;
    
    std::any CreateDefaultConfig() const override {
        return std::make_any<NeuralNetworkConfig>();
    }
    
    std::any CloneConfig(const std::any& config) const override;
    bool ValidateConfig(const std::any& config) const override;
    
    bool IsAvailable() const override;
    std::string GetAvailabilityError() const override { return m_availability_error; }
    
    Capabilities GetCapabilities() const override {
        return {
            .supports_feature_importance = false,  // Could add via permutation importance
            .supports_partial_dependence = false,
            .supports_prediction_intervals = false,
            .supports_online_learning = true,      // Can continue training
            .supports_regularization = true,
            .supports_early_stopping = true,
            .requires_normalization = true,        // Usually required
            .requires_feature_scaling = true       // Critical for NNs
        };
    }
    
    std::map<std::string, float> GetModelMetrics() const override;
    
    // Neural network specific metrics
    struct TrainingHistory {
        std::vector<float> train_loss;
        std::vector<float> val_loss;
        int stopped_epoch;
        float best_val_loss;
    };
    
    TrainingHistory GetTrainingHistory() const { return m_history; }
    
private:
    // Network structure
    struct Layer {
        std::vector<std::vector<float>> weights;
        std::vector<float> biases;
        NeuralNetworkConfig::ActivationType activation;
    };
    std::vector<Layer> m_layers;
    
    // Training state
    TrainingHistory m_history;
    int m_input_size;
    
    // Availability
    mutable bool m_availability_checked = false;
    mutable bool m_is_available = false;
    mutable std::string m_availability_error;
    
    // Helper methods
    void InitializeWeights(const NeuralNetworkConfig& config, int input_size);
    std::vector<float> Forward(const std::vector<float>& input);
    void Backward(const std::vector<float>& input, float target, float learning_rate);
    float ActivationFunction(float x, NeuralNetworkConfig::ActivationType type);
    float ActivationDerivative(float x, NeuralNetworkConfig::ActivationType type);
};

// UI Widget for Neural Network hyperparameters
class NeuralNetworkWidget : public IHyperparameterWidget {
public:
    bool Draw(std::any& config) override;
    std::string GetSummary(const std::any& config) const override;
    std::string ExportToJson(const std::any& config) const override;
    bool ImportFromJson(const std::string& json, std::any& config) const override;
    
private:
    // UI state for layer editing
    bool m_editing_layers = false;
    std::string m_layer_string;  // For text input of layers
};

} // namespace models
} // namespace simulation
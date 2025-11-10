// Example of how to use the new simulation architecture
// This would go in your main window or main.cpp

#include "simulation/SimulationEngine.h"
#include "simulation/ISimulationModel_v2.h"
#include "simulation/XGBoostConfig.h"
#include "simulation/ui/UniversalConfigWidget.h"
#include "simulation/PerformanceMetrics.h"

// In your initialization:
void InitializeSimulation() {
    // Register all models (call once at startup)
    simulation::InitializeModels();
}

// Example window using the new architecture
class MySimulationWindow {
public:
    MySimulationWindow() {
        // Create engine
        m_engine = std::make_unique<simulation::SimulationEngine>();
        
        // Create UI widget
        m_configWidget = std::make_unique<simulation::ui::UniversalConfigWidget>();
        
        // Set available models
        auto models_by_category = simulation::ModelFactory::GetModelsByCategory();
        m_configWidget->SetAvailableModels(models_by_category);
        
        // Set callbacks
        m_engine->SetProgressCallback([this](int current, int total) {
            OnProgress(current, total);
        });
        
        m_engine->SetFoldCompleteCallback([this](const simulation::FoldResult& result) {
            OnFoldComplete(result);
        });
    }
    
    void Draw() {
        if (ImGui::Begin("Universal Simulation")) {
            // Configuration section
            if (m_configWidget->Draw()) {
                // Configuration changed
            }
            
            ImGui::Separator();
            
            // Control buttons
            if (!m_engine->IsRunning()) {
                if (ImGui::Button("Start Simulation")) {
                    StartSimulation();
                }
            } else {
                if (ImGui::Button("Stop Simulation")) {
                    m_engine->StopSimulation();
                }
                
                // Progress bar
                int current = m_engine->GetCurrentFold();
                int total = m_engine->GetTotalFolds();
                float progress = total > 0 ? (float)current / total : 0;
                ImGui::ProgressBar(progress, ImVec2(-1, 0));
            }
            
            ImGui::Separator();
            
            // Results display
            DrawResults();
        }
        ImGui::End();
    }
    
private:
    void StartSimulation() {
        // Get selected model type
        std::string model_type = m_configWidget->GetSelectedModelType();
        
        // Create model instance
        auto model = simulation::ModelFactory::CreateModel(model_type);
        if (!model) {
            std::cerr << "Failed to create model: " << model_type << std::endl;
            return;
        }
        
        // Check if available
        if (!model->IsAvailable()) {
            std::cerr << "Model not available: " << model->GetAvailabilityError() << std::endl;
            return;
        }
        
        // Create configuration
        auto base_config = std::make_unique<simulation::ModelConfigBase>();
        base_config->feature_columns = m_configWidget->GetFeatures();
        base_config->target_column = m_configWidget->GetTarget();
        
        // Get model-specific config from widget
        auto model_config = m_configWidget->GetConfig();
        if (model_type == "XGBoost" && model_config.has_value()) {
            try {
                auto& xgb_config = std::any_cast<simulation::XGBoostConfig&>(model_config);
                // Copy XGBoost-specific parameters
                auto full_config = std::make_unique<simulation::XGBoostConfig>();
                *full_config = xgb_config;
                full_config->feature_columns = base_config->feature_columns;
                full_config->target_column = base_config->target_column;
                base_config = std::move(full_config);
            } catch (...) {
                std::cerr << "Invalid configuration for XGBoost" << std::endl;
                return;
            }
        }
        
        // Set up engine
        m_engine->SetModel(std::move(model));
        m_engine->SetModelConfig(std::move(base_config));
        m_engine->SetWalkForwardConfig(m_configWidget->GetWalkForwardConfig());
        m_engine->SetDataSource(m_timeSeriesWindow);
        
        // Start simulation
        m_engine->StartSimulation();
    }
    
    void OnProgress(int current, int total) {
        // Update UI on main thread if needed
        m_currentProgress = current;
        m_totalProgress = total;
    }
    
    void OnFoldComplete(const simulation::FoldResult& result) {
        // Add to results
        m_results.push_back(result);
        
        // Calculate metrics
        if (!result.model_learned_nothing) {
            // Could calculate comprehensive metrics here
            simulation::metrics::PerformanceMetrics::RegressionMetrics metrics = {};
            metrics.hit_rate = result.hit_rate;
            metrics.sharpe_ratio = result.avg_return_on_signals / 
                (result.std_return_on_signals > 0 ? result.std_return_on_signals : 1.0f);
            
            m_performanceTracker.AddFoldMetrics(result.fold_number, metrics);
        }
    }
    
    void DrawResults() {
        if (ImGui::BeginTable("Results", 6)) {
            ImGui::TableSetupColumn("Fold");
            ImGui::TableSetupColumn("Signals");
            ImGui::TableSetupColumn("Hit Rate");
            ImGui::TableSetupColumn("Avg Return");
            ImGui::TableSetupColumn("Sum");
            ImGui::TableSetupColumn("Status");
            ImGui::TableHeadersRow();
            
            for (const auto& result : m_results) {
                ImGui::TableNextRow();
                
                // Color based on status
                ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                if (result.model_learned_nothing && !result.used_cached_model) {
                    color = ImVec4(1, 0.3f, 0.3f, 1);  // Red
                } else if (result.used_cached_model) {
                    color = ImVec4(1, 0.8f, 0.3f, 1);  // Orange
                }
                
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                
                ImGui::TableNextColumn();
                ImGui::Text("%d", result.fold_number);
                
                ImGui::TableNextColumn();
                ImGui::Text("%d", result.n_signals);
                
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", result.hit_rate * 100);
                
                ImGui::TableNextColumn();
                ImGui::Text("%.6f", result.avg_return_on_signals);
                
                ImGui::TableNextColumn();
                ImGui::Text("%.6f", result.running_sum);
                
                ImGui::TableNextColumn();
                if (result.model_learned_nothing && !result.used_cached_model) {
                    ImGui::Text("Failed");
                } else if (result.used_cached_model) {
                    ImGui::Text("Cached");
                } else {
                    ImGui::Text("OK");
                }
                
                ImGui::PopStyleColor();
            }
            
            ImGui::EndTable();
        }
        
        // Show average metrics
        if (!m_results.empty()) {
            auto avg_metrics = m_performanceTracker.GetAverageMetrics();
            ImGui::Text("Average Sharpe: %.3f", avg_metrics.sharpe_ratio);
            ImGui::Text("Average Hit Rate: %.1f%%", avg_metrics.hit_rate * 100);
        }
    }
    
private:
    TimeSeriesWindow* m_timeSeriesWindow;
    std::unique_ptr<simulation::SimulationEngine> m_engine;
    std::unique_ptr<simulation::ui::UniversalConfigWidget> m_configWidget;
    
    std::vector<simulation::FoldResult> m_results;
    simulation::metrics::PerformanceTracker m_performanceTracker;
    
    int m_currentProgress = 0;
    int m_totalProgress = 0;
};

// Example: Running multiple models for comparison
void CompareModels(TimeSeriesWindow* data_source) {
    std::vector<std::string> models_to_test = {"XGBoost", "Linear Regression", "Neural Network"};
    std::map<std::string, simulation::metrics::PerformanceMetrics::RegressionMetrics> results;
    
    for (const auto& model_type : models_to_test) {
        auto model = simulation::ModelFactory::CreateModel(model_type);
        if (!model || !model->IsAvailable()) {
            continue;
        }
        
        // Run simulation for this model
        simulation::SimulationEngine engine;
        engine.SetModel(std::move(model));
        // ... configure and run ...
        
        // Collect metrics
        // results[model_type] = ...
    }
    
    // Compare results
    auto comparison = simulation::metrics::ModelComparison::Compare(results);
    std::cout << "Best model: " << comparison.best_model << std::endl;
    
    // Rank by specific metric
    auto rankings = simulation::metrics::ModelComparison::RankByMetric(results, "sharpe_ratio");
    for (const auto& [model, score] : rankings) {
        std::cout << model << ": " << score << std::endl;
    }
}
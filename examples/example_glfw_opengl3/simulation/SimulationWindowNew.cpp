// New, clean SimulationWindow implementation using the refactored architecture
// This replaces the 3000-line monolithic version

#include "SimulationWindowNew.h"
#include "SimulationEngine.h"
#include "ISimulationModel_v2.h"
#include "TestModelWindow.h"
#include "XGBoostConfig.h"
#include "models/XGBoostModel.h"
#include "models/XGBoostWidget.h"
#include "ui/UniversalConfigWidget.h"
#include "ui/SimulationResultsWidget_v2.h"
#include "ui/SimulationControlsWidget.h"
#include "PerformanceMetrics.h"
#include "../TimeSeriesWindow.h"
#include "../stage1_metadata_writer.h"
#include "../Stage1MetadataReader.h"
#include "../QuestDbExports.h"
#include "../QuestDbImports.h"
#include "imgui.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <unordered_map>
#include <cctype>
#include <limits>
#include <thread>
#include <atomic>
#include <optional>
#include <cstdlib>
#include <mutex>
#include <memory>
#include <any>
#include <arrow/table.h>
#include <arrow/array.h>

// Call this once at application startup
extern void InitializeSimulationModels();

namespace simulation {

namespace {

std::string ToSlug(const std::string& value) {
    if (value.empty()) return {};
    std::string slug;
    slug.reserve(value.size());
    bool lastUnderscore = false;
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            lastUnderscore = false;
        } else if (!lastUnderscore) {
            slug.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!slug.empty() && slug.back() == '_') slug.pop_back();
    if (!slug.empty() && slug.front() == '_') slug.erase(slug.begin());
    return slug;
}

std::string EscapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string WalkConfigToJson(const WalkForwardConfig& cfg) {
    std::ostringstream oss;
    oss << "{"
        << "\"train_size\":" << cfg.train_size << ","
        << "\"test_size\":" << cfg.test_size << ","
        << "\"train_test_gap\":" << cfg.train_test_gap << ","
        << "\"fold_step\":" << cfg.fold_step << ","
        << "\"start_fold\":" << cfg.start_fold << ","
        << "\"end_fold\":" << cfg.end_fold << ","
        << "\"initial_offset\":" << cfg.initial_offset
        << "}";
    return oss.str();
}

std::string HyperparamsToJson(const SimulationRun& run, const ui::UniversalConfigWidget* widget) {
    const XGBoostConfig* xgb = nullptr;
    static XGBoostConfig fallback;
    if (run.config) {
        xgb = dynamic_cast<const XGBoostConfig*>(run.config.get());
    }
    if (!xgb && widget) {
        auto configAny = widget->GetConfig();
        if (configAny.has_value()) {
            try {
                fallback = std::any_cast<XGBoostConfig>(configAny);
                xgb = &fallback;
            } catch (...) {
            }
        }
    }
    if (!xgb) {
        return "{}";
    }
    std::ostringstream oss;
    oss << "{"
        << "\"learning_rate\":" << xgb->learning_rate << ","
        << "\"max_depth\":" << xgb->max_depth << ","
        << "\"min_child_weight\":" << xgb->min_child_weight << ","
        << "\"subsample\":" << xgb->subsample << ","
        << "\"colsample_bytree\":" << xgb->colsample_bytree << ","
        << "\"lambda\":" << xgb->lambda << ","
        << "\"num_boost_round\":" << xgb->num_boost_round << ","
        << "\"early_stopping_rounds\":" << xgb->early_stopping_rounds << ","
        << "\"min_boost_rounds\":" << xgb->min_boost_rounds << ","
        << "\"force_minimum_training\":" << (xgb->force_minimum_training ? "true" : "false") << ","
        << "\"objective\":\"" << EscapeJson(xgb->objective) << "\","
        << "\"quantile_alpha\":" << xgb->quantile_alpha << ","
        << "\"tree_method\":\"" << EscapeJson(xgb->tree_method) << "\","
        << "\"device\":\"" << EscapeJson(xgb->device) << "\","
        << "\"random_seed\":" << xgb->random_seed << ","
        << "\"val_split_ratio\":" << xgb->val_split_ratio << ","
        << "\"use_tanh_transform\":" << (xgb->use_tanh_transform ? "true" : "false") << ","
        << "\"tanh_scaling_factor\":" << xgb->tanh_scaling_factor << ","
        << "\"use_standardization\":" << (xgb->use_standardization ? "true" : "false") << ","
        << "\"threshold_method\":\"" << EscapeJson(
               xgb->threshold_method == ThresholdMethod::Percentile95 ? "Percentile95" :
               xgb->threshold_method == ThresholdMethod::OptimalROC ? "OptimalROC" : "Custom")
        << "\""
        << "}";
    return oss.str();
}

std::string SummaryMetricsToJson(const SimulationRun& run) {
    double totalWinsLong = 0.0;
    double totalLossesLong = 0.0;
    double totalWinsShort = 0.0;
    double totalLossesShort = 0.0;
    double totalWinsDual = 0.0;
    double totalLossesDual = 0.0;
    double weightedLongHits = 0.0;
    double weightedShortHits = 0.0;
    double weightedTotalHits = 0.0;
    int totalLongSignals = 0;
    int totalShortSignals = 0;
    int totalSignals = 0;

    for (const auto& fold : run.foldResults) {
        totalWinsLong += fold.sum_wins;
        totalLossesLong += fold.sum_losses;
        totalWinsShort += fold.sum_short_wins;
        totalLossesShort += fold.sum_short_losses;
        totalWinsDual += fold.sum_wins + fold.sum_short_wins;
        totalLossesDual += fold.sum_losses + fold.sum_short_losses;

        totalLongSignals += fold.n_signals;
        totalShortSignals += fold.n_short_signals;
        totalSignals += fold.n_signals + fold.n_short_signals;

        weightedLongHits += fold.hit_rate * fold.n_signals;
        weightedShortHits += fold.short_hit_rate * fold.n_short_signals;
        weightedTotalHits += fold.hit_rate * fold.n_signals + fold.short_hit_rate * fold.n_short_signals;
    }

    auto compute_pf = [](double wins, double losses) {
        if (losses > 0.0) return wins / losses;
        return wins > 0.0 ? 999.0 : 0.0;
    };

    double pfLong = compute_pf(totalWinsLong, totalLossesLong);
    double pfShort = compute_pf(totalWinsShort, totalLossesShort);
    double pfDual = compute_pf(totalWinsDual, totalLossesDual);

    auto lastRunning = [](const std::vector<FoldResult>& folds, auto accessor) {
        if (folds.empty()) return 0.0;
        return static_cast<double>((folds.back().*accessor));
    };

    double runningLong = lastRunning(run.foldResults, &FoldResult::running_sum);
    double runningShort = lastRunning(run.foldResults, &FoldResult::running_sum_short);
    double runningDual = lastRunning(run.foldResults, &FoldResult::running_sum_dual);

    double avgLongHit = totalLongSignals > 0 ? (weightedLongHits / totalLongSignals) : 0.0;
    double avgShortHit = totalShortSignals > 0 ? (weightedShortHits / totalShortSignals) : 0.0;
    double avgTotalHit = totalSignals > 0 ? (weightedTotalHits / totalSignals) : 0.0;

    std::ostringstream oss;
    oss << "{"
        << "\"folds\":" << run.foldResults.size() << ","
        << "\"pf_long\":" << pfLong << ","
        << "\"pf_short\":" << pfShort << ","
        << "\"pf_dual\":" << pfDual << ","
        << "\"total_long_signals\":" << totalLongSignals << ","
        << "\"total_short_signals\":" << totalShortSignals << ","
        << "\"total_signals\":" << totalSignals << ","
        << "\"hit_rate_long\":" << avgLongHit << ","
        << "\"hit_rate_short\":" << avgShortHit << ","
        << "\"hit_rate_overall\":" << avgTotalHit << ","
        << "\"running_sum_long\":" << runningLong << ","
        << "\"running_sum_short\":" << runningShort << ","
        << "\"running_sum_dual\":" << runningDual
        << "}";
    return oss.str();
}

std::vector<std::string> ExtractFeatureColumns(const SimulationRun& run,
                                               const ui::UniversalConfigWidget* widget) {
    if (run.config && !run.config->feature_columns.empty()) {
        return run.config->feature_columns;
    }
    if (widget) {
        return widget->GetFeatures();
    }
    return {};
}

#if defined(_WIN32)
void SetEnvVar(const char* key, const char* value) {
    _putenv_s(key, value ? value : "");
}
#else
void SetEnvVar(const char* key, const char* value) {
    setenv(key, value ? value : "", 1);
}
#endif

std::string ExtractTargetColumn(const SimulationRun& run,
                                const ui::UniversalConfigWidget* widget) {
    if (run.config && !run.config->target_column.empty()) {
        return run.config->target_column;
    }
    if (widget) {
        return widget->GetTarget();
    }
    return {};
}

} // namespace

class SimulationWindow::Impl {
public:
    Impl() : runCounter(0) {
        // Initialize components
        engine = std::make_unique<SimulationEngine>();
        configWidget = std::make_unique<ui::UniversalConfigWidget>();
        resultsWidget = std::make_unique<ui::SimulationResultsWidget_v2>();
        controlsWidget = std::make_unique<ui::SimulationControlsWidget>();
        testModelWindow = std::make_unique<TestModelWindow>();
        
        // Set up available models
        auto models = ModelFactory::GetModelsByCategory();
        configWidget->SetAvailableModels(models);
        controlsWidget->SetAvailableModels(ModelFactory::GetAllModels());
        
        // Set up callbacks
        SetupCallbacks();
        
        // Wire up widgets
        resultsWidget->SetConfigWidget(configWidget.get());
    }

    void PersistRun(const SimulationRun& run);
    void EnsureDatasetRegistered(const std::string& datasetSlug);
    std::pair<std::optional<int64_t>, std::optional<int64_t>> ComputeTimestampBounds(
        const std::shared_ptr<arrow::Table>& table) const;
    void SaveRunAsync(const SimulationRun* run);
    void PumpAsyncUiNotifications();
    void QueueSaveStatus(const std::string& message, bool success);
    bool BeginLoadRunFlow();
    void DrawLoadRunModal();
    void RefreshAvailableRuns();
    void LoadSelectedRun();
    void FinalizeRunLoad(bool success, const std::string& status, std::unique_ptr<SimulationRun> run = nullptr);
    struct SaveStatusMessage {
        std::string message;
        bool success = false;
    };

    bool BuildSimulationRunFromSaved(const Stage1MetadataReader::RunPayload& payload,
                                     const questdb::WalkforwardPredictionSeries* series,
                                     SimulationRun* outRun,
                                     std::string* error);
    FoldResult FoldFromRecord(const Stage1MetadataWriter::WalkforwardFoldRecord& record) const;

private:
    
    void SetupCallbacks() {
        // Controls callbacks
        controlsWidget->SetStartCallback([this]() { StartSimulation(); });
        controlsWidget->SetStopCallback([this]() { StopSimulation(); });
        controlsWidget->SetResetCallback([this]() { ResetSimulation(); });
        controlsWidget->SetModelChangeCallback([this](const std::string& model) { 
            OnModelChanged(model); 
        });
        
        // Engine callbacks
        engine->SetProgressCallback([this](int current, int total) {
            controlsWidget->SetProgress(current, total);
        });
        
        engine->SetFoldCompleteCallback([this](const FoldResult& result) {
            resultsWidget->AddFoldResult(result);
            
            // Track performance
            if (!result.model_learned_nothing) {
                metrics::PerformanceMetrics::RegressionMetrics metrics = {};
                metrics.hit_rate = result.hit_rate;
                metrics.sharpe_ratio = result.avg_return_on_signals / 
                    (result.std_return_on_signals > 0 ? result.std_return_on_signals : 1.0f);
                performanceTracker.AddFoldMetrics(result.fold_number, metrics);
            }
        });
        
        engine->SetCompleteCallback([this](const SimulationRun& run) {
            // The run from engine should already have the config stored
            // But update the current run in resultsWidget to ensure it has everything
        resultsWidget->UpdateCurrentRun(run);
        controlsWidget->SetRunning(false);  // Now it's safe to update UI
            
            // Show summary - calculate directly from fold results for consistency
            if (!run.foldResults.empty()) {
                float avgHitRate = 0.0f;
                float totalPF = 0.0f;
                float totalWins = 0.0f, totalLosses = 0.0f;
                int foldsWithSignals = 0;
                
                for (const auto& fold : run.foldResults) {
                    if (fold.n_signals > 0) {
                        avgHitRate += fold.hit_rate;
                        foldsWithSignals++;
                    }
                    totalWins += fold.sum_wins;
                    totalLosses += fold.sum_losses;
                }
                
                if (foldsWithSignals > 0) {
                    avgHitRate /= foldsWithSignals;
                }
                
                if (totalLosses > 0) {
                    totalPF = totalWins / totalLosses;
                } else if (totalWins > 0) {
                    totalPF = 999.0f;
                }
                
                std::ostringstream msg;
                msg << "Completed: Hit Rate=" << std::fixed << std::setprecision(1) 
                    << (avgHitRate * 100) << "%, PF=" << std::setprecision(2) << totalPF;
                controlsWidget->SetStatusMessage(msg.str());
            } else {
                controlsWidget->SetStatusMessage("Simulation stopped");
            }
        });
    }
    
    void StartSimulation() {
        // Don't start if already running
        if (engine->IsRunning()) {
            return;
        }
        
        // Get model type from config widget
        std::string modelType = configWidget->GetSelectedModelType();
        if (modelType.empty()) {
            controlsWidget->SetStatusMessage("Please select a model");
            return;
        }
        
        // Create model
        auto model = ModelFactory::CreateModel(modelType);
        if (!model) {
            controlsWidget->SetStatusMessage("Failed to create model: " + modelType);
            return;
        }
        
        if (!model->IsAvailable()) {
            controlsWidget->SetStatusMessage("Model not available: " + model->GetAvailabilityError());
            return;
        }
        
        // Ensure a dataset is selected
        std::optional<DatasetMetadata> datasetMeta;
        if (timeSeriesWindow) {
            datasetMeta = timeSeriesWindow->GetActiveDataset();
        }
        if (!datasetMeta) {
            controlsWidget->SetStatusMessage("Select or export a dataset in the Dataset Manager before running.");
            return;
        }
        std::string datasetSlug = !datasetMeta->dataset_slug.empty()
            ? datasetMeta->dataset_slug
            : datasetMeta->indicator_measurement;
        if (datasetSlug.empty()) {
            datasetSlug = datasetMeta->dataset_id;
        }
        if (datasetSlug.empty()) {
            datasetSlug = "dataset";
        }
        std::string indicatorMeasurement = !datasetMeta->indicator_measurement.empty()
            ? datasetMeta->indicator_measurement
            : datasetSlug;
        std::string datasetId = !datasetMeta->dataset_id.empty()
            ? datasetMeta->dataset_id
            : Stage1MetadataWriter::MakeDeterministicUuid(indicatorMeasurement);

        // Get configuration
        auto features = configWidget->GetFeatures();
        auto target = configWidget->GetTarget();
        bool usingFeatureSchedule = configWidget->IsUsingFeatureSchedule();
        auto featureSchedule = configWidget->GetFeatureSchedule();
        
        // Get model caching setting from controls widget
        bool useModelCaching = controlsWidget->IsModelCachingEnabled();
        
        if (!usingFeatureSchedule && features.empty()) {
            controlsWidget->SetStatusMessage("Please select features");
            return;
        }
        
        if (target.empty()) {
            controlsWidget->SetStatusMessage("Please select target");
            return;
        }
        
        // Create model configuration
        std::unique_ptr<ModelConfigBase> modelConfig;
        
        // Get model-specific config from widget
        auto widgetConfig = configWidget->GetConfig();
        if (modelType == "XGBoost" && widgetConfig.has_value()) {
            try {
                auto xgbConfig = std::make_unique<XGBoostConfig>();
                *xgbConfig = std::any_cast<XGBoostConfig>(widgetConfig);
                if (usingFeatureSchedule) {
                    xgbConfig->use_feature_schedule = true;
                    xgbConfig->feature_schedule = featureSchedule;
                    // Still need some initial features for pre-caching
                    xgbConfig->feature_columns = configWidget->GetFeaturesForRange(0, 100000);  // Get features for full range
                } else {
                    xgbConfig->feature_columns = features;
                }
                xgbConfig->target_column = target;
                xgbConfig->calculate_training_profit_factor = configWidget->GetCalculateTrainingPF();
                xgbConfig->reuse_previous_model = useModelCaching;  // Use cached model when fold fails
                modelConfig = std::move(xgbConfig);
            } catch (...) {
                controlsWidget->SetStatusMessage("Invalid configuration for " + modelType);
                return;
            }
        } else {
            // Default config - for now just create XGBoost config
            // TODO: Handle other model types
            auto xgbConfig = std::make_unique<XGBoostConfig>();
            if (usingFeatureSchedule) {
                xgbConfig->use_feature_schedule = true;
                xgbConfig->feature_schedule = featureSchedule;
                // Still need some initial features for pre-caching
                xgbConfig->feature_columns = configWidget->GetFeaturesForRange(0, 100000);  // Get features for full range
            } else {
                xgbConfig->feature_columns = features;
            }
            xgbConfig->target_column = target;
            xgbConfig->calculate_training_profit_factor = configWidget->GetCalculateTrainingPF();
            xgbConfig->reuse_previous_model = useModelCaching;  // Use cached model when fold fails
            modelConfig = std::move(xgbConfig);
        }
        
        // Create a new run in results widget
        SimulationRun newRun;
        newRun.name = "Run " + std::to_string(++runCounter);
        newRun.model_type = modelType;
        newRun.using_feature_schedule = usingFeatureSchedule;
        
        if (usingFeatureSchedule) {
            // Show that we're using a feature schedule
            newRun.config_description = "Feature Schedule (dynamic), Target: " + target;
            newRun.feature_schedule = featureSchedule;
        } else {
            newRun.config_description = configWidget->GetConfig().has_value() ? 
                "Features: " + std::to_string(features.size()) + ", Target: " + target : "Default config";
        }
        newRun.walk_forward_config = configWidget->GetWalkForwardConfig();
        newRun.startTime = std::chrono::system_clock::now();
        newRun.endTime = newRun.startTime; // Initialize to same as start
        newRun.completed = false;
        newRun.dataset_measurement = indicatorMeasurement;
        newRun.dataset_id = datasetId;
        
        // Store the configuration so TestModel can use it (BEFORE moving to engine)
        if (modelType == "XGBoost" && modelConfig) {
            auto* xgbSrc = dynamic_cast<XGBoostConfig*>(modelConfig.get());
            if (xgbSrc) {
                auto xgbCopy = std::make_unique<XGBoostConfig>();
                *xgbCopy = *xgbSrc;  // Copy the full config including features and target
                newRun.config = std::move(xgbCopy);
            }
        }
        
        // Set up engine (this moves modelConfig, so must be after copying)
        engine->SetModel(std::move(model));
        engine->SetModelConfig(std::move(modelConfig));
        engine->SetWalkForwardConfig(configWidget->GetWalkForwardConfig());
        engine->SetDataSource(timeSeriesWindow);
        engine->SetDatasetContext(datasetId, datasetSlug, indicatorMeasurement);
        engine->EnableModelCaching(useModelCaching);
        
        // Clear previous results
        performanceTracker = metrics::PerformanceTracker();
        
        resultsWidget->AddRun(std::move(newRun));
        
        // Update UI state
        controlsWidget->SetRunning(true);
        controlsWidget->ResetTimer();  // Reset timer when starting new simulation
        controlsWidget->SetStatusMessage("Running " + modelType + " simulation...");
        
        // Start
        engine->StartSimulation();
    }
    
    void StopSimulation() {
        engine->StopSimulation();
        // Don't immediately set running to false - let the engine tell us when it's actually stopped
        controlsWidget->SetStatusMessage("Stopping simulation (waiting for current fold to complete)...");
    }
    
    void ResetSimulation() {
        StopSimulation();
        resultsWidget->ClearRuns();
        performanceTracker = metrics::PerformanceTracker();
        runCounter = 0;  // Reset counter when clearing runs
        controlsWidget->SetStatusMessage("Ready");
    }
    
    void OnModelChanged(const std::string& modelType) {
        // This would update the config widget to show the new model
        // For now, just log it
        std::cout << "Model changed to: " << modelType << std::endl;
    }

public:
    // Components
    std::unique_ptr<SimulationEngine> engine;
    std::unique_ptr<ui::UniversalConfigWidget> configWidget;
    std::unique_ptr<ui::SimulationResultsWidget_v2> resultsWidget;
    std::unique_ptr<ui::SimulationControlsWidget> controlsWidget;
    std::unique_ptr<TestModelWindow> testModelWindow;
    
    // State
    TimeSeriesWindow* timeSeriesWindow = nullptr;
    metrics::PerformanceTracker performanceTracker;
    // Model caching is controlled by SimulationControlsWidget
    float configPanelHeight = 400.0f;
    int runCounter;  // Counter for simple run naming
    std::unordered_set<std::string> m_savedRunIds;
    std::unordered_set<std::string> m_registeredDatasets;
    std::atomic<bool> m_saveInProgress{false};
    bool loadRunModalOpen = false;
    bool loadRunsRefreshing = false;
    std::string loadRunStatus;
    std::string loadDatasetId;
    std::string loadDatasetSlug;
    int selectedSavedRun = -1;
    std::vector<Stage1MetadataReader::RunSummary> savedRuns;
    std::mutex loadMutex;
    bool loadRunInProgress = false;
    std::string pendingControlsStatus;
    std::unique_ptr<SimulationRun> pendingLoadedRun;
    std::mutex saveStatusMutex;
    std::optional<SaveStatusMessage> pendingSaveStatus;
};

// SimulationWindow implementation (thin wrapper)
SimulationWindow::SimulationWindow() 
    : m_isVisible(false)
    , pImpl(std::make_unique<Impl>()) {
}

SimulationWindow::~SimulationWindow() = default;

void SimulationWindow::SetTimeSeriesWindow(TimeSeriesWindow* tsWindow) {
    pImpl->timeSeriesWindow = tsWindow;
    pImpl->configWidget->SetDataSource(tsWindow);
    pImpl->testModelWindow->SetDataSource(tsWindow);
}

void SimulationWindow::SetVisible(bool visible) {
    m_isVisible = visible;
}

bool SimulationWindow::HasResults() const {
    return pImpl->resultsWidget && pImpl->resultsWidget->GetRunCount() > 0;
}

const SimulationRun* SimulationWindow::GetLastResults() const {
    if (pImpl->resultsWidget) {
        const int runCount = pImpl->resultsWidget->GetRunCount();
        if (runCount > 0) {
            return pImpl->resultsWidget->GetRunByIndex(runCount - 1);
        }
    }
    if (pImpl->engine) {
        const auto& run = pImpl->engine->GetCurrentRun();
        if (!run.foldResults.empty()) {
            return &run;
        }
    }
    return nullptr;
}

std::vector<std::string> simulation::SimulationWindow::GetRunNames() const {
    std::vector<std::string> names;
    if (!pImpl->resultsWidget) return names;
    
    // Access the simulation runs from results widget
    auto runCount = pImpl->resultsWidget->GetRunCount();
    for (int i = 0; i < runCount; ++i) {
        auto run = pImpl->resultsWidget->GetRunByIndex(i);
        if (run) {
            names.push_back(run->name.empty() ? 
                "Run " + std::to_string(i + 1) : run->name);
        }
    }
    return names;
}

const SimulationRun* simulation::SimulationWindow::GetRunByIndex(int index) const {
    if (!pImpl->resultsWidget) return nullptr;
    return pImpl->resultsWidget->GetRunByIndex(index);
}

int simulation::SimulationWindow::GetRunCount() const {
    if (!pImpl->resultsWidget) return 0;
    return pImpl->resultsWidget->GetRunCount();
}

void SimulationWindow::Draw() {
    if (!m_isVisible) return;
    
    pImpl->PumpAsyncUiNotifications();
    
    // Update elapsed time for the controls widget only if running
    static auto lastFrameTime = std::chrono::steady_clock::now();
    if (pImpl->engine->IsRunning()) {
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;
        pImpl->controlsWidget->UpdateElapsedTime(deltaTime);
    } else {
        // Reset the timer reference when not running
        lastFrameTime = std::chrono::steady_clock::now();
    }
    
    ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Trading Simulation", &m_isVisible, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }
    
    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Test Model", nullptr, pImpl->testModelWindow->IsVisible())) {
                pImpl->testModelWindow->SetVisible(!pImpl->testModelWindow->IsVisible());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    
    // Check for fold examination request before drawing tabs
    auto selectedFold = pImpl->resultsWidget->GetSelectedFold();
    bool switchToTestModel = false;
    if (selectedFold.valid) {
        const SimulationRun* sourceRun = nullptr;
        if (selectedFold.runIndex >= 0) {
            sourceRun = pImpl->resultsWidget->GetRunByIndex(selectedFold.runIndex);
        }
        if (!sourceRun) {
            const auto& currentRun = pImpl->engine->GetCurrentRun();
            if (!currentRun.model_type.empty() || !currentRun.foldResults.empty()) {
                sourceRun = &currentRun;
            }
        }

        SimulationRun fallbackRun;
        const SimulationRun* runForTest = sourceRun;

        if (!runForTest) {
            fallbackRun.name = selectedFold.runName.empty() ? "Ad-hoc Run" : selectedFold.runName;
            fallbackRun.model_type = selectedFold.modelType.empty() ? "XGBoost" : selectedFold.modelType;
            fallbackRun.walk_forward_config = pImpl->configWidget->GetWalkForwardConfig();
            fallbackRun.using_feature_schedule = pImpl->configWidget->IsUsingFeatureSchedule();
            fallbackRun.feature_schedule = pImpl->configWidget->GetFeatureSchedule();

            if (pImpl->timeSeriesWindow) {
                if (auto datasetMeta = pImpl->timeSeriesWindow->GetActiveDataset()) {
                    fallbackRun.dataset_measurement = datasetMeta->indicator_measurement.empty()
                        ? datasetMeta->dataset_slug
                        : datasetMeta->indicator_measurement;
                    fallbackRun.dataset_id = datasetMeta->dataset_id;
                } else {
                    fallbackRun.dataset_measurement = pImpl->timeSeriesWindow->GetSuggestedDatasetId();
                }
            }

            auto xgbConfig = std::make_unique<XGBoostConfig>();
            std::any widgetConfig = pImpl->configWidget->GetConfig();
            if (widgetConfig.has_value()) {
                try {
                    *xgbConfig = std::any_cast<XGBoostConfig>(widgetConfig);
                } catch (const std::bad_any_cast&) {
                }
            } else if (auto* engineConfig = dynamic_cast<XGBoostConfig*>(pImpl->engine->GetModelConfig())) {
                *xgbConfig = *engineConfig;
            } else {
                xgbConfig->feature_columns = pImpl->configWidget->GetFeatures();
                xgbConfig->target_column = pImpl->configWidget->GetTarget();
                xgbConfig->learning_rate = 0.01f;
                xgbConfig->max_depth = 4;
                xgbConfig->min_child_weight = 10.0f;
                xgbConfig->subsample = 0.8f;
                xgbConfig->colsample_bytree = 0.7f;
                xgbConfig->lambda = 2.0f;
                xgbConfig->num_boost_round = 2000;
                xgbConfig->early_stopping_rounds = 50;
                xgbConfig->val_split_ratio = 0.8f;
            }
            fallbackRun.config = std::move(xgbConfig);
            runForTest = &fallbackRun;
        }

        if (runForTest) {
            pImpl->testModelWindow->SetFromFold(selectedFold.fold, *runForTest);
        }
        pImpl->testModelWindow->SetVisible(true);
        pImpl->resultsWidget->ClearSelectedFold();
        switchToTestModel = true;
    }
    
    // Main tab bar for Simulation and Test Model
    if (ImGui::BeginTabBar("MainSimulationTabs")) {
        // Force tab selection if needed
        if (switchToTestModel) {
            ImGui::SetTabItemClosed("Simulation");
        }
        
        if (ImGui::BeginTabItem("Simulation")) {
            // Top section: Controls
            pImpl->controlsWidget->Draw();
            
            ImGui::Separator();
            
            // Configuration panel (collapsible)
            float availHeight = ImGui::GetContentRegionAvail().y;
            
            if (ImGui::CollapsingHeader("Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginChild("ConfigPanel", ImVec2(0, 250), true);
                pImpl->configWidget->Draw();
                ImGui::EndChild();
            }
            
            ImGui::Separator();
            
            // Model caching option is in the SimulationControlsWidget above
            // (removed duplicate checkbox)
            ImGui::Checkbox("Auto-scroll Results", &autoScrollResults);
            pImpl->resultsWidget->SetAutoScroll(autoScrollResults);
            
            ImGui::SameLine();
            ImGui::Checkbox("Auto-fit Plot", &autoFitPlot);
            pImpl->resultsWidget->SetAutoFitPlot(autoFitPlot);

            ImGui::SameLine();
            if (ImGui::Button("Load Saved Run...")) {
                if (pImpl->BeginLoadRunFlow()) {
                    ImGui::OpenPopup("Load Saved Run");
                }
            }
            
            ImGui::Separator();
            
            // Results section with new layout (plot always visible, tabs for runs)
            ImGui::BeginChild("ResultsPanel", ImVec2(0, 0), false);
            pImpl->resultsWidget->Draw();
            int pending = pImpl->resultsWidget->ConsumePendingSaveRequest();
            if (pending >= 0) {
                const SimulationRun* run = pImpl->resultsWidget->GetRunByIndex(pending);
                pImpl->SaveRunAsync(run);
            }
            
            ImGui::EndChild();
            
            ImGui::EndTabItem();
        }
        
        bool testModelOpen = switchToTestModel;
        if (ImGui::BeginTabItem("Test Model", nullptr, testModelOpen ? ImGuiTabItemFlags_SetSelected : 0)) {
            pImpl->testModelWindow->Draw();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
    pImpl->DrawLoadRunModal();
}

// Static initialization
void InitializeSimulationModels() {
    static bool initialized = false;
    if (!initialized) {
        // Register XGBoost
        ModelFactory::ModelRegistration xgboost_reg;
        xgboost_reg.create_model = []() { 
            return std::make_unique<models::XGBoostModel>(); 
        };
        xgboost_reg.create_widget = []() { 
            return std::make_unique<models::XGBoostWidget>(); 
        };
        xgboost_reg.category = "Tree-Based";
        xgboost_reg.description = "Gradient boosting with XGBoost library";
        ModelFactory::RegisterModel("XGBoost", xgboost_reg);
        
        // Future models would be registered here
        // RegisterLinearRegression();
        // RegisterNeuralNetwork();
        
        initialized = true;
    }
}

void SimulationWindow::Impl::PersistRun(const SimulationRun& run) {
    if (!resultsWidget) {
        return;
    }
    if (run.foldResults.empty()) {
        QueueSaveStatus("Cannot save run without fold results.", false);
        return;
    }

    std::string datasetSlug = run.dataset_measurement;
    if (datasetSlug.empty() && timeSeriesWindow) {
        datasetSlug = timeSeriesWindow->GetSuggestedDatasetId();
    }
    if (datasetSlug.empty()) {
        datasetSlug = "dataset";
    }
    datasetSlug = ToSlug(datasetSlug);
    EnsureDatasetRegistered(datasetSlug);

    std::string measurement = run.prediction_measurement;
    if (measurement.empty()) {
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            run.startTime.time_since_epoch()).count();
        measurement = datasetSlug + "_wf" + std::to_string(timestamp);
    } else {
        measurement = ToSlug(measurement);
    }

    Stage1MetadataWriter::WalkforwardRecord record;
    std::string datasetUuid = run.dataset_id.empty()
        ? Stage1MetadataWriter::MakeDeterministicUuid(datasetSlug)
        : run.dataset_id;
    record.dataset_id = datasetUuid;
    record.run_id = Stage1MetadataWriter::MakeDeterministicUuid(measurement);
    record.prediction_measurement = measurement;
    record.target_column = ExtractTargetColumn(run, configWidget.get());
    record.feature_columns = ExtractFeatureColumns(run, configWidget.get());
    record.hyperparameters_json = HyperparamsToJson(run, configWidget.get());
    record.walk_config_json = WalkConfigToJson(run.walk_forward_config);
    record.summary_metrics_json = SummaryMetricsToJson(run);
    record.status = run.completed ? "COMPLETED" : "INCOMPLETE";
    record.requested_by.clear();

    const auto defaultStart = std::chrono::system_clock::now();
    record.started_at = (run.startTime.time_since_epoch().count() == 0)
        ? defaultStart : run.startTime;
    record.completed_at = (run.endTime > run.startTime)
        ? run.endTime : std::chrono::system_clock::now();
    record.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        record.completed_at - record.started_at).count();

    record.folds.reserve(run.foldResults.size());
    for (const auto& fold : run.foldResults) {
        Stage1MetadataWriter::WalkforwardFoldRecord foldRecord;
        foldRecord.fold_number = fold.fold_number;
        foldRecord.train_start = fold.train_start;
        foldRecord.train_end = fold.train_end;
        foldRecord.test_start = fold.test_start;
        foldRecord.test_end = fold.test_end;
        foldRecord.samples_train = fold.n_train_samples;
        foldRecord.samples_test = fold.n_test_samples;
        if (fold.best_iteration >= 0) {
            foldRecord.best_iteration = fold.best_iteration;
        }
        if (std::isfinite(fold.best_score)) {
            foldRecord.best_score = fold.best_score;
        }
        foldRecord.hit_rate = fold.hit_rate;
        foldRecord.profit_factor_test = fold.profit_factor_test;
        foldRecord.long_threshold_optimal = fold.long_threshold_optimal;
        foldRecord.short_threshold_optimal = fold.short_threshold_optimal;
        foldRecord.prediction_threshold_scaled = fold.prediction_threshold_scaled;
        foldRecord.prediction_threshold_original = fold.prediction_threshold_original;
        foldRecord.dynamic_positive_threshold = fold.dynamic_positive_threshold;
        foldRecord.short_threshold_scaled = fold.short_threshold_scaled;
        foldRecord.short_threshold_original = fold.short_threshold_original;
        foldRecord.long_threshold_95th = fold.long_threshold_95th;
        foldRecord.short_threshold_5th = fold.short_threshold_5th;
        foldRecord.n_signals = fold.n_signals;
        foldRecord.n_short_signals = fold.n_short_signals;
        foldRecord.signal_sum = fold.signal_sum;
        foldRecord.short_signal_sum = fold.short_signal_sum;
        foldRecord.signal_rate = fold.signal_rate;
        foldRecord.short_signal_rate = fold.short_signal_rate;
        foldRecord.avg_return_on_signals = fold.avg_return_on_signals;
        foldRecord.median_return_on_signals = fold.median_return_on_signals;
        foldRecord.std_return_on_signals = fold.std_return_on_signals;
        foldRecord.avg_return_on_short_signals = fold.avg_return_on_short_signals;
        foldRecord.avg_predicted_return_on_signals = fold.avg_predicted_return_on_signals;
        foldRecord.short_hit_rate = fold.short_hit_rate;
        foldRecord.running_sum = fold.running_sum;
        foldRecord.running_sum_short = fold.running_sum_short;
        foldRecord.running_sum_dual = fold.running_sum_dual;
        foldRecord.sum_wins = fold.sum_wins;
        foldRecord.sum_losses = fold.sum_losses;
        foldRecord.sum_short_wins = fold.sum_short_wins;
        foldRecord.sum_short_losses = fold.sum_short_losses;
        foldRecord.profit_factor_train = fold.profit_factor_train;
        foldRecord.profit_factor_short_train = fold.profit_factor_short_train;
        foldRecord.profit_factor_short_test = fold.profit_factor_short_test;
        foldRecord.model_learned_nothing = fold.model_learned_nothing;
        foldRecord.used_cached_model = fold.used_cached_model;
        record.folds.push_back(foldRecord);
    }

    if (m_savedRunIds.find(record.run_id) != m_savedRunIds.end()) {
        QueueSaveStatus("Run already saved.", true);
        return;
    }

    auto ensureEnv = [](const char* key, const char* fallback) {
        const char* existing = std::getenv(key);
        if (!existing || !*existing) {
            SetEnvVar(key, fallback);
        }
    };
    ensureEnv("STAGE1_POSTGRES_HOST", "45.85.147.236");
    ensureEnv("STAGE1_POSTGRES_PORT", "5432");
    ensureEnv("STAGE1_POSTGRES_DB", "stage1_trading");
    ensureEnv("STAGE1_POSTGRES_USER", "stage1_app");
    ensureEnv("STAGE1_POSTGRES_PASSWORD", "TempPass2025");

    resultsWidget->SetSaveStatus("Saving run '" + measurement + "'...", true);

    std::string exportError;
    if (!questdb::ExportWalkforwardPredictions(run, record, {}, &exportError)) {
        QueueSaveStatus("QuestDB export failed for '" + measurement + "': " + exportError, false);
        return;
    }

    std::string stage1Error;
    if (!Stage1MetadataWriter::Instance().RecordWalkforwardRun(record, &stage1Error)) {
        const std::string message = stage1Error.empty()
            ? "Stage1 export failed for '" + measurement + "'."
            : "Stage1 export failed: " + stage1Error;
        QueueSaveStatus(message, false);
        return;
    }
    m_savedRunIds.insert(record.run_id);
    QueueSaveStatus("Run exported to Stage1 (measurement '" + measurement + "').", true);
}

std::pair<std::optional<int64_t>, std::optional<int64_t>>
SimulationWindow::Impl::ComputeTimestampBounds(const std::shared_ptr<arrow::Table>& table) const {
    std::optional<int64_t> first;
    std::optional<int64_t> last;
    if (!table) {
        return {first, last};
    }
    int tsIndex = table->schema()->GetFieldIndex("timestamp_unix");
    if (tsIndex < 0) {
        return {first, last};
    }
    auto column = table->column(tsIndex);
    if (!column) {
        return {first, last};
    }
    for (int chunkIdx = 0; chunkIdx < column->num_chunks() && !first.has_value(); ++chunkIdx) {
        auto chunk = column->chunk(chunkIdx);
        if (chunk->type_id() != arrow::Type::INT64) {
            continue;
        }
        auto intArray = std::static_pointer_cast<arrow::Int64Array>(chunk);
        for (int64_t i = 0; i < intArray->length(); ++i) {
            if (!intArray->IsValid(i)) {
                continue;
            }
            first = intArray->Value(i);
            break;
        }
    }
    for (int chunkIdx = column->num_chunks() - 1; chunkIdx >= 0 && !last.has_value(); --chunkIdx) {
        auto chunk = column->chunk(chunkIdx);
        if (chunk->type_id() != arrow::Type::INT64) {
            continue;
        }
        auto intArray = std::static_pointer_cast<arrow::Int64Array>(chunk);
        for (int64_t i = intArray->length() - 1; i >= 0; --i) {
            if (!intArray->IsValid(i)) {
                continue;
            }
            last = intArray->Value(i);
            break;
        }
    }
    return {first, last};
}

void SimulationWindow::Impl::EnsureDatasetRegistered(const std::string& datasetSlug) {
    if (datasetSlug.empty()) {
        return;
    }
    auto datasetMeta = timeSeriesWindow ? timeSeriesWindow->GetActiveDataset() : std::nullopt;
    if (!datasetMeta) {
        return;
    }
    std::string slug = !datasetMeta->dataset_slug.empty() ? datasetMeta->dataset_slug : datasetSlug;
    if (m_registeredDatasets.count(slug)) {
        return;
    }

    Stage1MetadataWriter::DatasetRecord record;
    record.dataset_id = !datasetMeta->dataset_id.empty()
        ? datasetMeta->dataset_id
        : Stage1MetadataWriter::MakeDeterministicUuid(slug);
    record.dataset_slug = slug;
    record.symbol = datasetMeta->symbol.empty() ? slug : datasetMeta->symbol;
    record.granularity = "unknown";
    record.source = "laptop_imgui";
    record.ohlcv_measurement = datasetMeta->ohlcv_measurement;
    record.indicator_measurement = datasetMeta->indicator_measurement.empty() ? slug : datasetMeta->indicator_measurement;
    record.ohlcv_row_count = datasetMeta->ohlcv_rows;
    record.indicator_row_count = datasetMeta->indicator_rows;
    record.created_at = std::chrono::system_clock::now();

    if (timeSeriesWindow) {
        const auto* df = timeSeriesWindow->GetDataFrame();
        if (df) {
            if (auto table = df->get_cpu_table()) {
                auto bounds = ComputeTimestampBounds(table);
                record.indicator_first_timestamp_unix = bounds.first;
                record.indicator_last_timestamp_unix = bounds.second;
            }
        }
    }

    Stage1MetadataWriter::Instance().RecordDatasetExport(record);
    m_registeredDatasets.insert(slug);
}

void SimulationWindow::Impl::SaveRunAsync(const SimulationRun* run) {
    if (!run) {
        resultsWidget->SetSaveStatus("Selected run is unavailable.", false);
        return;
    }
    bool expected = false;
    if (!m_saveInProgress.compare_exchange_strong(expected, true)) {
        resultsWidget->SetSaveStatus("Another save is already running.", false);
        return;
    }
    std::thread([this, run]() {
        PersistRun(*run);
        m_saveInProgress.store(false);
    }).detach();
}

void SimulationWindow::Impl::QueueSaveStatus(const std::string& message, bool success) {
    std::lock_guard<std::mutex> lock(saveStatusMutex);
    pendingSaveStatus = SaveStatusMessage{message, success};
}

void SimulationWindow::Impl::PumpAsyncUiNotifications() {
    std::string statusUpdate;
    std::unique_ptr<SimulationRun> runToAdd;
    std::optional<SaveStatusMessage> saveUpdate;

    {
        std::lock_guard<std::mutex> lock(loadMutex);
        if (!pendingControlsStatus.empty()) {
            statusUpdate = std::move(pendingControlsStatus);
            pendingControlsStatus.clear();
        }
        if (pendingLoadedRun) {
            runToAdd = std::move(pendingLoadedRun);
        }
    }

    {
        std::lock_guard<std::mutex> lock(saveStatusMutex);
        if (pendingSaveStatus.has_value()) {
            saveUpdate = pendingSaveStatus;
            pendingSaveStatus.reset();
        }
    }

    if (!statusUpdate.empty()) {
        controlsWidget->SetStatusMessage(statusUpdate);
    }
    if (runToAdd) {
        resultsWidget->AddRun(std::move(*runToAdd));
    }
    if (saveUpdate.has_value()) {
        resultsWidget->SetSaveStatus(saveUpdate->message, saveUpdate->success);
    }
}

bool SimulationWindow::Impl::BeginLoadRunFlow() {
    auto datasetMeta = timeSeriesWindow ? timeSeriesWindow->GetActiveDataset() : std::nullopt;
    const bool hasDataset = datasetMeta && !datasetMeta->dataset_id.empty();
    if (!hasDataset) {
        controlsWidget->SetStatusMessage("Select or export a dataset before loading runs.");
    }
    {
        std::lock_guard<std::mutex> lock(loadMutex);
        loadDatasetId = hasDataset ? datasetMeta->dataset_id : std::string();
        loadDatasetSlug = hasDataset
            ? (datasetMeta->dataset_slug.empty() ? datasetMeta->dataset_id : datasetMeta->dataset_slug)
            : std::string();
        savedRuns.clear();
        selectedSavedRun = -1;
        loadRunsRefreshing = false;
        loadRunInProgress = false;
        loadRunStatus = hasDataset
            ? "Loading runs..."
            : "Select a dataset in the Dataset Manager, then try again.";
    }
    loadRunModalOpen = true;
    if (hasDataset) {
        RefreshAvailableRuns();
    }
    return true;
}

void SimulationWindow::Impl::RefreshAvailableRuns() {
    std::string datasetId;
    {
        std::lock_guard<std::mutex> lock(loadMutex);
        if (loadDatasetId.empty()) {
            loadRunStatus = "Dataset ID is missing.";
            return;
        }
        if (loadRunsRefreshing) {
            return;
        }
        loadRunsRefreshing = true;
        loadRunStatus = "Loading runs...";
        datasetId = loadDatasetId;
    }

    std::thread([this, datasetId]() {
        std::vector<Stage1MetadataReader::RunSummary> runs;
        std::string error;
        const bool ok = Stage1MetadataReader::ListRunsForDataset(datasetId, &runs, &error);
        std::lock_guard<std::mutex> lock(loadMutex);
        loadRunsRefreshing = false;
        if (ok) {
            savedRuns = std::move(runs);
            if (savedRuns.empty()) {
                loadRunStatus = "No saved runs for this dataset.";
            } else {
                loadRunStatus.clear();
            }
            if (selectedSavedRun >= static_cast<int>(savedRuns.size())) {
                selectedSavedRun = -1;
            }
        } else {
            savedRuns.clear();
            loadRunStatus = error.empty() ? "Failed to query saved runs." : error;
        }
    }).detach();
}

void SimulationWindow::Impl::LoadSelectedRun() {
    Stage1MetadataReader::RunSummary summary;
    {
        std::lock_guard<std::mutex> lock(loadMutex);
        if (loadRunInProgress) {
            loadRunStatus = "Another load is already in progress.";
            return;
        }
        if (selectedSavedRun < 0 || selectedSavedRun >= static_cast<int>(savedRuns.size())) {
            loadRunStatus = "Select a run from the list.";
            return;
        }
        summary = savedRuns[selectedSavedRun];
        const std::string label = summary.measurement.empty() ? summary.run_id : summary.measurement;
        loadRunStatus = "Loading run '" + label + "'...";
        loadRunInProgress = true;
    }

    std::thread([this, summary]() {
        Stage1MetadataReader::RunPayload payload;
        std::string error;
        if (!Stage1MetadataReader::LoadRunPayload(summary.run_id, &payload, &error)) {
            FinalizeRunLoad(false, error.empty() ? "Failed to load run metadata." : error);
            return;
        }
        questdb::WalkforwardPredictionSeries series;
        bool predictionsAvailable = true;
        std::string predictionsError;
        if (!questdb::ImportWalkforwardPredictions(payload.prediction_measurement, &series, &predictionsError)) {
            predictionsAvailable = false;
            std::cerr << "[SimulationWindow] Warning: could not load predictions for run "
                      << payload.run_id << ": " << predictionsError << std::endl;
        }
        SimulationRun loadedRun;
        if (!BuildSimulationRunFromSaved(payload,
                                         predictionsAvailable ? &series : nullptr,
                                         &loadedRun,
                                         &error)) {
            FinalizeRunLoad(false, error.empty() ? "Failed to rebuild run payload." : error);
            return;
        }
        const std::string label = payload.prediction_measurement.empty()
            ? payload.run_id
            : payload.prediction_measurement;
        auto runPtr = std::make_unique<SimulationRun>(std::move(loadedRun));
        std::string statusMessage = "Loaded run " + label;
        if (!predictionsAvailable) {
            statusMessage += " (predictions unavailable: "
                + (predictionsError.empty() ? "see console" : predictionsError) + ")";
        }
        FinalizeRunLoad(true, statusMessage, std::move(runPtr));
    }).detach();
}

void SimulationWindow::Impl::FinalizeRunLoad(bool success,
                                             const std::string& status,
                                             std::unique_ptr<SimulationRun> run) {
    {
        std::lock_guard<std::mutex> lock(loadMutex);
        loadRunStatus = status;
        loadRunInProgress = false;
        pendingControlsStatus = status;
        if (success && run) {
            pendingLoadedRun = std::move(run);
        }
    }
}

void SimulationWindow::Impl::DrawLoadRunModal() {
    if (!loadRunModalOpen) {
        return;
    }

    // Open the popup if not already open
    if (!ImGui::IsPopupOpen("Load Saved Run")) {
        ImGui::OpenPopup("Load Saved Run");
    }

    // Copy state from mutex-protected variables
    std::vector<Stage1MetadataReader::RunSummary> runsCopy;
    std::string statusText;
    std::string datasetSlug;
    bool refreshing = false;
    bool loadingRun = false;
    int currentSelection = -1;

    {
        std::lock_guard<std::mutex> lock(loadMutex);
        runsCopy = savedRuns;
        statusText = loadRunStatus;
        datasetSlug = loadDatasetSlug;
        refreshing = loadRunsRefreshing;
        loadingRun = loadRunInProgress;
        currentSelection = selectedSavedRun;
    }

    // Modal will only close via the X button (keepOpen flag)
    bool keepOpen = true;
    if (ImGui::BeginPopupModal("Load Saved Run", &keepOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Dataset: %s", datasetSlug.empty() ? "(unspecified)" : datasetSlug.c_str());

        // Refresh button
        if (ImGui::Button("Refresh")) {
            RefreshAvailableRuns();
        }

        // Load Selected button
        ImGui::SameLine();
        bool canLoad = !refreshing && !loadingRun &&
                       currentSelection >= 0 && currentSelection < static_cast<int>(runsCopy.size());
        ImGui::BeginDisabled(!canLoad);
        if (ImGui::Button("Load Selected")) {
            LoadSelectedRun();
        }
        ImGui::EndDisabled();

        // Show loading/refreshing status
        if (loadingRun) {
            ImGui::SameLine();
            ImGui::TextUnformatted("Loading run...");
        } else if (refreshing) {
            ImGui::SameLine();
            ImGui::TextUnformatted("Refreshing list...");
        }

        ImGui::Separator();

        // Table of saved runs
        const float tableHeight = 300.0f;
        if (ImGui::BeginTable("saved-runs-table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                              ImVec2(650, tableHeight))) {
            ImGui::TableSetupColumn("Measurement");
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Started", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Completed", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(runsCopy.size()); ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool selected = (currentSelection == i);
                const std::string label = runsCopy[i].measurement.empty() ? runsCopy[i].run_id : runsCopy[i].measurement;

                // Selectable to choose a run from the list
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    std::lock_guard<std::mutex> lock(loadMutex);
                    selectedSavedRun = i;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Run ID: %s", runsCopy[i].run_id.c_str());
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(runsCopy[i].status.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(runsCopy[i].started_at.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(runsCopy[i].completed_at.c_str());
            }
            ImGui::EndTable();
        }

        // Status message
        if (!statusText.empty()) {
            ImGui::TextWrapped("%s", statusText.c_str());
        }

        ImGui::EndPopup();
    }

    // Modal was closed via X button
    if (!keepOpen) {
        loadRunModalOpen = false;
    }
}

FoldResult SimulationWindow::Impl::FoldFromRecord(const Stage1MetadataWriter::WalkforwardFoldRecord& record) const {
    FoldResult fold{};
    fold.fold_number = record.fold_number;
    fold.train_start = record.train_start;
    fold.train_end = record.train_end;
    fold.test_start = record.test_start;
    fold.test_end = record.test_end;
    fold.n_train_samples = record.samples_train;
    fold.n_test_samples = record.samples_test;
    fold.best_iteration = record.best_iteration.value_or(-1);
    fold.best_score = record.best_score.value_or(0.0f);
    fold.hit_rate = record.hit_rate;
    fold.short_hit_rate = record.short_hit_rate;
    fold.profit_factor_test = record.profit_factor_test;
    fold.profit_factor_train = record.profit_factor_train;
    fold.profit_factor_short_train = record.profit_factor_short_train;
    fold.profit_factor_short_test = record.profit_factor_short_test;
    fold.n_signals = record.n_signals;
    fold.n_short_signals = record.n_short_signals;
    fold.signal_sum = record.signal_sum;
    fold.short_signal_sum = record.short_signal_sum;
    fold.signal_rate = record.signal_rate;
    fold.short_signal_rate = record.short_signal_rate;
    fold.avg_return_on_signals = record.avg_return_on_signals;
    fold.median_return_on_signals = record.median_return_on_signals;
    fold.std_return_on_signals = record.std_return_on_signals;
    fold.avg_return_on_short_signals = record.avg_return_on_short_signals;
    fold.avg_predicted_return_on_signals = record.avg_predicted_return_on_signals;
    fold.running_sum = record.running_sum;
    fold.running_sum_short = record.running_sum_short;
    fold.running_sum_dual = record.running_sum_dual;
    fold.sum_wins = record.sum_wins;
    fold.sum_losses = record.sum_losses;
    fold.sum_short_wins = record.sum_short_wins;
    fold.sum_short_losses = record.sum_short_losses;
    fold.long_threshold_optimal = record.long_threshold_optimal;
    fold.short_threshold_optimal = record.short_threshold_optimal;
    fold.prediction_threshold_scaled = record.prediction_threshold_scaled;
    fold.prediction_threshold_original = record.prediction_threshold_original;
    fold.dynamic_positive_threshold = record.dynamic_positive_threshold;
    fold.short_threshold_scaled = record.short_threshold_scaled;
    fold.short_threshold_original = record.short_threshold_original;
    fold.long_threshold_95th = record.long_threshold_95th;
    fold.short_threshold_5th = record.short_threshold_5th;
    fold.model_learned_nothing = record.model_learned_nothing;
    fold.used_cached_model = record.used_cached_model;
    return fold;
}

bool SimulationWindow::Impl::BuildSimulationRunFromSaved(
    const Stage1MetadataReader::RunPayload& payload,
    const questdb::WalkforwardPredictionSeries* series,
    SimulationRun* outRun,
    std::string* error) {
    if (!outRun) {
        if (error) *error = "Run destination is null.";
        return false;
    }

    SimulationRun run;
    run.name = payload.prediction_measurement.empty() ? payload.run_id : payload.prediction_measurement;
    run.model_type = "XGBoost";
    run.dataset_measurement = payload.dataset_slug;
    run.dataset_id = payload.dataset_id;
    run.prediction_measurement = payload.prediction_measurement;
    run.walk_forward_config = payload.walk_config;
    auto config = std::make_unique<XGBoostConfig>(payload.hyperparameters);
    config->feature_columns = payload.feature_columns;
    config->target_column = payload.target_column;
    run.config = std::move(config);
    run.config_description = "Loaded from Stage1";
    run.startTime = payload.started_at;
    run.endTime = payload.completed_at;
    run.completed = true;

    run.foldResults.reserve(payload.folds.size());
    for (const auto& foldRecord : payload.folds) {
        run.foldResults.push_back(FoldFromRecord(foldRecord));
    }
    std::sort(run.foldResults.begin(), run.foldResults.end(),
              [](const FoldResult& a, const FoldResult& b) {
                  return a.fold_number < b.fold_number;
              });

    run.profitPlotX.clear();
    run.profitPlotY_long.clear();
    run.profitPlotY_short.clear();
    run.profitPlotY_dual.clear();
    for (const auto& fold : run.foldResults) {
        run.profitPlotX.push_back(static_cast<double>(fold.fold_number));
        run.profitPlotY_long.push_back(fold.running_sum);
        run.profitPlotY_short.push_back(fold.running_sum_short);
        run.profitPlotY_dual.push_back(fold.running_sum_dual);
    }

    if (series && !series->rows.empty()) {
        struct PredictionRow {
            int32_t fold;
            int64_t barIndex;
            int64_t timestamp;
            double prediction;
            double target;
        };
        std::vector<PredictionRow> rows;
        rows.reserve(series->rows.size());
        for (const auto& entry : series->rows) {
            if (!std::isfinite(entry.prediction)) {
                continue;
            }
            rows.push_back({entry.fold_number, entry.bar_index, entry.timestamp_ms, entry.prediction, entry.target});
        }
        std::sort(rows.begin(), rows.end(), [](const PredictionRow& a, const PredictionRow& b) {
            if (a.fold != b.fold) return a.fold < b.fold;
            if (a.barIndex != b.barIndex) return a.barIndex < b.barIndex;
            return a.timestamp < b.timestamp;
        });

        std::unordered_map<int32_t, int> foldOffsets;
        run.all_test_predictions.reserve(rows.size());
        run.all_test_actuals.reserve(rows.size());
        run.all_test_timestamps.reserve(rows.size());
        for (const auto& row : rows) {
            if (!foldOffsets.count(row.fold)) {
                foldOffsets[row.fold] = static_cast<int>(run.all_test_predictions.size());
            }
            run.all_test_predictions.push_back(static_cast<float>(row.prediction));
            const bool hasTarget = std::isfinite(row.target);
            run.all_test_actuals.push_back(hasTarget ? static_cast<float>(row.target) : 0.0f);
            run.all_test_timestamps.push_back(row.timestamp);
        }

        if (!run.foldResults.empty()) {
            run.fold_prediction_offsets.assign(run.foldResults.size(),
                                               static_cast<int>(run.all_test_predictions.size()));
            for (size_t i = 0; i < run.foldResults.size(); ++i) {
                auto it = foldOffsets.find(run.foldResults[i].fold_number);
                if (it != foldOffsets.end()) {
                    run.fold_prediction_offsets[i] = it->second;
                }
            }
        }
        std::cout << "[SimulationWindow] Rebuilt run " << run.name
                  << " predictions=" << run.all_test_predictions.size()
                  << " folds=" << run.foldResults.size() << std::endl;
    } else {
        std::cout << "[SimulationWindow] Rebuilt run " << run.name
                  << " folds=" << run.foldResults.size()
                  << " (no prediction series available)" << std::endl;
    }

    *outRun = std::move(run);
    return true;
}

} // namespace simulation

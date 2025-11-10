#pragma once

#include "SimulationTypes.h"
#include "ISimulationModel_v2.h"
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>

// Forward declarations
class TimeSeriesWindow;

namespace simulation {

/**
 * Optimized Simulation Engine with pre-extracted data caching
 * 
 * Key optimizations:
 * 1. Pre-extracts all data once at simulation start (not per-fold)
 * 2. Uses direct memory access instead of Arrow GetScalar() calls
 * 3. Maintains exact feature column name-to-index mapping
 * 4. Removes unnecessary atomic operations where thread-safety isn't needed
 * 5. Simplified model caching (only last successful model)
 */
class SimulationEngine {
public:
    SimulationEngine();
    ~SimulationEngine();
    
    // Configuration
    void SetTimeSeriesWindow(TimeSeriesWindow* window) { m_timeSeriesWindow = window; }
    void SetDataSource(TimeSeriesWindow* tsWindow) { SetTimeSeriesWindow(tsWindow); }  // Alias for compatibility
    void SetModel(std::unique_ptr<ISimulationModel> model);
    void SetModelConfig(std::unique_ptr<ModelConfigBase> config);
    void SetWalkForwardConfig(const WalkForwardConfig& config);
    ModelConfigBase* GetModelConfig() const { return m_modelConfig.get(); }  // Added for compatibility
    void SetDatasetContext(const std::string& datasetId,
                           const std::string& datasetSlug,
                           const std::string& indicatorMeasurement);
    
    // Control
    void StartSimulation();
    void StopSimulation();
    bool IsRunning() const { return m_isRunning.load(); }
    
    // Results
    const SimulationRun& GetCurrentRun() const { return m_currentRun; }
    const std::vector<SimulationRun>& GetCompletedRuns() const { return m_completedRuns; }
    
    // Progress monitoring
    int GetCurrentFold() const { return m_currentFold; }
    int GetTotalFolds() const { return m_totalFolds; }
    
    // Callbacks (kept for UI updates)
    using ProgressCallback = std::function<void(int current, int total)>;
    using FoldCallback = std::function<void(const FoldResult&)>;
    using CompleteCallback = std::function<void(const SimulationRun&)>;
    
    void SetProgressCallback(ProgressCallback cb) { m_progressCallback = cb; }
    void SetFoldCallback(FoldCallback cb) { m_foldCallback = cb; }
    void SetFoldCompleteCallback(FoldCallback cb) { SetFoldCallback(cb); }  // Alias for compatibility
    void SetCompleteCallback(CompleteCallback cb) { m_completeCallback = cb; }
    
    // Cache control
    void SetEnableCaching(bool enable) { m_enableCaching = enable; }
    void EnableModelCaching(bool enable) { SetEnableCaching(enable); }  // Alias for compatibility
    
private:
    // Pre-extracted data cache (aligned for cache performance)
    struct alignas(64) DataCache {
        std::vector<float> all_features;    // Row-major: [n_rows * n_features]
        std::vector<float> all_targets;     // [n_rows]
        int num_rows = 0;
        int num_features = 0;
        bool is_valid = false;
        
        // Feature name to column index mapping
        std::unordered_map<std::string, int> feature_name_to_index;
        std::vector<std::string> feature_index_to_name;  // For validation
        
        // For feature schedule support - cache ALL features that might be used
        bool using_feature_schedule = false;
        std::unordered_map<std::string, int> all_feature_indices;  // Maps all cached feature names to indices
        
        void Clear() {
            all_features.clear();
            all_targets.clear();
            num_rows = 0;
            num_features = 0;
            is_valid = false;
            feature_name_to_index.clear();
            feature_index_to_name.clear();
            using_feature_schedule = false;
            all_feature_indices.clear();
        }
    };
    
    // Data management
    void PreExtractAllData();
    void ValidateFeatureMapping() const;
    
    // Feature schedule support
    std::vector<std::string> GetFeaturesForFold(int train_start, int train_end) const;
    std::vector<float> GetFeaturesVectorForSchedule(int start_row, int end_row, 
                                                    const std::vector<std::string>& features) const;
    
    // Fast data access (returns pointers, no copying)
    const float* GetFeaturesPtr(int start_row, int num_rows) const;
    const float* GetTargetPtr(int start_row, int num_rows) const;
    
    // Convert pointers to vectors for model interface (minimal copying)
    std::vector<float> GetFeaturesVector(int start_row, int end_row) const;
    std::vector<float> GetTargetVector(int start_row, int end_row) const;
    
    // Simulation thread
    void RunSimulationThread();
    FoldResult ProcessSingleFold(int train_start, int train_end, 
                                  int test_start, int test_end, 
                                  int fold_number);
    int CalculateMaxFolds() const;
    
    // Data source
    TimeSeriesWindow* m_timeSeriesWindow;
    
    // Pre-extracted data cache
    DataCache m_dataCache;
    
    // Model and configuration
    std::unique_ptr<ISimulationModel> m_model;
    std::unique_ptr<ModelConfigBase> m_modelConfig;
    WalkForwardConfig m_walkForwardConfig;
    
    // Simplified model cache (only last successful model)
    struct LastModelCache {
        bool valid = false;
        std::vector<char> serialized_model;
        TransformParams params;
        float threshold_scaled;
        float threshold_original;
        float dynamic_threshold;
        int source_fold;
        
        void Clear() { 
            valid = false; 
            serialized_model.clear();
        }
    };
    LastModelCache m_lastModelCache;
    bool m_enableCaching;
    
    // Thread management (minimal atomics)
    std::thread m_simulationThread;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_shouldStop;
    
    // Progress (regular ints, only simulation thread writes)
    int m_currentFold;
    int m_totalFolds;
    
    // Results
    SimulationRun m_currentRun;
    std::vector<SimulationRun> m_completedRuns;
    std::string m_datasetId;
    std::string m_datasetSlug;
    std::string m_indicatorMeasurement;
    bool m_hasDatasetContext = false;
    
    // Callbacks
    ProgressCallback m_progressCallback;
    FoldCallback m_foldCallback;
    CompleteCallback m_completeCallback;
};

} // namespace simulation

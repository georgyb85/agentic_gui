#include "SimulationEngine.h"
#include "SimulationUtils.h"
#include "PerformanceMetrics.h"
#include "models/XGBoostModel.h"
#include "XGBoostConfig.h"
#include "ThresholdCalculator.h"
#include "../TimeSeriesWindow.h"
#include "../analytics_dataframe.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <set>
#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>

namespace simulation {

SimulationEngine::SimulationEngine()
    : m_timeSeriesWindow(nullptr)
    , m_enableCaching(true)
    , m_isRunning(false)
    , m_shouldStop(false)
    , m_currentFold(0)
    , m_totalFolds(0) {
}

SimulationEngine::~SimulationEngine() {
    // Make sure to stop and wait for thread before destroying
    if (m_isRunning.load()) {
        StopSimulation();
    }
    // Extra safety check
    if (m_simulationThread.joinable()) {
        m_simulationThread.join();
    }
}

void SimulationEngine::SetModel(std::unique_ptr<ISimulationModel> model) {
    if (m_isRunning.load()) {
        std::cerr << "Cannot change model while simulation is running" << std::endl;
        return;
    }
    m_model = std::move(model);
}

void SimulationEngine::SetModelConfig(std::unique_ptr<ModelConfigBase> config) {
    if (m_isRunning.load()) {
        std::cerr << "Cannot change configuration while simulation is running" << std::endl;
        return;
    }
    m_modelConfig = std::move(config);
}

void SimulationEngine::SetWalkForwardConfig(const WalkForwardConfig& config) {
    if (m_isRunning.load()) {
        std::cerr << "Cannot change configuration while simulation is running" << std::endl;
        return;
    }
    m_walkForwardConfig = config;
}

void SimulationEngine::SetDatasetContext(const std::string& datasetId,
                                         const std::string& datasetSlug,
                                         const std::string& indicatorMeasurement) {
    m_datasetId = datasetId;
    m_datasetSlug = datasetSlug;
    m_indicatorMeasurement = indicatorMeasurement;
    m_hasDatasetContext = true;
}

void SimulationEngine::PreExtractAllData() {
    if (!m_timeSeriesWindow || !m_timeSeriesWindow->HasData()) {
        throw std::runtime_error("No data available for extraction");
    }
    
    const chronosflow::AnalyticsDataFrame* dataFrame = m_timeSeriesWindow->GetDataFrame();
    if (!dataFrame) {
        throw std::runtime_error("DataFrame is null");
    }
    
    // Clear existing cache
    m_dataCache.Clear();
    
    // Check if using feature schedule
    m_dataCache.using_feature_schedule = m_modelConfig->use_feature_schedule;
    
    // Get dimensions
    m_dataCache.num_rows = static_cast<int>(dataFrame->num_rows());
    
    // When using schedule, cache ALL features mentioned in schedule
    std::vector<std::string> features_to_cache;
    if (m_dataCache.using_feature_schedule) {
        // m_modelConfig->feature_columns should contain ALL unique features from schedule
        features_to_cache = m_modelConfig->feature_columns;
        std::cout << "Using feature schedule - caching " << features_to_cache.size() 
                  << " unique features from schedule" << std::endl;
    } else {
        features_to_cache = m_modelConfig->feature_columns;
    }
    
    m_dataCache.num_features = static_cast<int>(features_to_cache.size());
    
    std::cout << "Pre-extracting data: " << m_dataCache.num_rows << " rows, " 
              << m_dataCache.num_features << " features" << std::endl;
    
    // Reserve memory for all data (avoid reallocations)
    m_dataCache.all_features.reserve(m_dataCache.num_rows * m_dataCache.num_features);
    m_dataCache.all_targets.reserve(m_dataCache.num_rows);
    
    // Get Arrow table ONCE
    auto table = dataFrame->get_cpu_table();
    if (!table) {
        throw std::runtime_error("Unable to get CPU table");
    }
    
    // Build feature name to index mapping and extract feature columns
    std::vector<std::shared_ptr<arrow::ChunkedArray>> feature_columns;
    for (size_t i = 0; i < features_to_cache.size(); ++i) {
        const std::string& feature_name = features_to_cache[i];
        
        // Map name to index
        m_dataCache.feature_name_to_index[feature_name] = static_cast<int>(i);
        m_dataCache.feature_index_to_name.push_back(feature_name);
        
        // For schedule support, also store in all_feature_indices
        if (m_dataCache.using_feature_schedule) {
            m_dataCache.all_feature_indices[feature_name] = static_cast<int>(i);
        }
        
        // Get column
        auto column = table->GetColumnByName(feature_name);
        if (!column) {
            throw std::runtime_error("Feature column not found: " + feature_name);
        }
        feature_columns.push_back(column);
    }
    
    // Get target column
    auto target_column = table->GetColumnByName(m_modelConfig->target_column);
    if (!target_column) {
        throw std::runtime_error("Target column not found: " + m_modelConfig->target_column);
    }
    
    // Extract all data using bulk operations
    // Process in chunks for better memory locality
    const int CHUNK_SIZE = 1000;
    
    for (int row_start = 0; row_start < m_dataCache.num_rows; row_start += CHUNK_SIZE) {
        int row_end = std::min(row_start + CHUNK_SIZE, m_dataCache.num_rows);
        int chunk_size = row_end - row_start;
        
        // Extract features for this chunk (row-major order)
        for (int row = row_start; row < row_end; ++row) {
            // Extract all features for this row
            for (int feat_idx = 0; feat_idx < m_dataCache.num_features; ++feat_idx) {
                float value = 0.0f;
                
                // Get value from chunked array
                auto& column = feature_columns[feat_idx];
                
                // Find which chunk contains this row
                int64_t current_row = row;
                for (int chunk_idx = 0; chunk_idx < column->num_chunks(); ++chunk_idx) {
                    auto chunk = column->chunk(chunk_idx);
                    if (current_row < chunk->length()) {
                        // Extract value based on type
                        if (chunk->type()->id() == arrow::Type::DOUBLE) {
                            auto array = std::static_pointer_cast<arrow::DoubleArray>(chunk);
                            value = array->IsValid(current_row) ? 
                                static_cast<float>(array->Value(current_row)) : 0.0f;
                        } else if (chunk->type()->id() == arrow::Type::FLOAT) {
                            auto array = std::static_pointer_cast<arrow::FloatArray>(chunk);
                            value = array->IsValid(current_row) ? array->Value(current_row) : 0.0f;
                        } else if (chunk->type()->id() == arrow::Type::INT64) {
                            auto array = std::static_pointer_cast<arrow::Int64Array>(chunk);
                            value = array->IsValid(current_row) ? 
                                static_cast<float>(array->Value(current_row)) : 0.0f;
                        } else if (chunk->type()->id() == arrow::Type::INT32) {
                            auto array = std::static_pointer_cast<arrow::Int32Array>(chunk);
                            value = array->IsValid(current_row) ? 
                                static_cast<float>(array->Value(current_row)) : 0.0f;
                        }
                        break;
                    }
                    current_row -= chunk->length();
                }
                
                m_dataCache.all_features.push_back(value);
            }
        }
        
        // Extract targets for this chunk
        for (int row = row_start; row < row_end; ++row) {
            float value = 0.0f;
            
            // Get value from chunked array
            int64_t current_row = row;
            for (int chunk_idx = 0; chunk_idx < target_column->num_chunks(); ++chunk_idx) {
                auto chunk = target_column->chunk(chunk_idx);
                if (current_row < chunk->length()) {
                    // Extract value based on type
                    if (chunk->type()->id() == arrow::Type::DOUBLE) {
                        auto array = std::static_pointer_cast<arrow::DoubleArray>(chunk);
                        value = array->IsValid(current_row) ? 
                            static_cast<float>(array->Value(current_row)) : 0.0f;
                    } else if (chunk->type()->id() == arrow::Type::FLOAT) {
                        auto array = std::static_pointer_cast<arrow::FloatArray>(chunk);
                        value = array->IsValid(current_row) ? array->Value(current_row) : 0.0f;
                    } else if (chunk->type()->id() == arrow::Type::INT64) {
                        auto array = std::static_pointer_cast<arrow::Int64Array>(chunk);
                        value = array->IsValid(current_row) ? 
                            static_cast<float>(array->Value(current_row)) : 0.0f;
                    } else if (chunk->type()->id() == arrow::Type::INT32) {
                        auto array = std::static_pointer_cast<arrow::Int32Array>(chunk);
                        value = array->IsValid(current_row) ? 
                            static_cast<float>(array->Value(current_row)) : 0.0f;
                    }
                    break;
                }
                current_row -= chunk->length();
            }
            
            m_dataCache.all_targets.push_back(value);
        }
    }
    
    m_dataCache.is_valid = true;
    
    // Validate the extraction
    ValidateFeatureMapping();
    
    std::cout << "Data extraction complete. Cache size: " 
              << (m_dataCache.all_features.size() * sizeof(float) + 
                  m_dataCache.all_targets.size() * sizeof(float)) / (1024.0 * 1024.0) 
              << " MB" << std::endl;
}

void SimulationEngine::ValidateFeatureMapping() const {
    // When using feature schedule, just verify all features are cached
    if (m_dataCache.using_feature_schedule) {
        for (const auto& [name, idx] : m_dataCache.all_feature_indices) {
            if (idx < 0 || idx >= m_dataCache.num_features) {
                throw std::runtime_error("Invalid feature index for " + name);
            }
        }
        return;
    }
    
    // Normal validation for non-schedule mode
    for (size_t i = 0; i < m_modelConfig->feature_columns.size(); ++i) {
        const std::string& expected_name = m_modelConfig->feature_columns[i];
        const std::string& actual_name = m_dataCache.feature_index_to_name[i];
        
        if (expected_name != actual_name) {
            throw std::runtime_error("Feature mapping error: expected " + expected_name + 
                                   " at index " + std::to_string(i) + 
                                   " but got " + actual_name);
        }
        
        auto it = m_dataCache.feature_name_to_index.find(expected_name);
        if (it == m_dataCache.feature_name_to_index.end() || it->second != static_cast<int>(i)) {
            throw std::runtime_error("Feature index mapping error for " + expected_name);
        }
    }
}

const float* SimulationEngine::GetFeaturesPtr(int start_row, int num_rows) const {
    if (!m_dataCache.is_valid) {
        throw std::runtime_error("Data cache not initialized");
    }
    
    if (start_row < 0 || start_row + num_rows > m_dataCache.num_rows) {
        throw std::runtime_error("Invalid row range");
    }
    
    // Return pointer to start of requested data (row-major layout)
    return m_dataCache.all_features.data() + start_row * m_dataCache.num_features;
}

const float* SimulationEngine::GetTargetPtr(int start_row, int num_rows) const {
    if (!m_dataCache.is_valid) {
        throw std::runtime_error("Data cache not initialized");
    }
    
    if (start_row < 0 || start_row + num_rows > m_dataCache.num_rows) {
        throw std::runtime_error("Invalid row range");
    }
    
    return m_dataCache.all_targets.data() + start_row;
}

std::vector<float> SimulationEngine::GetFeaturesVector(int start_row, int end_row) const {
    int num_rows = end_row - start_row;
    int num_elements = num_rows * m_dataCache.num_features;
    
    const float* ptr = GetFeaturesPtr(start_row, num_rows);
    
    // Create vector from pointer range (single allocation and copy)
    return std::vector<float>(ptr, ptr + num_elements);
}

std::vector<float> SimulationEngine::GetTargetVector(int start_row, int end_row) const {
    int num_rows = end_row - start_row;
    
    const float* ptr = GetTargetPtr(start_row, num_rows);
    
    // Create vector from pointer range (single allocation and copy)
    return std::vector<float>(ptr, ptr + num_rows);
}

std::vector<std::string> SimulationEngine::GetFeaturesForFold(int train_start, int train_end) const {
    if (!m_dataCache.using_feature_schedule || m_modelConfig->feature_schedule.empty()) {
        // Not using schedule, return all cached features
        return m_dataCache.feature_index_to_name;
    }
    
    // Parse the feature schedule to find the appropriate feature set
    std::istringstream scheduleStream(m_modelConfig->feature_schedule);
    std::string line;
    
    while (std::getline(scheduleStream, line)) {
        if (line.empty()) continue;
        
        // Parse line format: "startRow-endRow: feature1, feature2, ..."
        size_t dashPos = line.find('-');
        size_t colonPos = line.find(':');
        
        if (dashPos == std::string::npos || colonPos == std::string::npos) {
            continue;
        }
        
        try {
            int rangeStart = std::stoi(line.substr(0, dashPos));
            int rangeEnd = std::stoi(line.substr(dashPos + 1, colonPos - dashPos - 1));
            
            // Check if the training data falls within this schedule range
            // We want to use this schedule if the training range is mostly within it
            // Calculate the midpoint of training data to determine best match
            int train_midpoint = (train_start + train_end) / 2;
            
            if (train_midpoint >= rangeStart && train_midpoint < rangeEnd) {
                // Parse features from this line
                std::vector<std::string> features;
                std::string featuresStr = line.substr(colonPos + 1);
                
                std::istringstream featureStream(featuresStr);
                std::string feature;
                
                while (std::getline(featureStream, feature, ',')) {
                    // Trim whitespace
                    feature.erase(0, feature.find_first_not_of(" \t"));
                    feature.erase(feature.find_last_not_of(" \t") + 1);
                    
                    if (!feature.empty()) {
                        features.push_back(feature);
                    }
                }
                
                std::cout << "Schedule match: range " << rangeStart << "-" << rangeEnd 
                          << " selected for training " << train_start << "-" << train_end 
                          << " (midpoint: " << train_midpoint << ")" << std::endl;
                
                return features;
            }
        } catch (const std::exception&) {
            continue;
        }
    }
    
    // No matching range found, return all features as fallback
    std::cout << "WARNING: No feature schedule match found for training range " 
              << train_start << "-" << train_end << ", using ALL " 
              << m_dataCache.feature_index_to_name.size() << " features as fallback" << std::endl;
    return m_dataCache.feature_index_to_name;
}

std::vector<float> SimulationEngine::GetFeaturesVectorForSchedule(
    int start_row, int end_row, const std::vector<std::string>& features) const {
    
    int num_rows = end_row - start_row;
    int num_features = static_cast<int>(features.size());
    
    std::vector<float> result;
    result.reserve(num_rows * num_features);
    
    // For each row
    for (int row = start_row; row < end_row; ++row) {
        // For each requested feature
        for (const auto& feature_name : features) {
            // Find the feature index in our cache
            auto it = m_dataCache.all_feature_indices.find(feature_name);
            if (it == m_dataCache.all_feature_indices.end()) {
                // Feature not in cache, use 0
                result.push_back(0.0f);
            } else {
                int feature_idx = it->second;
                // Get value from cache (row-major layout)
                result.push_back(m_dataCache.all_features[row * m_dataCache.num_features + feature_idx]);
            }
        }
    }
    
    return result;
}

void SimulationEngine::StartSimulation() {
    if (m_isRunning.load()) {
        std::cerr << "Simulation already running" << std::endl;
        return;
    }
    
    // Clean up old thread if it exists
    if (m_simulationThread.joinable()) {
        m_simulationThread.join();
    }
    
    if (!m_model || !m_modelConfig) {
        std::cerr << "Model and configuration must be set before starting simulation" << std::endl;
        return;
    }
    
    if (!m_timeSeriesWindow || !m_timeSeriesWindow->HasData()) {
        std::cerr << "No data available for simulation" << std::endl;
        return;
    }
    
    // Pre-extract all data ONCE
    try {
        PreExtractAllData();
    } catch (const std::exception& e) {
        std::cerr << "Failed to pre-extract data: " << e.what() << std::endl;
        return;
    }
    
    // Initialize run
    m_currentRun = SimulationRun();
    m_currentRun.name = "Run_" + std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    m_currentRun.model_type = m_model->GetModelType();
    
    // Store a copy of the configuration in the run
    if (m_model->GetModelType() == "XGBoost") {
        auto* xgb_src = dynamic_cast<XGBoostConfig*>(m_modelConfig.get());
        if (xgb_src) {
            m_currentRun.config = std::make_unique<XGBoostConfig>(*xgb_src);
        }
    }
    
    m_currentRun.walk_forward_config = m_walkForwardConfig;
    m_currentRun.startTime = std::chrono::system_clock::now();
    m_currentRun.completed = false;
    
    std::string datasetSlug;
    if (m_hasDatasetContext) {
        datasetSlug = m_datasetSlug.empty() ? "dataset" : m_datasetSlug;
        m_currentRun.dataset_measurement = m_indicatorMeasurement.empty() ? datasetSlug : m_indicatorMeasurement;
        m_currentRun.dataset_id = m_datasetId;
    } else {
        if (m_timeSeriesWindow) {
            datasetSlug = m_timeSeriesWindow->GetSuggestedDatasetId();
        }
        if (datasetSlug.empty()) {
            datasetSlug = "dataset";
        }
        m_currentRun.dataset_measurement = datasetSlug;
    }
    if (m_currentRun.prediction_measurement.empty()) {
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            m_currentRun.startTime.time_since_epoch()).count();
        const std::string base = datasetSlug.empty() ? "dataset" : datasetSlug;
        m_currentRun.prediction_measurement = base + "_wf" + std::to_string(ts);
    }
    
    // Clear cache
    m_lastModelCache.Clear();
    
    // Calculate total folds
    m_totalFolds = CalculateMaxFolds();
    m_currentFold = 0;
    
    // Start simulation thread
    m_isRunning.store(true);
    m_shouldStop.store(false);
    m_simulationThread = std::thread(&SimulationEngine::RunSimulationThread, this);
}

void SimulationEngine::StopSimulation() {
    if (!m_isRunning.load()) return;
    
    m_shouldStop.store(true);
    
    // Don't block the UI thread
}

void SimulationEngine::RunSimulationThread() {
    if (m_dataCache.using_feature_schedule) {
        std::cout << "Starting " << m_model->GetModelType() << " simulation with FEATURE SCHEDULE" << std::endl;
        std::cout << "Cached " << m_dataCache.num_features << " unique features from schedule" << std::endl;
        std::cout << "Features will be selected dynamically per fold" << std::endl;
    } else {
        std::cout << "Starting " << m_model->GetModelType() << " simulation with "
                  << m_modelConfig->feature_columns.size() << " features" << std::endl;
    }
    std::cout << "Target: " << m_modelConfig->target_column << std::endl;
    std::cout << "Walk-forward: Train=" << m_walkForwardConfig.train_size 
              << ", Test=" << m_walkForwardConfig.test_size
              << ", Gap=" << m_walkForwardConfig.train_test_gap
              << ", Step=" << m_walkForwardConfig.fold_step << std::endl;
    
    float running_sum = 0.0f;
    float running_sum_short = 0.0f;
    float running_sum_dual = 0.0f;
    int total_folds = m_totalFolds;
    
    // Calculate actual end fold
    int max_folds = CalculateMaxFolds();
    int actual_end_fold = (m_walkForwardConfig.end_fold == -1) ? 
        max_folds : std::min(m_walkForwardConfig.end_fold, max_folds);
    
    // Walk-forward loop
    for (int fold = m_walkForwardConfig.start_fold; 
         fold <= actual_end_fold && !m_shouldStop.load(); 
         ++fold) {
        
        m_currentFold = fold - m_walkForwardConfig.start_fold + 1;
        
        // Calculate data ranges
        int train_start = m_walkForwardConfig.initial_offset + 
            (fold - m_walkForwardConfig.start_fold) * m_walkForwardConfig.fold_step;
        int train_end = train_start + m_walkForwardConfig.train_size;
        int test_start = train_end + m_walkForwardConfig.train_test_gap;
        int test_end = test_start + m_walkForwardConfig.test_size;
        
        try {
            // Process fold
            FoldResult result = ProcessSingleFold(train_start, train_end, test_start, test_end, fold);
            
            // Update running sums for all trade modes
            if (result.n_signals > 0) {
                running_sum += result.signal_sum;  // Long-only
                std::cout << "===> Long Running sum: " << std::fixed << std::setprecision(6) 
                         << running_sum << " <====" << std::endl;
                std::cout << "Long Signals: " << result.n_signals 
                         << ", Hit rate: " << std::fixed << std::setprecision(2) 
                         << (result.hit_rate * 100) << "%" << std::endl;
            } else {
                std::cout << "No long signals generated." << std::endl;
            }
            
            if (result.n_short_signals > 0) {
                running_sum_short += result.short_signal_sum;  // Short-only
                std::cout << "===> Short Running sum: " << std::fixed << std::setprecision(6) 
                         << running_sum_short << " <====" << std::endl;
                std::cout << "Short Signals: " << result.n_short_signals 
                         << ", Hit rate: " << std::fixed << std::setprecision(2) 
                         << (result.short_hit_rate * 100) << "%" << std::endl;
            } else {
                std::cout << "No short signals generated." << std::endl;
            }
            
            // Dual mode combines both long and short profits
            running_sum_dual = running_sum + running_sum_short;
            std::cout << "===> Dual Running sum: " << std::fixed << std::setprecision(6) 
                     << running_sum_dual << " <====" << std::endl;
            
            result.running_sum = running_sum;
            result.running_sum_short = running_sum_short;
            result.running_sum_dual = running_sum_dual;
            
            // Add to results
            if (!m_shouldStop.load()) {
                m_currentRun.foldResults.push_back(result);
                m_currentRun.profitPlotX.push_back(fold);
                m_currentRun.profitPlotY_long.push_back(running_sum);
                m_currentRun.profitPlotY_short.push_back(running_sum_short);
                m_currentRun.profitPlotY_dual.push_back(running_sum_dual);
                
                // Notify callbacks
                if (m_progressCallback) {
                    m_progressCallback(m_currentFold, total_folds);
                }
                if (m_foldCallback) {
                    m_foldCallback(result);
                }
            }
            
            std::cout << std::string(50, '-') << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Error in fold " << fold << ": " << e.what() << std::endl;
        }
    }
    
    // Set end time and mark completion status
    m_currentRun.endTime = std::chrono::system_clock::now();
    // Only mark as completed if we didn't stop early
    m_currentRun.completed = !m_shouldStop.load();
    
    // Aggregate all predictions into the run
    m_currentRun.all_test_predictions.clear();
    m_currentRun.all_test_actuals.clear();
    m_currentRun.fold_prediction_offsets.clear();
    m_currentRun.all_test_timestamps.clear();
    
    for (const auto& fold : m_currentRun.foldResults) {
        // Store offset for this fold's predictions
        m_currentRun.fold_prediction_offsets.push_back(m_currentRun.all_test_predictions.size());
        
        // Append predictions and actuals
        m_currentRun.all_test_predictions.insert(
            m_currentRun.all_test_predictions.end(),
            fold.test_predictions_original.begin(),
            fold.test_predictions_original.end()
        );
        
        // Get actuals for this fold
        auto y_test = GetTargetVector(fold.test_start, fold.test_end);
        m_currentRun.all_test_actuals.insert(
            m_currentRun.all_test_actuals.end(),
            y_test.begin(),
            y_test.end()
        );
        
        // Get timestamps for this fold
        if (m_timeSeriesWindow) {
            for (int i = fold.test_start; i < fold.test_end && i < fold.test_start + fold.n_test_samples; ++i) {
                int64_t timestamp = m_timeSeriesWindow->GetTimestamp(i);
                m_currentRun.all_test_timestamps.push_back(timestamp);
            }
        }
    }
    
    // Calculate summary metrics
    if (!m_currentRun.foldResults.empty()) {
        std::cout << "\n=== Simulation Summary ===" << std::endl;
        std::cout << "Total folds: " << m_currentRun.foldResults.size() << std::endl;
        std::cout << "Final sum: " << running_sum << std::endl;
        std::cout << "Total predictions stored: " << m_currentRun.all_test_predictions.size() << std::endl;
    }
    
    // Notify completion
    if (m_completeCallback) {
        m_completeCallback(m_currentRun);
    }
    
    m_isRunning.store(false);
    std::cout << m_model->GetModelType() << " simulation completed." << std::endl;
}

FoldResult SimulationEngine::ProcessSingleFold(
    int train_start, int train_end,
    int test_start, int test_end,
    int fold_number) {
    
    FoldResult result = {};
    result.fold_number = fold_number;
    result.train_start = train_start;
    result.train_end = train_end;
    result.test_start = test_start;
    result.test_end = test_end;
    result.profit_factor_train = 0.0f;
    result.profit_factor_test = 0.0f;
    result.sum_wins = 0.0f;
    result.sum_losses = 0.0f;
    result.profit_factor_short_train = 0.0f;
    result.profit_factor_short_test = 0.0f;
    result.sum_short_wins = 0.0f;
    result.sum_short_losses = 0.0f;
    
    try {
        // Calculate train/val split
        float val_split = m_modelConfig->val_split_ratio;
        int split_point = train_start + (int)((train_end - train_start) * val_split);
        
        result.n_train_samples = split_point - train_start;
        result.n_val_samples = train_end - split_point;
        result.n_test_samples = test_end - test_start;
        
        // Get data vectors based on whether we're using a feature schedule
        std::vector<float> X_train, X_val, X_test;
        std::vector<std::string> features_for_fold;
        int num_features;
        
        if (m_dataCache.using_feature_schedule) {
            // Get the features for this specific fold from the schedule
            features_for_fold = GetFeaturesForFold(train_start, train_end);
            num_features = static_cast<int>(features_for_fold.size());
            
            // Store the features used in the result
            result.features_used = features_for_fold;
            
            // Get feature vectors with only the scheduled features
            X_train = GetFeaturesVectorForSchedule(train_start, split_point, features_for_fold);
            X_val = GetFeaturesVectorForSchedule(split_point, train_end, features_for_fold);
            X_test = GetFeaturesVectorForSchedule(test_start, test_end, features_for_fold);
            
            std::cout << "Fold " << fold_number << " using " << num_features 
                      << " features from schedule for range " << train_start << "-" << train_end << std::endl;
            std::cout << "Features: ";
            for (size_t i = 0; i < features_for_fold.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << features_for_fold[i];
            }
            std::cout << std::endl;
        } else {
            // Use all cached features
            X_train = GetFeaturesVector(train_start, split_point);
            X_val = GetFeaturesVector(split_point, train_end);
            X_test = GetFeaturesVector(test_start, test_end);
            num_features = m_dataCache.num_features;
        }
        
        // Target vectors are always the same
        auto y_train = GetTargetVector(train_start, split_point);
        auto y_val = GetTargetVector(split_point, train_end);
        auto y_test = GetTargetVector(test_start, test_end);
        
        // Train model
        auto train_result = m_model->Train(X_train, y_train, X_val, y_val, 
                                          *m_modelConfig, num_features);
        
        result.best_iteration = train_result.best_iteration;
        result.best_score = train_result.best_score;
        result.model_learned_nothing = !train_result.model_learned;
        result.mean_scale = train_result.transform_params.mean;
        result.std_scale = train_result.transform_params.std_dev;
        
        // Calculate profit factor on training data if model learned and option is enabled
        if (!result.model_learned_nothing && m_modelConfig->calculate_training_profit_factor) {
            // Combine train and validation data for profit factor calculation
            std::vector<float> X_trainval;
            std::vector<float> y_trainval;
            X_trainval.reserve(X_train.size() + X_val.size());
            y_trainval.reserve(y_train.size() + y_val.size());
            X_trainval.insert(X_trainval.end(), X_train.begin(), X_train.end());
            X_trainval.insert(X_trainval.end(), X_val.begin(), X_val.end());
            y_trainval.insert(y_trainval.end(), y_train.begin(), y_train.end());
            y_trainval.insert(y_trainval.end(), y_val.begin(), y_val.end());
            
            int n_trainval = y_trainval.size();
            auto pred_train = m_model->Predict(X_trainval, n_trainval, num_features);
            
            if (pred_train.success) {
                float train_wins = 0.0f, train_losses = 0.0f;
                TransformParams params = {result.mean_scale, result.std_scale, 
                                        m_modelConfig->tanh_scaling_factor};
                
                // Calculate threshold if not yet set
                float threshold_orig = train_result.validation_threshold;
                if (m_modelConfig->use_standardization || m_modelConfig->use_tanh_transform) {
                    threshold_orig = utils::Transform::InverseTransformPrediction(
                        train_result.validation_threshold, params,
                        m_modelConfig->use_tanh_transform,
                        m_modelConfig->use_standardization,
                        m_modelConfig->tanh_scaling_factor
                    );
                }
                
                for (size_t i = 0; i < pred_train.predictions.size(); ++i) {
                    float pred_orig = utils::Transform::InverseTransformPrediction(
                        pred_train.predictions[i], params,
                        m_modelConfig->use_tanh_transform,
                        m_modelConfig->use_standardization,
                        m_modelConfig->tanh_scaling_factor
                    );
                    
                    if (pred_orig > threshold_orig) {
                        float ret = y_trainval[i];
                        if (ret > 0) {
                            train_wins += ret;
                        } else {
                            train_losses += std::abs(ret);
                        }
                    }
                }
                
                if (train_losses > 0) {
                    result.profit_factor_train = train_wins / train_losses;
                } else if (train_wins > 0) {
                    result.profit_factor_train = 999.0f;
                } else {
                    result.profit_factor_train = 0.0f;
                }
            } else {
                result.profit_factor_train = 0.0f;
            }
        } else {
            result.profit_factor_train = 0.0f;
        }
        
        // Handle model caching/reuse (simplified)
        if (result.model_learned_nothing && m_enableCaching && 
            m_modelConfig->reuse_previous_model && m_lastModelCache.valid) {
            // Use cached model
            std::cout << "Fold " << fold_number << " failed - using cached model from fold " 
                     << m_lastModelCache.source_fold << std::endl;
            
            // Load cached model
            if (m_model->Deserialize(m_lastModelCache.serialized_model)) {
                result.used_cached_model = true;
                result.model_learned_nothing = false;
                result.mean_scale = m_lastModelCache.params.mean;
                result.std_scale = m_lastModelCache.params.std_dev;
                result.prediction_threshold_scaled = m_lastModelCache.threshold_scaled;
                result.prediction_threshold_original = m_lastModelCache.threshold_original;
                result.dynamic_positive_threshold = m_lastModelCache.dynamic_threshold;
                // Keep the actual training iterations from failed attempt, not from cached model
                // result.best_iteration already has the value from train_result
            } else {
                std::cerr << "Failed to load cached model" << std::endl;
            }
        } else if (!result.model_learned_nothing) {
            // Model learned - calculate threshold and cache if enabled
            result.prediction_threshold_scaled = train_result.validation_threshold;
            
            // Inverse transform threshold
            result.prediction_threshold_original = utils::Transform::InverseTransformPrediction(
                train_result.validation_threshold,
                train_result.transform_params,
                m_modelConfig->use_tanh_transform,
                m_modelConfig->use_standardization,
                m_modelConfig->tanh_scaling_factor
            );
            
            result.dynamic_positive_threshold = 0.0f;

            // Compute Long thresholds for this fold (both methods), independent of model config
            //  - 95th Percentile on VALIDATION predictions (in-sample, no leakage)
            //  - Optimal ROC (PF-based) on TRAINING predictions and returns (in-sample, no leakage)
            {
                // Params for inverse transform
                TransformParams params_for_inv = {result.mean_scale, result.std_scale, m_modelConfig->tanh_scaling_factor};

                // 95th percentile (validation)
                result.long_threshold_95th = 0.0f;
                if (result.n_val_samples > 0) {
                    auto pred_val = m_model->Predict(X_val, result.n_val_samples, num_features);
                    if (pred_val.success && !pred_val.predictions.empty()) {
                        std::vector<float> val_preds_original;
                        val_preds_original.reserve(pred_val.predictions.size());
                        for (float p : pred_val.predictions) {
                            val_preds_original.push_back(
                                utils::Transform::InverseTransformPrediction(
                                    p, params_for_inv,
                                    m_modelConfig->use_tanh_transform,
                                    m_modelConfig->use_standardization,
                                    m_modelConfig->tanh_scaling_factor));
                        }
                        try {
                            result.long_threshold_95th = utils::Statistics::CalculateQuantile(val_preds_original, 0.95f);
                        } catch (...) {
                            // Fallback to previously computed per-config threshold
                            result.long_threshold_95th = result.prediction_threshold_original;
                        }
                    } else {
                        // Fallback if prediction failed
                        result.long_threshold_95th = result.prediction_threshold_original;
                    }
                } else {
                    // No validation data; fallback
                    result.long_threshold_95th = result.prediction_threshold_original;
                }

                // Optimal ROC (training)
                result.long_threshold_optimal = 0.0f;
                if (result.n_train_samples > 0) {
                    auto pred_train_only = m_model->Predict(X_train, result.n_train_samples, num_features);
                    if (pred_train_only.success && !pred_train_only.predictions.empty()) {
                        std::vector<float> train_preds_original;
                        train_preds_original.reserve(pred_train_only.predictions.size());
                        for (float p : pred_train_only.predictions) {
                            train_preds_original.push_back(
                                utils::Transform::InverseTransformPrediction(
                                    p, params_for_inv,
                                    m_modelConfig->use_tanh_transform,
                                    m_modelConfig->use_standardization,
                                    m_modelConfig->tanh_scaling_factor));
                        }
                        try {
                            // min_kept_percent = 1 (%), consistent with prior usage
                            result.long_threshold_optimal = ThresholdCalculator::CalculateOptimalThreshold(
                                train_preds_original, y_train, 1);
                        } catch (...) {
                            // Fallback
                            result.long_threshold_optimal = result.prediction_threshold_original;
                        }
                    } else {
                        // Fallback if prediction failed
                        result.long_threshold_optimal = result.prediction_threshold_original;
                    }
                } else {
                    // No training data; fallback
                    result.long_threshold_optimal = result.prediction_threshold_original;
                }
            }
            
            // Cache model if enabled (simplified - only keep last successful)
            if (m_enableCaching && m_modelConfig->reuse_previous_model) {
                m_lastModelCache.valid = true;
                m_lastModelCache.serialized_model = m_model->Serialize();
                m_lastModelCache.params = train_result.transform_params;
                m_lastModelCache.threshold_scaled = result.prediction_threshold_scaled;
                m_lastModelCache.threshold_original = result.prediction_threshold_original;
                m_lastModelCache.dynamic_threshold = result.dynamic_positive_threshold;
                m_lastModelCache.source_fold = fold_number;
            }
        }
        
        // Make predictions if model is valid
        if (!result.model_learned_nothing) {
            auto pred_result = m_model->Predict(X_test, result.n_test_samples, num_features);
            
            if (pred_result.success) {
                // Inverse transform predictions and store them
                result.test_predictions_original.reserve(pred_result.predictions.size());
                
                TransformParams params = {result.mean_scale, result.std_scale, 
                                        m_modelConfig->tanh_scaling_factor};
                
                for (float pred : pred_result.predictions) {
                    float original = utils::Transform::InverseTransformPrediction(
                        pred, params,
                        m_modelConfig->use_tanh_transform,
                        m_modelConfig->use_standardization,
                        m_modelConfig->tanh_scaling_factor
                    );
                    result.test_predictions_original.push_back(original);
                }
                
                // Calculate short thresholds from TRAINING data (not test data!)
                // This must use training predictions to avoid data leakage
                // Reconstruct training+validation data
                std::vector<float> X_trainval_short;
                std::vector<float> y_trainval_short;
                X_trainval_short.reserve(X_train.size() + X_val.size());
                y_trainval_short.reserve(y_train.size() + y_val.size());
                X_trainval_short.insert(X_trainval_short.end(), X_train.begin(), X_train.end());
                X_trainval_short.insert(X_trainval_short.end(), X_val.begin(), X_val.end());
                y_trainval_short.insert(y_trainval_short.end(), y_train.begin(), y_train.end());
                y_trainval_short.insert(y_trainval_short.end(), y_val.begin(), y_val.end());
                
                if (!y_trainval_short.empty()) {
                    // Get training predictions
                    auto pred_train = m_model->Predict(X_trainval_short, y_trainval_short.size(), num_features);
                    if (pred_train.success && !pred_train.predictions.empty()) {
                        std::vector<float> train_preds_original;
                        train_preds_original.reserve(pred_train.predictions.size());
                        
                        TransformParams params = {result.mean_scale, result.std_scale, 
                                                m_modelConfig->tanh_scaling_factor};
                        
                        // Transform predictions to original scale
                        for (float pred : pred_train.predictions) {
                            float original = utils::Transform::InverseTransformPrediction(
                                pred, params,
                                m_modelConfig->use_tanh_transform,
                                m_modelConfig->use_standardization,
                                m_modelConfig->tanh_scaling_factor
                            );
                            train_preds_original.push_back(original);
                        }
                        
                        // Calculate 5th percentile threshold from TRAINING predictions
                        std::vector<float> sorted_train_preds = train_preds_original;
                        std::sort(sorted_train_preds.begin(), sorted_train_preds.end());
                        int percentile_idx_5th = static_cast<int>(0.05f * (sorted_train_preds.size() - 1));
                        percentile_idx_5th = std::max(0, std::min(percentile_idx_5th, 
                                                     static_cast<int>(sorted_train_preds.size() - 1)));
                        result.short_threshold_5th = sorted_train_preds[percentile_idx_5th];
                        
                        // Calculate optimal short threshold using profit factor optimization
                        result.short_threshold_optimal = ThresholdCalculator::CalculateOptimalShortThreshold(
                            train_preds_original, y_trainval_short, 1);
                        
                        // Select threshold based on configuration (same method as used for long trades)
                        // Get the threshold method from the XGBoost config
                        const XGBoostConfig* xgbConfig = dynamic_cast<const XGBoostConfig*>(m_modelConfig.get());
                        if (xgbConfig) {
                            if (xgbConfig->threshold_method == ThresholdMethod::OptimalROC) {
                                result.short_threshold_original = result.short_threshold_optimal;
                            } else {
                                // Default to percentile method
                                result.short_threshold_original = result.short_threshold_5th;
                            }
                        } else {
                            // Fallback to 5th percentile if not XGBoost config
                            result.short_threshold_original = result.short_threshold_5th;
                        }
                    } else {
                        // Fallback: if training predictions fail, set conservative thresholds
                        std::cerr << "Warning: Failed to get training predictions for short threshold calculation" << std::endl;
                        result.short_threshold_5th = -999.0f;  // Very low threshold (no shorts)
                        result.short_threshold_optimal = -999.0f;
                        result.short_threshold_original = -999.0f;
                    }
                } else {
                    // No training data available - set conservative thresholds
                    std::cerr << "Warning: No training data available for short threshold calculation" << std::endl;
                    result.short_threshold_5th = -999.0f;  // Very low threshold (no shorts)
                    result.short_threshold_optimal = -999.0f;
                    result.short_threshold_original = -999.0f;
                }
                
                // Calculate trading metrics
                result.n_signals = 0;
                result.signal_sum = 0.0f;
                result.sum_wins = 0.0f;
                result.sum_losses = 0.0f;
                std::vector<float> returns_on_signals;
                
                // Calculate SHORT trading metrics
                result.n_short_signals = 0;
                result.short_signal_sum = 0.0f;
                result.sum_short_wins = 0.0f;
                result.sum_short_losses = 0.0f;
                std::vector<float> returns_on_short_signals;
                
                // Simplified position tracking without stop loss
                
                for (size_t i = 0; i < result.test_predictions_original.size(); ++i) {
                    float pred = result.test_predictions_original[i];
                    float ret = y_test[i];
                    
                    // Long trade logic
                    bool long_signal = pred > result.prediction_threshold_original &&
                                      pred > result.dynamic_positive_threshold;
                    
                    if (long_signal) {
                        result.n_signals++;
                        result.signal_sum += ret;
                        returns_on_signals.push_back(ret);
                        
                        // Track wins and losses for profit factor
                        if (ret > 0) {
                            result.sum_wins += ret;
                        } else {
                            result.sum_losses += std::abs(ret);
                        }
                    }
                    
                    // Short trade logic (prediction below threshold)
                    bool short_signal = pred < result.short_threshold_original;
                    
                    if (short_signal) {
                        result.n_short_signals++;
                        // For short trades, we profit when actual return is negative
                        float short_ret = -ret;  // Invert the return for short position
                        result.short_signal_sum += short_ret;
                        returns_on_short_signals.push_back(short_ret);
                        
                        // Track wins and losses for short profit factor
                        if (short_ret > 0) {
                            result.sum_short_wins += short_ret;
                        } else {
                            result.sum_short_losses += std::abs(short_ret);
                        }
                    }
                }
                
                // Calculate long trade statistics
                if (result.n_signals > 0) {
                    result.signal_rate = (float)result.n_signals / result.test_predictions_original.size();
                    result.avg_return_on_signals = result.signal_sum / result.n_signals;
                    result.median_return_on_signals = utils::Statistics::CalculateMedian(returns_on_signals);
                    result.std_return_on_signals = utils::Statistics::CalculateStdDev(
                        returns_on_signals, result.avg_return_on_signals);
                    
                    // Calculate hit rate
                    int hits = 0;
                    for (float ret : returns_on_signals) {
                        if (ret > 0) hits++;
                    }
                    result.hit_rate = (float)hits / result.n_signals;
                    
                    // Calculate profit factor for test data (long)
                    if (result.sum_losses > 0) {
                        result.profit_factor_test = result.sum_wins / result.sum_losses;
                    } else if (result.sum_wins > 0) {
                        result.profit_factor_test = 999.0f; // Cap at 999 when no losses
                    } else {
                        result.profit_factor_test = 0.0f;
                    }
                } else {
                    // No long signals
                    result.profit_factor_test = 0.0f;
                }
                
                // Calculate short trade statistics
                if (result.n_short_signals > 0) {
                    result.short_signal_rate = (float)result.n_short_signals / result.test_predictions_original.size();
                    result.avg_return_on_short_signals = result.short_signal_sum / result.n_short_signals;
                    
                    // Calculate short hit rate
                    int short_hits = 0;
                    for (float ret : returns_on_short_signals) {
                        if (ret > 0) short_hits++;
                    }
                    result.short_hit_rate = (float)short_hits / result.n_short_signals;
                    
                    // Calculate profit factor for test data (short)
                    if (result.sum_short_losses > 0) {
                        result.profit_factor_short_test = result.sum_short_wins / result.sum_short_losses;
                    } else if (result.sum_short_wins > 0) {
                        result.profit_factor_short_test = 999.0f; // Cap at 999 when no losses
                    } else {
                        result.profit_factor_short_test = 0.0f;
                    }
                } else {
                    // No short signals
                    result.profit_factor_short_test = 0.0f;
                    result.short_signal_rate = 0.0f;
                    result.avg_return_on_short_signals = 0.0f;
                    result.short_hit_rate = 0.0f;
                }
            }
        } else {
            // No model available
            std::cout << "Fold " << fold_number << " - no predictions (model failed, no cache)" << std::endl;
            result.n_signals = 0;
            result.signal_sum = 0.0f;
            result.signal_rate = 0.0f;
            result.hit_rate = 0.0f;
            result.profit_factor_train = 0.0f;
            result.profit_factor_test = 0.0f;
            result.sum_wins = 0.0f;
            result.sum_losses = 0.0f;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error in ProcessSingleFold: " << e.what() << std::endl;
        result.model_learned_nothing = true;
    }
    
    return result;
}

int SimulationEngine::CalculateMaxFolds() const {
    if (!m_dataCache.is_valid) {
        return 0;
    }
    
    int num_rows = m_dataCache.num_rows;
    
    // Calculate how many folds fit in the data
    int required_per_fold = m_walkForwardConfig.train_size + 
                           m_walkForwardConfig.train_test_gap + 
                           m_walkForwardConfig.test_size;
    
    int available_rows = num_rows - m_walkForwardConfig.initial_offset;
    if (available_rows <= required_per_fold) {
        return 0;
    }
    
    // Calculate based on step size
    int max_folds = 1 + (available_rows - required_per_fold) / m_walkForwardConfig.fold_step;
    
    // Adjust for start fold offset
    max_folds = m_walkForwardConfig.start_fold + max_folds - 1;
    
    return max_folds;
}

} // namespace simulation

#pragma once

#include "SimulationEngine.h"
#include "OptimizedSimulationEngine.h"
#include <memory>
#include <vector>
#include <string>
#include <chrono>

namespace simulation {
namespace validation {

// Results comparison for validation
struct ComparisonResult {
    bool results_identical;
    bool performance_improved;
    
    // Timing comparison
    std::chrono::milliseconds original_time;
    std::chrono::milliseconds optimized_time;
    double speedup_factor;
    
    // Results validation
    size_t total_folds_compared;
    size_t identical_folds;
    std::vector<int> mismatched_fold_numbers;
    
    // Detailed differences (if any)
    struct FoldDifference {
        int fold_number;
        std::string field_name;
        float original_value;
        float optimized_value;
        float absolute_diff;
        float relative_diff;
    };
    std::vector<FoldDifference> differences;
    
    // Performance metrics
    double memory_usage_mb;
    double avg_fold_time_original_ms;
    double avg_fold_time_optimized_ms;
    
    // Summary
    std::string GetSummary() const;
};

// Comprehensive validation utility
class OptimizationValidator {
public:
    OptimizationValidator();
    ~OptimizationValidator();
    
    // Set data source and configuration
    void SetDataSource(TimeSeriesWindow* tsWindow);
    void SetModel(std::unique_ptr<ISimulationModel> model);
    void SetModelConfig(std::unique_ptr<ModelConfigBase> config);
    void SetWalkForwardConfig(const WalkForwardConfig& config);
    
    // Validation settings
    void SetFloatTolerance(float tolerance) { m_float_tolerance = tolerance; }
    void SetValidateAllFolds(bool validate) { m_validate_all_folds = validate; }
    void SetMaxFoldsToTest(int max_folds) { m_max_folds_to_test = max_folds; }
    
    // Run comprehensive validation
    ComparisonResult RunValidation();
    
    // Individual tests
    bool ValidateDataExtraction();
    bool ValidateFeatureMapping();
    bool ValidateSingleFold(int fold_number);
    
    // Performance testing
    std::chrono::milliseconds TimeOriginalSimulation(int num_folds = 10);
    std::chrono::milliseconds TimeOptimizedSimulation(int num_folds = 10);
    
private:
    // Internal validation methods
    bool CompareFoldResults(const FoldResult& original, const FoldResult& optimized,
                           std::vector<ComparisonResult::FoldDifference>& differences);
    bool CompareFloats(float a, float b, const std::string& field_name = "");
    
    // Test data extraction equivalence
    bool TestDataExtractionRange(int start_row, int end_row);
    
    // Configuration
    TimeSeriesWindow* m_timeSeriesWindow;
    std::unique_ptr<ISimulationModel> m_originalModel;
    std::unique_ptr<ISimulationModel> m_optimizedModel;
    std::unique_ptr<ModelConfigBase> m_modelConfig;
    WalkForwardConfig m_walkForwardConfig;
    
    // Validation settings
    float m_float_tolerance;
    bool m_validate_all_folds;
    int m_max_folds_to_test;
    
    // Internal engines
    std::unique_ptr<SimulationEngine> m_originalEngine;
    std::unique_ptr<OptimizedSimulationEngine> m_optimizedEngine;
};

// Utility functions for validation
namespace utils {
    // Compare two float vectors with tolerance
    bool CompareFloatVectors(const std::vector<float>& a, const std::vector<float>& b, 
                            float tolerance = 1e-6f);
    
    // Calculate memory usage estimate
    double EstimateMemoryUsageMB(const PreExtractedData& data);
    
    // Generate test configuration for validation
    std::unique_ptr<ModelConfigBase> CreateValidationConfig(TimeSeriesWindow* tsWindow);
    
    // Print detailed comparison report
    void PrintComparisonReport(const ComparisonResult& result);
}

} // namespace validation
} // namespace simulation
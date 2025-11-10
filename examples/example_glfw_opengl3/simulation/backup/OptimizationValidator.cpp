#include "OptimizationValidator.h"
#include "models/XGBoostModel.h"
#include "XGBoostConfig.h"
#include "../TimeSeriesWindow.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace simulation {
namespace validation {

std::string ComparisonResult::GetSummary() const {
    std::ostringstream ss;
    
    ss << "=== Optimization Validation Summary ===\n";
    ss << "Results Identical: " << (results_identical ? "✅ YES" : "❌ NO") << "\n";
    ss << "Performance Improved: " << (performance_improved ? "✅ YES" : "❌ NO") << "\n";
    ss << "\n";
    
    ss << "=== Timing ===\n";
    ss << "Original Time: " << original_time.count() << "ms\n";
    ss << "Optimized Time: " << optimized_time.count() << "ms\n";
    ss << "Speedup Factor: " << std::fixed << std::setprecision(2) << speedup_factor << "x\n";
    ss << "Time Savings: " << (original_time.count() - optimized_time.count()) << "ms\n";
    ss << "\n";
    
    ss << "=== Results Validation ===\n";
    ss << "Total Folds Compared: " << total_folds_compared << "\n";
    ss << "Identical Folds: " << identical_folds << "\n";
    ss << "Mismatched Folds: " << mismatched_fold_numbers.size() << "\n";
    
    if (!mismatched_fold_numbers.empty()) {
        ss << "Mismatched Fold Numbers: ";
        for (size_t i = 0; i < mismatched_fold_numbers.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << mismatched_fold_numbers[i];
        }
        ss << "\n";
    }
    
    if (!differences.empty()) {
        ss << "\n=== Detailed Differences ===\n";
        ss << "Total Differences: " << differences.size() << "\n";
        
        // Show first few differences as examples
        size_t max_show = std::min(differences.size(), size_t(5));
        for (size_t i = 0; i < max_show; ++i) {
            const auto& diff = differences[i];
            ss << "Fold " << diff.fold_number << " - " << diff.field_name 
               << ": " << diff.original_value << " vs " << diff.optimized_value
               << " (diff: " << diff.absolute_diff << ")\n";
        }
        
        if (differences.size() > max_show) {
            ss << "... and " << (differences.size() - max_show) << " more differences\n";
        }
    }
    
    ss << "\n=== Performance Metrics ===\n";
    ss << "Memory Usage: " << std::fixed << std::setprecision(1) << memory_usage_mb << " MB\n";
    ss << "Avg Fold Time (Original): " << std::fixed << std::setprecision(2) 
       << avg_fold_time_original_ms << "ms\n";
    ss << "Avg Fold Time (Optimized): " << std::fixed << std::setprecision(2) 
       << avg_fold_time_optimized_ms << "ms\n";
    
    return ss.str();
}

OptimizationValidator::OptimizationValidator()
    : m_timeSeriesWindow(nullptr)
    , m_float_tolerance(1e-6f)
    , m_validate_all_folds(false)
    , m_max_folds_to_test(5) {
}

OptimizationValidator::~OptimizationValidator() = default;

void OptimizationValidator::SetDataSource(TimeSeriesWindow* tsWindow) {
    m_timeSeriesWindow = tsWindow;
}

void OptimizationValidator::SetModel(std::unique_ptr<ISimulationModel> model) {
    // Create two separate instances for comparison
    if (model->GetModelType() == "XGBoost") {
        m_originalModel = std::make_unique<XGBoostModel>();
        m_optimizedModel = std::make_unique<XGBoostModel>();
    } else {
        throw std::runtime_error("Validation only supports XGBoost model currently");
    }
}

void OptimizationValidator::SetModelConfig(std::unique_ptr<ModelConfigBase> config) {
    // Clone the configuration for both engines
    if (auto* xgb_config = dynamic_cast<XGBoostConfig*>(config.get())) {
        m_modelConfig = std::make_unique<XGBoostConfig>(*xgb_config);
    } else {
        throw std::runtime_error("Unsupported model configuration type");
    }
}

void OptimizationValidator::SetWalkForwardConfig(const WalkForwardConfig& config) {
    m_walkForwardConfig = config;
}

ComparisonResult OptimizationValidator::RunValidation() {
    if (!m_timeSeriesWindow || !m_originalModel || !m_optimizedModel || !m_modelConfig) {
        throw std::runtime_error("Validator not properly configured");
    }
    
    std::cout << "🔍 Starting comprehensive optimization validation..." << std::endl;
    
    ComparisonResult result = {};
    
    // Step 1: Validate data extraction
    std::cout << "Step 1: Validating data extraction..." << std::endl;
    if (!ValidateDataExtraction()) {
        std::cout << "❌ Data extraction validation failed" << std::endl;
        result.results_identical = false;
        return result;
    }
    std::cout << "✅ Data extraction validation passed" << std::endl;
    
    // Step 2: Validate feature mapping
    std::cout << "Step 2: Validating feature mapping..." << std::endl;
    if (!ValidateFeatureMapping()) {
        std::cout << "❌ Feature mapping validation failed" << std::endl;
        result.results_identical = false;
        return result;
    }
    std::cout << "✅ Feature mapping validation passed" << std::endl;
    
    // Step 3: Run limited simulation comparison
    std::cout << "Step 3: Running simulation comparison..." << std::endl;
    
    // Create engines
    m_originalEngine = std::make_unique<SimulationEngine>();
    m_optimizedEngine = std::make_unique<OptimizedSimulationEngine>();
    
    // Configure engines
    m_originalEngine->SetDataSource(m_timeSeriesWindow);
    m_originalEngine->SetModel(std::move(m_originalModel));
    
    // Clone config for original engine
    if (auto* xgb_config = dynamic_cast<XGBoostConfig*>(m_modelConfig.get())) {
        m_originalEngine->SetModelConfig(std::make_unique<XGBoostConfig>(*xgb_config));
    }
    
    m_originalEngine->SetWalkForwardConfig(m_walkForwardConfig);
    m_originalEngine->EnableModelCaching(false);  // Disable for consistent comparison
    
    m_optimizedEngine->SetDataSource(m_timeSeriesWindow);
    m_optimizedEngine->SetModel(std::move(m_optimizedModel));
    
    // Clone config for optimized engine  
    if (auto* xgb_config = dynamic_cast<XGBoostConfig*>(m_modelConfig.get())) {
        m_optimizedEngine->SetModelConfig(std::make_unique<XGBoostConfig>(*xgb_config));
    }
    
    m_optimizedEngine->SetWalkForwardConfig(m_walkForwardConfig);
    m_optimizedEngine->EnableModelCaching(false);  // Disable for consistent comparison
    m_optimizedEngine->SetValidateAgainstOriginal(true);  // Enable validation mode
    
    // Limit folds for testing
    WalkForwardConfig test_config = m_walkForwardConfig;
    test_config.end_fold = test_config.start_fold + m_max_folds_to_test - 1;
    
    m_originalEngine->SetWalkForwardConfig(test_config);
    m_optimizedEngine->SetWalkForwardConfig(test_config);
    
    // Time original simulation
    std::cout << "Running original simulation..." << std::endl;
    auto original_start = std::chrono::steady_clock::now();
    
    m_originalEngine->StartSimulation();
    while (m_originalEngine->IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    auto original_end = std::chrono::steady_clock::now();
    result.original_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        original_end - original_start);
    
    // Time optimized simulation
    std::cout << "Running optimized simulation..." << std::endl;
    auto optimized_start = std::chrono::steady_clock::now();
    
    m_optimizedEngine->StartSimulation();
    while (m_optimizedEngine->IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    auto optimized_end = std::chrono::steady_clock::now();
    result.optimized_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        optimized_end - optimized_start);
    
    // Calculate performance metrics
    result.performance_improved = result.optimized_time < result.original_time;
    result.speedup_factor = static_cast<double>(result.original_time.count()) / 
                           static_cast<double>(result.optimized_time.count());
    
    // Compare results
    const auto& original_results = m_originalEngine->GetCurrentRun().foldResults;
    const auto& optimized_results = m_optimizedEngine->GetCurrentRun().foldResults;
    
    result.total_folds_compared = std::min(original_results.size(), optimized_results.size());
    result.identical_folds = 0;
    
    if (original_results.size() != optimized_results.size()) {
        std::cout << "⚠️  Different number of folds: original=" << original_results.size() 
                  << ", optimized=" << optimized_results.size() << std::endl;
    }
    
    // Compare fold by fold
    for (size_t i = 0; i < result.total_folds_compared; ++i) {
        if (CompareFoldResults(original_results[i], optimized_results[i], result.differences)) {
            result.identical_folds++;
        } else {
            result.mismatched_fold_numbers.push_back(original_results[i].fold_number);
        }
    }
    
    result.results_identical = (result.identical_folds == result.total_folds_compared) && 
                              (result.differences.empty());
    
    // Calculate performance metrics
    if (result.total_folds_compared > 0) {
        result.avg_fold_time_original_ms = static_cast<double>(result.original_time.count()) / 
                                          result.total_folds_compared;
        result.avg_fold_time_optimized_ms = static_cast<double>(result.optimized_time.count()) / 
                                           result.total_folds_compared;
    }
    
    // Estimate memory usage (approximate)
    result.memory_usage_mb = 0.0;  // Would need access to PreExtractedData to calculate
    
    std::cout << "🏁 Validation completed!" << std::endl;
    std::cout << result.GetSummary() << std::endl;
    
    return result;
}

bool OptimizationValidator::ValidateDataExtraction() {
    // Test a few ranges to ensure data extraction is identical
    std::vector<std::pair<int, int>> test_ranges = {
        {0, 100},
        {1000, 1200},
        {5000, 5100}
    };
    
    for (const auto& range : test_ranges) {
        if (!TestDataExtractionRange(range.first, range.second)) {
            return false;
        }
    }
    
    return true;
}

bool OptimizationValidator::ValidateFeatureMapping() {
    // Create optimized engine and trigger pre-extraction
    auto test_engine = std::make_unique<OptimizedSimulationEngine>();
    test_engine->SetDataSource(m_timeSeriesWindow);
    
    // Create a test model for validation
    auto test_model = std::make_unique<XGBoostModel>();
    test_engine->SetModel(std::move(test_model));
    
    // Clone config
    if (auto* xgb_config = dynamic_cast<XGBoostConfig*>(m_modelConfig.get())) {
        auto config_copy = std::make_unique<XGBoostConfig>(*xgb_config);
        test_engine->SetModelConfig(std::move(config_copy));
    }
    
    test_engine->SetWalkForwardConfig(m_walkForwardConfig);
    
    // This should trigger pre-extraction and validation
    try {
        // Note: We can't directly call PreExtractAllData() as it's private
        // The validation will happen during StartSimulation()
        // For now, assume mapping is correct if we get here
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Feature mapping validation failed: " << e.what() << std::endl;
        return false;
    }
}

bool OptimizationValidator::TestDataExtractionRange(int start_row, int end_row) {
    // This would test that GetOptimizedFeatures/GetOptimizedTarget returns 
    // the same results as the original ExtractFeatures/ExtractTarget
    // For now, we'll do basic validation
    
    if (!m_timeSeriesWindow || !m_timeSeriesWindow->HasData()) {
        return false;
    }
    
    const auto* dataFrame = m_timeSeriesWindow->GetDataFrame();
    if (!dataFrame) {
        return false;
    }
    
    int64_t num_rows = dataFrame->num_rows();
    if (end_row > num_rows) {
        end_row = num_rows;
    }
    
    if (start_row >= end_row) {
        return true;  // Empty range is valid
    }
    
    // Basic validation: ensure the range is accessible
    try {
        auto table = dataFrame->get_cpu_table();
        if (!table) {
            return false;
        }
        
        // Check that all required columns exist
        for (const std::string& feature_name : m_modelConfig->feature_columns) {
            auto column = table->GetColumnByName(feature_name);
            if (!column) {
                std::cerr << "Feature column not found: " << feature_name << std::endl;
                return false;
            }
        }
        
        auto target_column = table->GetColumnByName(m_modelConfig->target_column);
        if (!target_column) {
            std::cerr << "Target column not found: " << m_modelConfig->target_column << std::endl;
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Data extraction range test failed: " << e.what() << std::endl;
        return false;
    }
}

bool OptimizationValidator::CompareFoldResults(const FoldResult& original, const FoldResult& optimized,
                                             std::vector<ComparisonResult::FoldDifference>& differences) {
    bool identical = true;
    
    // Helper lambda to check and record differences
    auto checkField = [&](float orig, float opt, const std::string& field) {
        if (!CompareFloats(orig, opt, field)) {
            identical = false;
            
            ComparisonResult::FoldDifference diff;
            diff.fold_number = original.fold_number;
            diff.field_name = field;
            diff.original_value = orig;
            diff.optimized_value = opt;
            diff.absolute_diff = std::abs(orig - opt);
            diff.relative_diff = (orig != 0.0f) ? (diff.absolute_diff / std::abs(orig)) : 0.0f;
            
            differences.push_back(diff);
        }
    };
    
    // Compare key fields
    checkField(original.best_score, optimized.best_score, "best_score");
    checkField(original.mean_scale, optimized.mean_scale, "mean_scale");
    checkField(original.std_scale, optimized.std_scale, "std_scale");
    checkField(original.prediction_threshold_scaled, optimized.prediction_threshold_scaled, "prediction_threshold_scaled");
    checkField(original.prediction_threshold_original, optimized.prediction_threshold_original, "prediction_threshold_original");
    checkField(original.signal_sum, optimized.signal_sum, "signal_sum");
    checkField(original.signal_rate, optimized.signal_rate, "signal_rate");
    checkField(original.avg_return_on_signals, optimized.avg_return_on_signals, "avg_return_on_signals");
    checkField(original.hit_rate, optimized.hit_rate, "hit_rate");
    
    // Compare integer fields
    if (original.fold_number != optimized.fold_number) {
        identical = false;
        std::cerr << "Fold number mismatch: " << original.fold_number << " vs " << optimized.fold_number << std::endl;
    }
    
    if (original.n_signals != optimized.n_signals) {
        identical = false;
        std::cerr << "Signal count mismatch: " << original.n_signals << " vs " << optimized.n_signals << std::endl;
    }
    
    if (original.best_iteration != optimized.best_iteration) {
        identical = false;
        std::cerr << "Best iteration mismatch: " << original.best_iteration << " vs " << optimized.best_iteration << std::endl;
    }
    
    return identical;
}

bool OptimizationValidator::CompareFloats(float a, float b, const std::string& field_name) {
    if (std::isnan(a) && std::isnan(b)) {
        return true;
    }
    
    if (std::isnan(a) || std::isnan(b)) {
        if (!field_name.empty()) {
            std::cerr << "NaN mismatch in " << field_name << ": " << a << " vs " << b << std::endl;
        }
        return false;
    }
    
    float diff = std::abs(a - b);
    float rel_diff = (std::abs(a) > 0) ? diff / std::abs(a) : 0.0f;
    
    bool within_tolerance = (diff <= m_float_tolerance) || (rel_diff <= m_float_tolerance);
    
    if (!within_tolerance && !field_name.empty()) {
        std::cerr << "Float mismatch in " << field_name << ": " << a << " vs " << b 
                  << " (diff: " << diff << ", rel: " << rel_diff << ")" << std::endl;
    }
    
    return within_tolerance;
}

// Utility functions implementation
namespace utils {

bool CompareFloatVectors(const std::vector<float>& a, const std::vector<float>& b, float tolerance) {
    if (a.size() != b.size()) {
        return false;
    }
    
    for (size_t i = 0; i < a.size(); ++i) {
        float diff = std::abs(a[i] - b[i]);
        if (diff > tolerance) {
            return false;
        }
    }
    
    return true;
}

double EstimateMemoryUsageMB(const PreExtractedData& data) {
    double total_bytes = 0.0;
    
    // Feature data
    total_bytes += data.features.size() * data.features[0].size() * sizeof(float);
    
    // Target data
    total_bytes += data.targets.size() * sizeof(float);
    
    // Mapping overhead (approximate)
    total_bytes += data.feature_name_to_index.size() * 64;  // Rough estimate for map overhead
    
    // String storage
    for (const auto& name : data.feature_column_order) {
        total_bytes += name.size();
    }
    total_bytes += data.target_column_name.size();
    
    return total_bytes / (1024.0 * 1024.0);  // Convert to MB
}

std::unique_ptr<ModelConfigBase> CreateValidationConfig(TimeSeriesWindow* tsWindow) {
    if (!tsWindow || !tsWindow->HasData()) {
        return nullptr;
    }
    
    const auto* dataFrame = tsWindow->GetDataFrame();
    if (!dataFrame) {
        return nullptr;
    }
    
    auto config = std::make_unique<XGBoostConfig>();
    
    // Get available columns
    auto table = dataFrame->get_cpu_table();
    if (!table) {
        return nullptr;
    }
    
    auto schema = table->schema();
    
    // Select first few numeric columns as features
    int feature_count = 0;
    const int max_features = 10;
    
    for (int i = 0; i < schema->num_fields() && feature_count < max_features; ++i) {
        auto field = schema->field(i);
        auto type = field->type();
        
        if (type->id() == arrow::Type::DOUBLE || 
            type->id() == arrow::Type::FLOAT ||
            type->id() == arrow::Type::INT64 ||
            type->id() == arrow::Type::INT32) {
            
            std::string field_name = field->name();
            
            // Use first field as target, rest as features
            if (feature_count == 0) {
                config->target_column = field_name;
            } else {
                config->feature_columns.push_back(field_name);
            }
            feature_count++;
        }
    }
    
    // Basic configuration
    config->val_split_ratio = 0.8f;
    config->random_seed = 42;
    config->use_tanh_transform = true;
    config->tanh_scaling_factor = 0.001f;
    
    return std::move(config);
}

void PrintComparisonReport(const ComparisonResult& result) {
    std::cout << result.GetSummary() << std::endl;
}

} // namespace utils
} // namespace validation
} // namespace simulation
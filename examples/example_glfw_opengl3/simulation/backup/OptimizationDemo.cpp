#include "OptimizationValidator.h"
#include "OptimizedSimulationEngine.h"
#include "SimulationEngine.h"
#include "models/XGBoostModel.h"
#include "XGBoostConfig.h"
#include "../TimeSeriesWindow.h"
#include <iostream>
#include <memory>
#include <chrono>

/**
 * Demonstration program showing the performance optimization of the simulation engine.
 * 
 * This program:
 * 1. Creates test data and configuration
 * 2. Runs both original and optimized simulation engines
 * 3. Validates that results are exactly identical
 * 4. Reports performance improvements
 * 
 * Usage: Run this as a standalone test to verify the optimization works correctly.
 */

namespace simulation {
namespace demo {

class OptimizationDemo {
public:
    OptimizationDemo() = default;
    
    void RunFullDemo(TimeSeriesWindow* tsWindow) {
        if (!tsWindow || !tsWindow->HasData()) {
            std::cerr << "❌ No data available for demonstration" << std::endl;
            return;
        }
        
        std::cout << "🚀 Starting Simulation Engine Optimization Demo" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        
        // Step 1: Create test configuration
        std::cout << "\n📋 Step 1: Creating test configuration..." << std::endl;
        auto config = CreateDemoConfiguration(tsWindow);
        if (!config) {
            std::cerr << "❌ Failed to create demo configuration" << std::endl;
            return;
        }
        
        std::cout << "✅ Configuration created:" << std::endl;
        std::cout << "   Features: " << config->feature_columns.size() << std::endl;
        std::cout << "   Target: " << config->target_column << std::endl;
        
        // Step 2: Run validation
        std::cout << "\n🔍 Step 2: Running comprehensive validation..." << std::endl;
        auto validation_result = RunValidation(tsWindow, std::move(config));
        
        // Step 3: Run performance comparison
        std::cout << "\n⚡ Step 3: Running performance comparison..." << std::endl;
        auto performance_result = RunPerformanceComparison(tsWindow);
        
        // Step 4: Generate report
        std::cout << "\n📊 Step 4: Generating final report..." << std::endl;
        GenerateFinalReport(validation_result, performance_result);
        
        std::cout << "\n🏁 Demo completed successfully!" << std::endl;
    }
    
private:
    struct PerformanceResult {
        std::chrono::milliseconds original_time;
        std::chrono::milliseconds optimized_time;
        double speedup_factor;
        int folds_tested;
        bool optimization_successful;
    };
    
    std::unique_ptr<ModelConfigBase> CreateDemoConfiguration(TimeSeriesWindow* tsWindow) {
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
        
        // Select meaningful financial features if available
        std::vector<std::string> preferred_features = {
            "close", "volume", "high", "low", "open",
            "returns", "volatility", "momentum", "rsi", "macd"
        };
        
        std::vector<std::string> available_features;
        std::string target_column;
        
        // First pass: look for preferred features
        for (const auto& preferred : preferred_features) {
            for (int i = 0; i < schema->num_fields(); ++i) {
                auto field = schema->field(i);
                std::string field_name = field->name();
                
                if (field_name.find(preferred) != std::string::npos) {
                    auto type = field->type();
                    if (type->id() == arrow::Type::DOUBLE || 
                        type->id() == arrow::Type::FLOAT ||
                        type->id() == arrow::Type::INT64 ||
                        type->id() == arrow::Type::INT32) {
                        
                        if (target_column.empty() && preferred == "returns") {
                            target_column = field_name;
                        } else if (std::find(available_features.begin(), available_features.end(), field_name) == available_features.end()) {
                            available_features.push_back(field_name);
                        }
                    }
                }
            }
        }
        
        // Second pass: fill with any numeric columns if we don't have enough
        const int min_features = 5;
        const int max_features = 20;
        
        if (available_features.size() < min_features) {
            for (int i = 0; i < schema->num_fields() && available_features.size() < max_features; ++i) {
                auto field = schema->field(i);
                auto type = field->type();
                std::string field_name = field->name();
                
                if (type->id() == arrow::Type::DOUBLE || 
                    type->id() == arrow::Type::FLOAT ||
                    type->id() == arrow::Type::INT64 ||
                    type->id() == arrow::Type::INT32) {
                    
                    if (target_column.empty()) {
                        target_column = field_name;
                    } else if (std::find(available_features.begin(), available_features.end(), field_name) == available_features.end()) {
                        available_features.push_back(field_name);
                    }
                }
            }
        }
        
        if (target_column.empty() || available_features.empty()) {
            std::cerr << "❌ Could not find suitable columns for demonstration" << std::endl;
            return nullptr;
        }
        
        // Configure the model
        config->target_column = target_column;
        config->feature_columns = available_features;
        
        // XGBoost specific settings optimized for demonstration
        config->num_boost_round = 50;  // Reduced for faster demo
        config->max_depth = 4;
        config->learning_rate = 0.1f;
        config->subsample = 0.8f;
        config->colsample_bytree = 0.8f;
        config->reg_alpha = 0.01f;
        config->reg_lambda = 0.01f;
        config->early_stopping_rounds = 10;
        
        // Data transformation
        config->use_tanh_transform = true;
        config->tanh_scaling_factor = 0.001f;
        config->use_standardization = false;
        config->val_split_ratio = 0.8f;
        config->random_seed = 42;
        config->reuse_previous_model = false;  // Disable for consistent comparison
        
        return std::move(config);
    }
    
    validation::ComparisonResult RunValidation(TimeSeriesWindow* tsWindow, 
                                             std::unique_ptr<ModelConfigBase> config) {
        validation::OptimizationValidator validator;
        
        // Configure validator
        validator.SetDataSource(tsWindow);
        validator.SetModel(std::make_unique<XGBoostModel>());
        validator.SetModelConfig(std::move(config));
        
        // Configure walk-forward for limited testing
        WalkForwardConfig wf_config;
        wf_config.train_size = 1000;
        wf_config.test_size = 100;
        wf_config.train_test_gap = 5;
        wf_config.fold_step = 100;
        wf_config.start_fold = 10;
        wf_config.end_fold = 12;  // Only 3 folds for demo
        wf_config.initial_offset = 2000;
        
        validator.SetWalkForwardConfig(wf_config);
        
        // Set validation parameters
        validator.SetFloatTolerance(1e-6f);
        validator.SetValidateAllFolds(true);
        validator.SetMaxFoldsToTest(3);
        
        // Run validation
        return validator.RunValidation();
    }
    
    PerformanceResult RunPerformanceComparison(TimeSeriesWindow* tsWindow) {
        PerformanceResult result = {};
        
        // Create identical configurations for both engines
        auto original_config = CreateDemoConfiguration(tsWindow);
        auto optimized_config = CreateDemoConfiguration(tsWindow);
        
        if (!original_config || !optimized_config) {
            result.optimization_successful = false;
            return result;
        }
        
        // Configure walk-forward for performance testing
        WalkForwardConfig perf_config;
        perf_config.train_size = 2000;
        perf_config.test_size = 200;
        perf_config.train_test_gap = 10;
        perf_config.fold_step = 200;
        perf_config.start_fold = 20;
        perf_config.end_fold = 29;  // 10 folds for performance test
        perf_config.initial_offset = 5000;
        
        result.folds_tested = 10;
        
        // Test original engine
        std::cout << "Testing original engine performance..." << std::endl;
        auto original_start = std::chrono::steady_clock::now();
        
        {
            SimulationEngine original_engine;
            original_engine.SetDataSource(tsWindow);
            original_engine.SetModel(std::make_unique<XGBoostModel>());
            original_engine.SetModelConfig(std::move(original_config));
            original_engine.SetWalkForwardConfig(perf_config);
            original_engine.EnableModelCaching(false);
            
            original_engine.StartSimulation();
            while (original_engine.IsRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        auto original_end = std::chrono::steady_clock::now();
        result.original_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            original_end - original_start);
        
        // Test optimized engine
        std::cout << "Testing optimized engine performance..." << std::endl;
        auto optimized_start = std::chrono::steady_clock::now();
        
        {
            OptimizedSimulationEngine optimized_engine;
            optimized_engine.SetDataSource(tsWindow);
            optimized_engine.SetModel(std::make_unique<XGBoostModel>());
            optimized_engine.SetModelConfig(std::move(optimized_config));
            optimized_engine.SetWalkForwardConfig(perf_config);
            optimized_engine.EnableModelCaching(false);
            
            optimized_engine.StartSimulation();
            while (optimized_engine.IsRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        auto optimized_end = std::chrono::steady_clock::now();
        result.optimized_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            optimized_end - optimized_start);
        
        // Calculate speedup
        result.speedup_factor = static_cast<double>(result.original_time.count()) / 
                               static_cast<double>(result.optimized_time.count());
        result.optimization_successful = result.optimized_time < result.original_time;
        
        return result;
    }
    
    void GenerateFinalReport(const validation::ComparisonResult& validation,
                           const PerformanceResult& performance) {
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "🎯 OPTIMIZATION DEMONSTRATION FINAL REPORT" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        
        // Validation Results
        std::cout << "\n📊 VALIDATION RESULTS:" << std::endl;
        std::cout << "Results Identical: " << (validation.results_identical ? "✅ YES" : "❌ NO") << std::endl;
        std::cout << "Folds Compared: " << validation.total_folds_compared << std::endl;
        std::cout << "Identical Folds: " << validation.identical_folds << std::endl;
        
        if (!validation.differences.empty()) {
            std::cout << "⚠️  Found " << validation.differences.size() << " differences:" << std::endl;
            for (size_t i = 0; i < std::min(validation.differences.size(), size_t(3)); ++i) {
                const auto& diff = validation.differences[i];
                std::cout << "   Fold " << diff.fold_number << " - " << diff.field_name 
                         << ": " << diff.absolute_diff << " difference" << std::endl;
            }
        }
        
        // Performance Results
        std::cout << "\n⚡ PERFORMANCE RESULTS:" << std::endl;
        std::cout << "Original Time: " << performance.original_time.count() << " ms" << std::endl;
        std::cout << "Optimized Time: " << performance.optimized_time.count() << " ms" << std::endl;
        std::cout << "Speedup Factor: " << std::fixed << std::setprecision(2) 
                  << performance.speedup_factor << "x" << std::endl;
        std::cout << "Time Saved: " << (performance.original_time.count() - performance.optimized_time.count()) 
                  << " ms" << std::endl;
        std::cout << "Folds Tested: " << performance.folds_tested << std::endl;
        
        if (performance.folds_tested > 0) {
            double avg_original = static_cast<double>(performance.original_time.count()) / performance.folds_tested;
            double avg_optimized = static_cast<double>(performance.optimized_time.count()) / performance.folds_tested;
            std::cout << "Avg Time per Fold (Original): " << std::fixed << std::setprecision(1) 
                      << avg_original << " ms" << std::endl;
            std::cout << "Avg Time per Fold (Optimized): " << std::fixed << std::setprecision(1) 
                      << avg_optimized << " ms" << std::endl;
        }
        
        // Overall Assessment
        std::cout << "\n🎯 OVERALL ASSESSMENT:" << std::endl;
        
        if (validation.results_identical && performance.optimization_successful) {
            std::cout << "✅ OPTIMIZATION SUCCESSFUL!" << std::endl;
            std::cout << "   • Results are identical between original and optimized engines" << std::endl;
            std::cout << "   • Performance improved by " << std::fixed << std::setprecision(1) 
                      << ((performance.speedup_factor - 1.0) * 100.0) << "%" << std::endl;
            std::cout << "   • Ready for production use" << std::endl;
        } else if (validation.results_identical && !performance.optimization_successful) {
            std::cout << "⚠️  OPTIMIZATION NEEDS INVESTIGATION" << std::endl;
            std::cout << "   • Results are identical (✅ correctness)" << std::endl;
            std::cout << "   • Performance did not improve (❌ speed)" << std::endl;
            std::cout << "   • May need further optimization or larger dataset for benefits" << std::endl;
        } else if (!validation.results_identical) {
            std::cout << "❌ OPTIMIZATION HAS CORRECTNESS ISSUES" << std::endl;
            std::cout << "   • Results differ between engines" << std::endl;
            std::cout << "   • MUST FIX before production use" << std::endl;
            std::cout << "   • Check feature mapping and data extraction logic" << std::endl;
        }
        
        // Recommendations
        std::cout << "\n💡 RECOMMENDATIONS:" << std::endl;
        
        if (performance.speedup_factor > 2.0) {
            std::cout << "   • Excellent speedup achieved - deploy optimized engine" << std::endl;
        } else if (performance.speedup_factor > 1.2) {
            std::cout << "   • Good speedup achieved - consider deployment for large simulations" << std::endl;
        } else {
            std::cout << "   • Limited speedup - benefits may be more apparent with larger datasets" << std::endl;
        }
        
        if (validation.results_identical) {
            std::cout << "   • Correctness validated - safe to use optimized engine" << std::endl;
        } else {
            std::cout << "   • CRITICAL: Fix correctness issues before deployment" << std::endl;
        }
        
        std::cout << "\n" << std::string(80, '=') << std::endl;
    }
};

} // namespace demo
} // namespace simulation

// Example usage function
void RunOptimizationDemo(TimeSeriesWindow* tsWindow) {
    simulation::demo::OptimizationDemo demo;
    demo.RunFullDemo(tsWindow);
}
// Add timing diagnostics to identify bottlenecks
#include <chrono>
#include <iostream>

class TimingDiagnostic {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    
    struct FoldTiming {
        double data_extraction_ms = 0;
        double transformation_ms = 0;
        double dmatrix_creation_ms = 0;
        double training_ms = 0;
        double prediction_ms = 0;
        double metrics_ms = 0;
        double total_ms = 0;
        int fold_number = 0;
    };
    
    static void PrintFoldTiming(const FoldTiming& t) {
        std::cout << "Fold " << t.fold_number << " timing (ms): "
                  << "Data=" << t.data_extraction_ms
                  << ", Transform=" << t.transformation_ms  
                  << ", DMatrix=" << t.dmatrix_creation_ms
                  << ", Train=" << t.training_ms
                  << ", Predict=" << t.prediction_ms
                  << ", Metrics=" << t.metrics_ms
                  << ", Total=" << t.total_ms << std::endl;
    }
    
    static void AnalyzeTimingVariance(const std::vector<FoldTiming>& timings) {
        if (timings.empty()) return;
        
        // Calculate statistics for each component
        double avg_train = 0, min_train = 1e9, max_train = 0;
        double avg_dmatrix = 0, min_dmatrix = 1e9, max_dmatrix = 0;
        
        for (const auto& t : timings) {
            avg_train += t.training_ms;
            min_train = std::min(min_train, t.training_ms);
            max_train = std::max(max_train, t.training_ms);
            
            avg_dmatrix += t.dmatrix_creation_ms;
            min_dmatrix = std::min(min_dmatrix, t.dmatrix_creation_ms);
            max_dmatrix = std::max(max_dmatrix, t.dmatrix_creation_ms);
        }
        
        avg_train /= timings.size();
        avg_dmatrix /= timings.size();
        
        std::cout << "\n=== Timing Analysis ===" << std::endl;
        std::cout << "Training: avg=" << avg_train 
                  << "ms, min=" << min_train 
                  << "ms, max=" << max_train 
                  << "ms, variance=" << (max_train/min_train) << "x" << std::endl;
        std::cout << "DMatrix: avg=" << avg_dmatrix
                  << "ms, min=" << min_dmatrix
                  << "ms, max=" << max_dmatrix
                  << "ms, variance=" << (max_dmatrix/min_dmatrix) << "x" << std::endl;
    }
};

// Possible causes of inconsistent timing:

// 1. GPU Memory Management Issues
void DiagnoseGPUMemory() {
    // GPU might be running out of memory and swapping
    // Or CUDA context switching overhead
    
    // Try forcing CPU mode for consistent timing:
    // XGBoosterSetParam(booster, "device", "cpu");
}

// 2. System Resource Contention  
void DiagnoseSystemResources() {
    // Other processes competing for CPU/RAM
    // Windows defender scanning files
    // Background Windows updates
    
    // Check with Resource Monitor during slow folds
}

// 3. Data Locality Issues
void DiagnoseDataLocality() {
    // Data might be getting paged out to disk
    // Or cache misses for certain fold ranges
    
    // Try pre-touching all memory:
    volatile float sum = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        sum += data[i];
    }
}

// 4. XGBoost Internal Caching Behavior
void DiagnoseXGBoostCaching() {
    // XGBoost might be doing different things for different folds
    // e.g., rebuilding histogram cache, GPU kernel compilation
    
    // Try disabling all caching:
    // XGBoosterSetParam(booster, "updater", "grow_colmaker,prune");
    // XGBoosterSetParam(booster, "predictor", "cpu_predictor");
}

// 5. Early Stopping Variance
void DiagnoseEarlyStopping() {
    // Some folds might stop at 50 iterations, others at 2000
    // This would cause huge timing differences
    
    // Log best_iteration for each fold to check
}
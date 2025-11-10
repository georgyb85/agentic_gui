#pragma once

#include "Series.hpp"
#include "IndicatorRequest.hpp"
#include "IndicatorResult.hpp"
#include "IndicatorConfig.hpp"
#include <vector>
#include <string>
#include <functional>
#include <thread>
#include <future>

namespace tssb {

/// A task to compute a single indicator
struct IndicatorTask {
    std::string variable_name;
    SingleIndicatorRequest request;
    int definition_index;  // Index in original config file
};

/// Result of executing an indicator task
struct TaskResult {
    std::string variable_name;
    IndicatorResult result;
    int definition_index;
    double computation_time_ms = 0.0;
};

/// Progress callback signature
/// Args: completed_count, total_count, current_indicator_name
using ProgressCallback = std::function<void(int, int, const std::string&)>;

/// Parallel executor for indicator computations
class TaskExecutor {
public:
    /// Constructor
    /// @param num_threads Number of worker threads (0 = auto-detect)
    explicit TaskExecutor(int num_threads = 0);

    /// Destructor
    ~TaskExecutor();

    /// Execute all tasks in parallel
    /// @param series Market data
    /// @param tasks Tasks to execute
    /// @param progress_callback Optional progress notification
    /// @return Results in same order as tasks
    std::vector<TaskResult> execute_parallel(
        const SingleMarketSeries& series,
        const std::vector<IndicatorTask>& tasks,
        ProgressCallback progress_callback = nullptr
    );

    /// Execute tasks sequentially (useful for debugging)
    std::vector<TaskResult> execute_sequential(
        const SingleMarketSeries& series,
        const std::vector<IndicatorTask>& tasks,
        ProgressCallback progress_callback = nullptr
    );

    /// Get number of worker threads
    int get_thread_count() const { return num_threads_; }

    /// Create tasks from indicator definitions
    static std::vector<IndicatorTask> create_tasks_from_definitions(
        const std::vector<IndicatorDefinition>& definitions
    );

private:
    int num_threads_;

    /// Worker function for thread pool
    void worker_thread(
        const SingleMarketSeries& series,
        const std::vector<IndicatorTask>& tasks,
        std::vector<TaskResult>& results,
        std::atomic<int>& next_task_index,
        std::atomic<int>& completed_count,
        int total_count,
        ProgressCallback progress_callback
    );
};

/// High-level API for batch indicator computation
class BatchIndicatorComputer {
public:
    /// Compute all indicators from config file
    /// @param ohlcv_file Path to OHLCV data file
    /// @param config_file Path to indicator config (var.txt format)
    /// @param output_file Path to output CSV file
    /// @param parallel Use parallel execution (default: true)
    /// @param num_threads Number of threads (0 = auto-detect)
    /// @param progress_callback Optional progress notification
    /// @return true if successful
    static bool compute_from_files(
        const std::string& ohlcv_file,
        const std::string& config_file,
        const std::string& output_file,
        bool parallel = true,
        int num_threads = 0,
        ProgressCallback progress_callback = nullptr
    );

    /// Compute indicators from pre-loaded data
    static std::vector<TaskResult> compute_from_series(
        const SingleMarketSeries& series,
        const std::vector<IndicatorDefinition>& definitions,
        bool parallel = true,
        int num_threads = 0,
        ProgressCallback progress_callback = nullptr
    );
};

} // namespace tssb

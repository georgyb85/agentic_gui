#include "TaskExecutor.hpp"
#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <chrono>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <iostream>

namespace tssb {

namespace {

/// Map indicator type string to SingleIndicatorId
std::optional<SingleIndicatorId> parse_indicator_type(const std::string& type_str)
{
    // This is a comprehensive mapping from TSSB indicator names to our enum
    static const std::map<std::string, SingleIndicatorId> type_map = {
        // RSI family
        {"RSI", SingleIndicatorId::RSI},
        {"DETRENDED RSI", SingleIndicatorId::DetrendedRsi},
        {"COND_RSI", SingleIndicatorId::CondRsi},
        {"STOCHASTIC RSI", SingleIndicatorId::StochasticRsi},
        {"STOCHASTIC K", SingleIndicatorId::Stochastic},
        {"STOCHASTIC D", SingleIndicatorId::Stochastic},

        // Trend indicators
        {"LINEAR PER ATR", SingleIndicatorId::LinearTrend},
        {"QUADRATIC PER ATR", SingleIndicatorId::QuadraticTrend},
        {"CUBIC PER ATR", SingleIndicatorId::CubicTrend},

        // Deviations
        {"LINEAR DEVIATION", SingleIndicatorId::LinearDeviation},
        {"QUADRATIC DEVIATION", SingleIndicatorId::QuadraticDeviation},
        {"CUBIC DEVIATION", SingleIndicatorId::CubicDeviation},

        // Moving averages
        {"MA DIFFERENCE", SingleIndicatorId::MovingAverageDifference},
        {"CLOSE MINUS MOVING AVERAGE", SingleIndicatorId::CloseMinusMovingAverage},
        {"MACD", SingleIndicatorId::Macd},
        {"PPO", SingleIndicatorId::Ppo},

        // ADX family
        {"ADX", SingleIndicatorId::Adx},
        {"MIN ADX", SingleIndicatorId::MinAdx},
        {"RESIDUAL MIN ADX", SingleIndicatorId::ResidualMinAdx},
        {"DELTA ADX", SingleIndicatorId::DeltaAdx},

        // Aroon
        {"AROON UP", SingleIndicatorId::AroonUp},
        {"AROON DOWN", SingleIndicatorId::AroonDown},
        {"AROON DIFF", SingleIndicatorId::AroonDiff},
        {"AROON OSCILLATOR", SingleIndicatorId::AroonDiff},

        // Volatility
        {"ATR RATIO", SingleIndicatorId::AtrRatio},
        {"PRICE CHANGE OSCILLATOR", SingleIndicatorId::PriceChangeOscillator},
        {"ABS PRICE CHANGE OSCILLATOR", SingleIndicatorId::PriceChangeOscillator},
        {"PRICE VARIANCE RATIO", SingleIndicatorId::PriceVarianceRatio},
        {"CHANGE VARIANCE RATIO", SingleIndicatorId::ChangeVarianceRatio},
        {"MIN PRICE VARIANCE RATIO", SingleIndicatorId::MinPriceVarianceRatio},
        {"MAX PRICE VARIANCE RATIO", SingleIndicatorId::MaxPriceVarianceRatio},
        {"MIN CHANGE VARIANCE RATIO", SingleIndicatorId::MinChangeVarianceRatio},
        {"MAX CHANGE VARIANCE RATIO", SingleIndicatorId::MaxChangeVarianceRatio},
        {"BOLLINGER WIDTH", SingleIndicatorId::BollingerWidth},
        {"DELTA BOLLINGER WIDTH", SingleIndicatorId::DeltaBollingerWidth},
        {"PRICE SKEWNESS", SingleIndicatorId::PriceSkewness},
        {"PRICE KURTOSIS", SingleIndicatorId::PriceKurtosis},
        {"PRICE MOMENTUM", SingleIndicatorId::PriceMomentum},

        // Volume
        {"VOLUME MOMENTUM", SingleIndicatorId::VolumeMomentum},
        {"ON BALANCE VOLUME", SingleIndicatorId::NormalizedOnBalanceVolume},
        {"DELTA ON BALANCE VOLUME", SingleIndicatorId::DeltaOnBalanceVolume},
        {"POSITIVE VOLUME INDICATOR", SingleIndicatorId::NormalizedPositiveVolumeIndex},
        {"DELTA POSITIVE VOLUME INDICATOR", SingleIndicatorId::DeltaPositiveVolumeIndex},
        {"NEGATIVE VOLUME INDICATOR", SingleIndicatorId::NegativeVolumeIndex},
        {"DELTA NEGATIVE VOLUME INDICATOR", SingleIndicatorId::DeltaNegativeVolumeIndex},
        {"PRICE VOLUME FIT", SingleIndicatorId::PriceVolumeFit},
        {"DELTA PRICE VOLUME FIT", SingleIndicatorId::DeltaPriceVolumeFit},
        {"VOLUME WEIGHTED MA OVER MA", SingleIndicatorId::VolumeWeightedMaRatio},
        {"DIFF VOLUME WEIGHTED MA OVER MA", SingleIndicatorId::DiffVolumeWeightedMaRatio},
        {"REACTIVITY", SingleIndicatorId::Reactivity},
        {"DELTA REACTIVITY", SingleIndicatorId::DeltaReactivity},
        {"MAX REACTIVITY", SingleIndicatorId::MaxReactivity},
        {"INTRADAY INTENSITY", SingleIndicatorId::IntradayIntensity},
        {"DELTA INTRADAY INTENSITY", SingleIndicatorId::DeltaIntradayIntensity},
        {"PRODUCT PRICE VOLUME", SingleIndicatorId::ProductPriceVolume},
        {"SUM PRICE VOLUME", SingleIndicatorId::SumPriceVolume},
        {"DELTA PRODUCT PRICE VOLUME", SingleIndicatorId::DeltaProductPriceVolume},
        {"DELTA SUM PRICE VOLUME", SingleIndicatorId::DeltaSumPriceVolume},

        // Information theory
        {"PRICE ENTROPY", SingleIndicatorId::Entropy},
        {"PRICE MUTUAL INFORMATION", SingleIndicatorId::MutualInformation},
        {"VOLUME MUTUAL INFORMATION", SingleIndicatorId::MutualInformation},

        // FTI
        {"FTI LOWPASS", SingleIndicatorId::FtiLowpass},
        {"FTI MINOR LOWPASS", SingleIndicatorId::FtiMinorLowpass},
        {"FTI MAJOR LOWPASS", SingleIndicatorId::FtiMajorLowpass},
        {"FTI BEST PERIOD", SingleIndicatorId::FtiBestPeriod},
        {"FTI BEST FTI", SingleIndicatorId::FtiBestFti},
        {"FTI FTI", SingleIndicatorId::FtiBestFti},
        {"FTI CRAT", SingleIndicatorId::FtiCrat},
        {"FTI MINOR BEST CRAT", SingleIndicatorId::FtiMinorBestCrat},
        {"FTI LARGEST FTI", SingleIndicatorId::FtiLargest},

        // Wavelets
        {"REAL MORLET", SingleIndicatorId::RealMorlet},
        {"IMAG MORLET", SingleIndicatorId::ImagMorlet},
        {"REAL DIFF MORLET", SingleIndicatorId::RealDiffMorlet},
        {"REAL PRODUCT MORLET", SingleIndicatorId::RealProductMorlet},
        {"DAUB MEAN", SingleIndicatorId::DaubMean},
        {"DAUB MIN", SingleIndicatorId::DaubMin},
        {"DAUB MAX", SingleIndicatorId::DaubMax},
        {"DAUB STD", SingleIndicatorId::DaubStd},
        {"DAUB ENERGY", SingleIndicatorId::DaubEnergy},
        {"DAUB NL ENERGY", SingleIndicatorId::DaubNlEnergy},
        {"DAUB CURVE", SingleIndicatorId::DaubCurve},

        // Targets
        {"HIT OR MISS", SingleIndicatorId::HitOrMiss},
    };

    auto it = type_map.find(type_str);
    if (it != type_map.end()) {
        return it->second;
    }

    return std::nullopt;
}

/// Apply flags to indicator request
void apply_flags(SingleIndicatorRequest& request, const std::map<std::string, std::string>& flags)
{
    for (const auto& [key, value] : flags) {
        if (key == "method") {
            // Store method preference (e.g., for ADX, Volume Momentum)
            // This could be expanded to set a specific field in the request
            // For now, we store it as metadata that implementations can check
        } else if (key == "order") {
            // For indicators with order sensitivity (e.g., Hit or Miss)
            if (value == "down_first") {
                // Could set a flag field if we add it to SingleIndicatorRequest
            }
        } else if (key == "legacy") {
            // Use legacy algorithm variant
        }
        // Additional flags can be handled here
    }
}

} // anonymous namespace

TaskExecutor::TaskExecutor(int num_threads)
    : num_threads_(num_threads)
{
    if (num_threads_ <= 0) {
        num_threads_ = std::max(1u, std::thread::hardware_concurrency());
    }
}

TaskExecutor::~TaskExecutor() = default;

std::vector<TaskResult> TaskExecutor::execute_parallel(
    const SingleMarketSeries& series,
    const std::vector<IndicatorTask>& tasks,
    ProgressCallback progress_callback)
{
    if (tasks.empty()) {
        return {};
    }

    const int total_count = static_cast<int>(tasks.size());
    std::vector<TaskResult> results(tasks.size());
    std::atomic<int> next_task_index{0};
    std::atomic<int> completed_count{0};

    // Launch worker threads
    std::vector<std::thread> workers;
    workers.reserve(num_threads_);

    for (int i = 0; i < num_threads_; ++i) {
        workers.emplace_back([&]() {
            worker_thread(series, tasks, results, next_task_index,
                         completed_count, total_count, progress_callback);
        });
    }

    // Wait for all workers to complete
    for (auto& worker : workers) {
        worker.join();
    }

    return results;
}

std::vector<TaskResult> TaskExecutor::execute_sequential(
    const SingleMarketSeries& series,
    const std::vector<IndicatorTask>& tasks,
    ProgressCallback progress_callback)
{
    std::vector<TaskResult> results;
    results.reserve(tasks.size());

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        const auto& task = tasks[i];

        auto start = std::chrono::high_resolution_clock::now();
        auto indicator_result = compute_single_indicator(series, task.request);
        auto end = std::chrono::high_resolution_clock::now();

        TaskResult result;
        result.variable_name = task.variable_name;
        result.result = std::move(indicator_result);
        result.definition_index = task.definition_index;
        result.computation_time_ms =
            std::chrono::duration<double, std::milli>(end - start).count();

        results.push_back(std::move(result));

        if (progress_callback) {
            progress_callback(static_cast<int>(i + 1),
                            static_cast<int>(tasks.size()),
                            task.variable_name);
        }
    }

    return results;
}

void TaskExecutor::worker_thread(
    const SingleMarketSeries& series,
    const std::vector<IndicatorTask>& tasks,
    std::vector<TaskResult>& results,
    std::atomic<int>& next_task_index,
    std::atomic<int>& completed_count,
    int total_count,
    ProgressCallback progress_callback)
{
    while (true) {
        // Get next task
        int task_idx = next_task_index.fetch_add(1);
        if (task_idx >= static_cast<int>(tasks.size())) {
            break;
        }

        const auto& task = tasks[task_idx];

        // Compute indicator
        auto start = std::chrono::high_resolution_clock::now();
        auto indicator_result = compute_single_indicator(series, task.request);
        auto end = std::chrono::high_resolution_clock::now();

        // Store result
        TaskResult result;
        result.variable_name = task.variable_name;
        result.result = std::move(indicator_result);
        result.definition_index = task.definition_index;
        result.computation_time_ms =
            std::chrono::duration<double, std::milli>(end - start).count();

        results[task_idx] = std::move(result);

        // Update progress
        int completed = completed_count.fetch_add(1) + 1;
        if (progress_callback) {
            progress_callback(completed, total_count, task.variable_name);
        }
    }
}

std::vector<IndicatorTask> TaskExecutor::create_tasks_from_definitions(
    const std::vector<IndicatorDefinition>& definitions)
{
    std::vector<IndicatorTask> tasks;
    tasks.reserve(definitions.size());

    for (std::size_t i = 0; i < definitions.size(); ++i) {
        const auto& def = definitions[i];

        auto indicator_id = parse_indicator_type(def.indicator_type);
        if (!indicator_id.has_value()) {
            // Unknown indicator type - skip or log error
            std::cerr << "Warning: Unknown indicator type '" << def.indicator_type
                     << "' for variable " << def.variable_name << "\n";
            continue;
        }

        IndicatorTask task;
        task.variable_name = def.variable_name;
        task.definition_index = static_cast<int>(i);

        task.request.id = indicator_id.value();
        task.request.name = def.variable_name;

        // Copy parameters
        for (std::size_t p = 0; p < def.params.size() && p < 8; ++p) {
            task.request.params[p] = def.params[p];
        }

        // Apply flags
        apply_flags(task.request, def.flags);

        tasks.push_back(std::move(task));
    }

    return tasks;
}

bool BatchIndicatorComputer::compute_from_files(
    const std::string& ohlcv_file,
    const std::string& config_file,
    const std::string& output_file,
    bool parallel,
    int num_threads,
    ProgressCallback progress_callback)
{
    // Parse config
    auto config = IndicatorConfigParser::parse_file(config_file);
    if (!config.success || config.definitions.empty()) {
        std::cerr << "Error parsing config file: " << config.error_message << "\n";
        return false;
    }

    std::cout << "Parsed " << config.parsed_indicators << " indicators from "
              << config_file << "\n";

    // Load OHLCV data
    auto ohlcv_bars = validation::OHLCVParser::parse_file(ohlcv_file);
    if (ohlcv_bars.empty()) {
        std::cerr << "Error: No data loaded from " << ohlcv_file << "\n";
        return false;
    }

    std::cout << "Loaded " << ohlcv_bars.size() << " bars from " << ohlcv_file << "\n";

    auto series = validation::OHLCVParser::to_series(ohlcv_bars);

    // Compute indicators
    auto results = compute_from_series(series, config.definitions, parallel,
                                      num_threads, progress_callback);

    // Extract dates and times
    std::vector<std::string> dates, times;
    for (const auto& bar : ohlcv_bars) {
        dates.push_back(bar.date);
        times.push_back(bar.time);
    }

    // Prepare output data
    std::vector<std::string> variable_names;
    std::vector<std::vector<double>> output_data;

    for (const auto& result : results) {
        variable_names.push_back(result.variable_name);
        output_data.push_back(result.result.values);
    }

    // Write output
    bool success = IndicatorResultWriter::write_csv(
        output_file, variable_names, output_data, dates, times);

    if (success) {
        std::cout << "Results written to " << output_file << "\n";
    } else {
        std::cerr << "Error writing output to " << output_file << "\n";
    }

    return success;
}

std::vector<TaskResult> BatchIndicatorComputer::compute_from_series(
    const SingleMarketSeries& series,
    const std::vector<IndicatorDefinition>& definitions,
    bool parallel,
    int num_threads,
    ProgressCallback progress_callback)
{
    // Create tasks
    auto tasks = TaskExecutor::create_tasks_from_definitions(definitions);

    if (tasks.empty()) {
        return {};
    }

    // Execute
    TaskExecutor executor(num_threads);

    if (parallel) {
        return executor.execute_parallel(series, tasks, progress_callback);
    } else {
        return executor.execute_sequential(series, tasks, progress_callback);
    }
}

} // namespace tssb

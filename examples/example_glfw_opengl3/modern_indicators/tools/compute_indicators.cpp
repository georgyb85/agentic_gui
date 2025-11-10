/// Command-line tool for batch indicator computation
///
/// Usage:
///   compute_indicators <ohlcv_file> <config_file> <output_file> [options]
///
/// Options:
///   --sequential         Run sequentially instead of parallel
///   --threads <N>        Number of threads (default: auto-detect)
///   --quiet              Suppress progress output
///
/// Example:
///   compute_indicators ../../btc25_3.txt ../../var.txt output.csv
///   compute_indicators data.txt config.txt out.csv --threads 4

#include "TaskExecutor.hpp"
#include "IndicatorConfig.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <mutex>

using namespace tssb;

namespace {

std::mutex progress_mutex;

void print_progress(int completed, int total, const std::string& current_name)
{
    std::lock_guard<std::mutex> lock(progress_mutex);

    int percent = (completed * 100) / total;
    int bar_width = 50;
    int filled = (completed * bar_width) / total;

    std::cout << "\r[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) {
            std::cout << "=";
        } else if (i == filled) {
            std::cout << ">";
        } else {
            std::cout << " ";
        }
    }
    std::cout << "] " << std::setw(3) << percent << "% ("
              << completed << "/" << total << ") "
              << current_name << "    ";
    std::cout.flush();
}

void print_usage(const char* program_name)
{
    std::cout << "Usage: " << program_name
              << " <ohlcv_file> <config_file> <output_file> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --sequential      Run sequentially instead of parallel\n";
    std::cout << "  --threads <N>     Number of threads (default: auto-detect)\n";
    std::cout << "  --quiet           Suppress progress output\n";
    std::cout << "  --help            Show this help message\n\n";
    std::cout << "Config file format (extended var.txt):\n";
    std::cout << "  VARIABLE_NAME: INDICATOR_TYPE param1 param2 ...\n";
    std::cout << "  VARIABLE_NAME: INDICATOR_TYPE param1 param2 --flag=value\n\n";
    std::cout << "Examples:\n";
    std::cout << "  RSI_S: RSI 10\n";
    std::cout << "  TREND_S100: LINEAR PER ATR 10 100\n";
    std::cout << "  ATR_RATIO_S: ATR RATIO 10 2.5\n";
    std::cout << "  VOL_MOM_S: VOLUME MOMENTUM 10 5 --order=down_first\n";
}

} // anonymous namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    std::string ohlcv_file = argv[1];
    std::string config_file = argv[2];
    std::string output_file = argv[3];

    bool parallel = true;
    int num_threads = 0;
    bool quiet = false;

    // Parse options
    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--sequential") {
            parallel = false;
        } else if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::atoi(argv[++i]);
        } else if (arg == "--quiet" || arg == "-q") {
            quiet = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    std::cout << "Modern Indicators Batch Computer\n";
    std::cout << "=================================\n\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    // Set up progress callback
    ProgressCallback progress_callback = nullptr;
    if (!quiet) {
        progress_callback = print_progress;
    }

    // Execute batch computation
    bool success = BatchIndicatorComputer::compute_from_files(
        ohlcv_file,
        config_file,
        output_file,
        parallel,
        num_threads,
        progress_callback
    );

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end_time - start_time).count();

    if (!quiet) {
        std::cout << "\n";  // New line after progress bar
    }

    if (success) {
        std::cout << "\nCompleted successfully in " << std::fixed
                  << std::setprecision(2) << duration << " seconds\n";

        if (parallel && num_threads > 0) {
            std::cout << "Used " << num_threads << " threads\n";
        } else if (parallel) {
            std::cout << "Used auto-detected thread count\n";
        } else {
            std::cout << "Ran sequentially\n";
        }

        return 0;
    } else {
        std::cerr << "\nFailed to complete computation\n";
        return 1;
    }
}

#pragma once

#include "imgui.h"
#include "implot.h"
#include "TradeSimulator.h"
#include "candlestick_chart.h"
#include <memory>
#include <chrono>
#include <string>

// Forward declaration
namespace simulation {
    class SimulationWindow;
}
class TimeSeriesWindow;

class TradeSimulationWindow {
public:
    TradeSimulationWindow();
    ~TradeSimulationWindow() = default;
    
    // Window management
    void Draw();
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }
    
    // Set data sources
    void SetCandlestickChart(CandlestickChart* chart) { 
        m_candlestick_chart = chart;
        m_simulator.SetCandlestickChart(chart);
    }
    
    void SetSimulationWindow(simulation::SimulationWindow* window);
    void SetTimeSeriesWindow(TimeSeriesWindow* window) { m_time_series_window = window; }

    
private:
    // UI sections
    void DrawConfiguration();
    void DrawExecutionControls();
    void DrawResults();
    void DrawTradeList();
    void DrawPnLChart();
    void DrawDrawdownChart();
    void DrawPerformanceReport();
    void SaveSimulation();
    bool PasteTradeConfigFromClipboard(std::string* statusMessage);
    void SetClipboardStatus(const std::string& message, bool success);
    
    // Run simulation
    void RunTradeSimulation();
    void RecomputeStressReports();
    
    // Trade simulator
    TradeSimulator m_simulator;
    TradeSimulator::Config m_config;
    
    // Data sources
    CandlestickChart* m_candlestick_chart = nullptr;
    simulation::SimulationWindow* m_simulation_window = nullptr;
    TimeSeriesWindow* m_time_series_window = nullptr;
    
    // UI state
    bool m_visible = false;
    bool m_has_results = false;
    int m_selected_run_index = -1;
    
    // Display options
    bool m_show_trade_list = true;
    bool m_show_pnl_chart = true;
    bool m_show_per_fold_stats = false;
    bool m_show_performance_report = true;
    bool m_enable_stress_tests = true;
    int m_bootstrap_iterations = 1000;
    int m_mcpt_iterations = 1000;
    unsigned int m_stress_seed = 123456789u;
    
    // Trade filter
    enum class TradeFilter {
        All,
        LongOnly,
        ShortOnly
    };
    TradeFilter m_trade_filter = TradeFilter::All;
    simulation::StressTestReport m_cached_stress_all{};
    simulation::StressTestReport m_cached_stress_long{};
    simulation::StressTestReport m_cached_stress_short{};
    bool m_stress_cache_valid = false;
    int m_simulation_counter = 0;
    char m_simulationLabel[64] = {0};
    std::string m_saveStatusMessage;
    std::string m_clipboardStatusMessage;
    bool m_clipboardStatusSuccess = false;
    std::chrono::system_clock::time_point m_lastSimulationStart{};
    std::chrono::system_clock::time_point m_lastSimulationEnd{};
};

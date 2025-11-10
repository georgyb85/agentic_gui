#pragma once

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "candlestick_chart.h"
#include "simulation/SimulationTypes.h"
#include "simulation/PerformanceStressTests.h"

// Trade structure for tracking executed trades
struct ExecutedTrade {
    double entry_timestamp;
    double exit_timestamp;
    float entry_price;
    float exit_price;
    float quantity;
    bool is_long;
    float pnl;
    float return_pct;
    int fold_index;
    float entry_signal;
    float exit_signal;
};

// Position tracking
struct Position {
    bool is_open = false;
    bool is_long = true;
    double entry_timestamp = 0;
    float entry_price = 0;
    float quantity = 0;
    float entry_signal = 0;
    int fold_index = -1;
    float peak_value = 0;  // For tracking drawdown
    int bars_held = 0;     // Track how many bars position has been held
    float atr_stop_loss = 0;  // ATR-based stop loss level (if used)
    float atr_take_profit = 0;  // ATR-based take profit level (if used)
};

class TradeSimulator {
public:
    TradeSimulator();
    ~TradeSimulator() = default;

    // Threshold selection for entries (applies to long and short)
    enum class ThresholdChoice { OptimalROC, Percentile, ZeroCrossover };
    
    // Set data sources
    void SetCandlestickChart(CandlestickChart* chart) { m_candlestick_chart = chart; }
    void SetSimulationResults(const simulation::SimulationRun* results) { m_simulation_results = results; }
    
    // Configuration
    struct Config {
        float position_size = 1000.0f;       // Position size in units
        
        // Signal-based exits
        bool use_signal_exit = true;         // Enable signal-based exits (decay)
        float exit_strength_pct = 0.8f;      // Exit when signal < entry_signal * this
        
        // Signal reversal (independent of signal decay)
        bool honor_signal_reversal = true;   // Close and reverse position on opposite signal
        
        // Stop loss configuration
        bool use_stop_loss = true;           // Enable stop loss
        bool use_atr_stop_loss = false;      // Use ATR-based stop loss instead of fixed %
        float stop_loss_pct = 3.0f;          // Stop loss at X% drawdown from peak
        float atr_multiplier = 2.0f;         // Stop loss at X * ATR from peak
        int atr_period = 14;                 // Period for ATR calculation
        int stop_loss_cooldown_bars = 3;     // Bars to wait after stop loss before re-entry
        
        bool use_take_profit = true;         // Enable take profit
        bool use_atr_take_profit = false;    // Use ATR-based take profit instead of fixed %
        float take_profit_pct = 3.0f;        // Take profit at X% gain
        float atr_tp_multiplier = 3.0f;      // Take profit at X * ATR from entry
        int atr_tp_period = 14;               // Period for ATR calculation (take profit)
        
        bool use_time_exit = false;          // Enable time-based exit
        int max_holding_bars = 10;           // Maximum bars to hold position
        
        // Entry configuration
        bool use_limit_orders = false;       // Use limit orders vs market orders
        int limit_order_window = 5;          // Bars to wait for limit order execution
        float limit_order_offset = 0.001f;   // Offset from current price for limit orders

        // Threshold selection for entries (applies to long and short)
        ThresholdChoice threshold_choice = ThresholdChoice::OptimalROC;
    };
    
    void SetConfig(const Config& config) { m_config = config; }
    
    // Run simulation
    void RunSimulation();
    
    // Get results
    const std::vector<ExecutedTrade>& GetTrades() const { return m_trades; }
    const std::vector<float>& GetCumulativePnL() const { return m_cumulative_pnl; }
    const std::vector<float>& GetBuyHoldPnL() const { return m_buy_hold_pnl; }
    const std::vector<double>& GetBuyHoldTimestamps() const { return m_buy_hold_timestamps; }
    float GetTotalPnL() const { return m_cumulative_pnl.empty() ? 0 : m_cumulative_pnl.back(); }
    float GetWinRate() const;
    float GetSharpeRatio() const;
    
    // Performance metrics
    struct PerformanceReport {
        // Combined (all trades)
        float total_return_pct = 0;
        float profit_factor = 0;
        float sharpe_ratio = 0;
        int total_trades = 0;
        int winning_trades = 0;
        int total_bars_in_position = 0;
        float max_drawdown_pct = 0;
        float avg_drawdown_pct = 0;
        int max_drawdown_duration = 0;
        
        // Long-only metrics
        float long_return_pct = 0;
        float long_profit_factor = 0;
        float long_sharpe_ratio = 0;
        int long_trades = 0;
        int long_winning_trades = 0;
        int long_bars_in_position = 0;
        float long_max_drawdown_pct = 0;
        
        // Short-only metrics
        float short_return_pct = 0;
        float short_profit_factor = 0;
        float short_sharpe_ratio = 0;
        int short_trades = 0;
        int short_winning_trades = 0;
        int short_bars_in_position = 0;
        float short_max_drawdown_pct = 0;
        
        // Buy & hold for comparison (same period as simulation)
        float buy_hold_return_pct = 0;
        float buy_hold_profit_factor = 0;
        float buy_hold_sharpe_ratio = 0;
        float buy_hold_max_drawdown_pct = 0;

        simulation::StressTestReport stress;
    };
    
    PerformanceReport GetPerformanceReport() const;
    
    // Clear results
    void ClearResults();

    const simulation::StressTestConfig& GetStressTestConfig() const { return m_stress_config; }
    float GetPositionSize() const { return m_config.position_size; }

    void SetStressTestConfig(const simulation::StressTestConfig& config) { m_stress_config = config; }
    
private:
    PerformanceReport CalculatePerformanceReport() const;

    // Process signals for a specific fold  
    void ProcessFold(int fold_index, const simulation::FoldResult* fold);
    
    // Time alignment - convert hourly indicator timestamp to minute OHLCV timestamp
    double AlignToMinuteData(double hourly_timestamp) const;
    
    // Trade execution
    void CheckEntrySignal(double timestamp, float prediction, float long_threshold, float short_threshold, int fold_index);
    void CheckExitSignal(double timestamp, float prediction);
    void ClosePosition(double timestamp, float exit_price, float exit_signal, bool is_stop_loss = false);
    
    // Helper functions
    float GetOhlcvPrice(double timestamp, const std::string& price_type) const;
    bool ExecuteLimitOrder(double timestamp, float target_price, bool is_buy, int window);
    void UpdateCumulativePnL();
    double GetNextTimestamp(double current_timestamp) const;
    bool CheckLimitOrderExecution(double timestamp, float target_price, bool is_buy) const;
    float CalculateATR(double timestamp, int period) const;
    
    // Data sources
    CandlestickChart* m_candlestick_chart = nullptr;
    const simulation::SimulationRun* m_simulation_results = nullptr;
    
    // Configuration
    Config m_config;
    
    // Current position
    Position m_current_position;
    
    // Trade management
    double m_last_exit_timestamp = 0;
    bool m_last_exit_was_stop_loss = false;
    int m_cooldown_bars_after_stop = 3;  // Wait N bars after stop loss
    
    // Results
    std::vector<ExecutedTrade> m_trades;
    std::vector<float> m_cumulative_pnl;
    std::vector<float> m_buy_hold_pnl;
    std::vector<double> m_buy_hold_timestamps;  // Timestamps for buy & hold P&L
    
    // Cached data for performance
    std::vector<double> m_ohlcv_timestamps;
    bool m_ohlcv_is_hourly = false;
    
    // Track simulation period for accurate buy & hold comparison
    double m_first_simulation_timestamp = 0;
    double m_last_simulation_timestamp = 0;

    simulation::StressTestConfig m_stress_config;
    mutable std::optional<PerformanceReport> m_cached_report;
};

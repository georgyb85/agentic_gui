#pragma once

#include "SimpleOhlcvWindow.h"
#include <vector>
#include <memory>

// Simple signal data
struct SignalData {
    double timestamp;
    float prediction;
    float long_threshold;
    float short_threshold;
};

// Simple position tracker
struct Position {
    double entry_time;
    float entry_price;
    float entry_signal;
    bool is_long;
    float quantity;
    float peak_value;  // For stop loss tracking
};

// Trade configuration
struct TradeConfig {
    float position_size = 1000.0f;        // Position size in shares/dollars
    float exit_strength_pct = 0.8f;       // Exit when signal < entry * this
    float stop_loss_pct = 3.0f;           // Stop loss percentage
    bool use_limit_orders = false;        // Use limit vs market orders
    float limit_gap_pct = 0.1f;           // Gap for limit orders
};

class SimpleTradeExecutor {
public:
    SimpleTradeExecutor();
    ~SimpleTradeExecutor() = default;
    
    // Set OHLCV data source
    void SetOhlcvData(SimpleOhlcvWindow* ohlcv_window) {
        m_ohlcv_window = ohlcv_window;
    }
    
    // Set signals (predictions with thresholds)
    void SetSignals(const std::vector<SignalData>& signals) {
        m_signals = signals;
    }
    
    // Run simulation
    std::vector<SimpleTrade> ExecuteTrades(const TradeConfig& config);
    
    // Get statistics
    struct Stats {
        int total_trades = 0;
        int winning_trades = 0;
        float total_pnl = 0;
        float win_rate = 0;
        float max_drawdown = 0;
    };
    Stats GetStatistics() const;
    
private:
    // Helper functions
    bool CheckLongSignal(const SignalData& signal) const;
    bool CheckShortSignal(const SignalData& signal) const;
    bool CheckExitSignal(const Position& pos, const SignalData& signal, const TradeConfig& config) const;
    bool CheckStopLoss(const Position& pos, float current_price, const TradeConfig& config) const;
    
    SimpleTrade ClosePosition(const Position& pos, double exit_time, float exit_price);
    
    // Data
    SimpleOhlcvWindow* m_ohlcv_window = nullptr;
    std::vector<SignalData> m_signals;
    std::vector<SimpleTrade> m_trades;
    
    // Position tracking
    std::unique_ptr<Position> m_current_position;
    float m_current_capital = 100000.0f;
};
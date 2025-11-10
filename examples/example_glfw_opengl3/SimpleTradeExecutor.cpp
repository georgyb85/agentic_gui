#include "SimpleTradeExecutor.h"
#include <algorithm>
#include <cmath>

SimpleTradeExecutor::SimpleTradeExecutor() {
}

std::vector<SimpleTrade> SimpleTradeExecutor::ExecuteTrades(const TradeConfig& config) {
    m_trades.clear();
    m_current_position.reset();
    m_current_capital = 100000.0f;
    
    if (!m_ohlcv_window || !m_ohlcv_window->HasData() || m_signals.empty()) {
        return m_trades;
    }
    
    // Process each signal
    for (const auto& signal : m_signals) {
        // Check if we have an open position
        if (m_current_position) {
            // Check exit conditions
            float current_price = m_ohlcv_window->ExecuteBuyOrder(signal.timestamp);  // Get current price
            
            if (current_price > 0) {
                // Update peak for stop loss
                float position_value = m_current_position->is_long ? 
                    (current_price - m_current_position->entry_price) * m_current_position->quantity :
                    (m_current_position->entry_price - current_price) * m_current_position->quantity;
                
                m_current_position->peak_value = std::max(m_current_position->peak_value, position_value);
                
                // Check stop loss
                if (CheckStopLoss(*m_current_position, current_price, config)) {
                    // Close position due to stop loss
                    float exit_price = m_current_position->is_long ?
                        m_ohlcv_window->ExecuteSellOrder(signal.timestamp) :
                        m_ohlcv_window->ExecuteBuyOrder(signal.timestamp);
                    
                    if (exit_price > 0) {
                        SimpleTrade trade = ClosePosition(*m_current_position, signal.timestamp, exit_price);
                        m_trades.push_back(trade);
                        m_current_position.reset();
                    }
                }
                // Check signal-based exit
                else if (CheckExitSignal(*m_current_position, signal, config)) {
                    // Close position due to weak signal
                    float exit_price = m_current_position->is_long ?
                        m_ohlcv_window->ExecuteSellOrder(signal.timestamp) :
                        m_ohlcv_window->ExecuteBuyOrder(signal.timestamp);
                    
                    if (exit_price > 0) {
                        SimpleTrade trade = ClosePosition(*m_current_position, signal.timestamp, exit_price);
                        m_trades.push_back(trade);
                        m_current_position.reset();
                    }
                }
            }
        }
        
        // Check for new entry signals if no position
        if (!m_current_position) {
            if (CheckLongSignal(signal)) {
                // Enter long position
                float entry_price = config.use_limit_orders ?
                    m_ohlcv_window->ExecuteBuyOrder(signal.timestamp, 
                        signal.prediction * (1.0f - config.limit_gap_pct / 100.0f)) :
                    m_ohlcv_window->ExecuteBuyOrder(signal.timestamp);
                
                if (entry_price > 0) {
                    m_current_position = std::make_unique<Position>();
                    m_current_position->entry_time = signal.timestamp;
                    m_current_position->entry_price = entry_price;
                    m_current_position->entry_signal = signal.prediction;
                    m_current_position->is_long = true;
                    m_current_position->quantity = config.position_size / entry_price;
                    m_current_position->peak_value = 0;
                }
            }
            else if (CheckShortSignal(signal)) {
                // Enter short position
                float entry_price = config.use_limit_orders ?
                    m_ohlcv_window->ExecuteSellOrder(signal.timestamp,
                        signal.prediction * (1.0f + config.limit_gap_pct / 100.0f)) :
                    m_ohlcv_window->ExecuteSellOrder(signal.timestamp);
                
                if (entry_price > 0) {
                    m_current_position = std::make_unique<Position>();
                    m_current_position->entry_time = signal.timestamp;
                    m_current_position->entry_price = entry_price;
                    m_current_position->entry_signal = signal.prediction;
                    m_current_position->is_long = false;
                    m_current_position->quantity = config.position_size / entry_price;
                    m_current_position->peak_value = 0;
                }
            }
        }
    }
    
    // Close any remaining position
    if (m_current_position && !m_signals.empty()) {
        float exit_price = m_current_position->is_long ?
            m_ohlcv_window->ExecuteSellOrder(m_signals.back().timestamp) :
            m_ohlcv_window->ExecuteBuyOrder(m_signals.back().timestamp);
        
        if (exit_price > 0) {
            SimpleTrade trade = ClosePosition(*m_current_position, m_signals.back().timestamp, exit_price);
            m_trades.push_back(trade);
            m_current_position.reset();
        }
    }
    
    // Update OHLCV window with trades
    if (m_ohlcv_window) {
        m_ohlcv_window->ClearTrades();
        for (const auto& trade : m_trades) {
            m_ohlcv_window->AddTrade(trade);
        }
    }
    
    return m_trades;
}

bool SimpleTradeExecutor::CheckLongSignal(const SignalData& signal) const {
    return signal.prediction > signal.long_threshold;
}

bool SimpleTradeExecutor::CheckShortSignal(const SignalData& signal) const {
    return signal.prediction < signal.short_threshold;
}

bool SimpleTradeExecutor::CheckExitSignal(const Position& pos, const SignalData& signal, const TradeConfig& config) const {
    float exit_threshold = pos.entry_signal * config.exit_strength_pct;
    
    if (pos.is_long) {
        return signal.prediction < exit_threshold;
    } else {
        return signal.prediction > -exit_threshold;
    }
}

bool SimpleTradeExecutor::CheckStopLoss(const Position& pos, float current_price, const TradeConfig& config) const {
    float position_value = pos.is_long ?
        (current_price - pos.entry_price) * pos.quantity :
        (pos.entry_price - current_price) * pos.quantity;
    
    if (pos.peak_value > 0) {
        float drawdown = (pos.peak_value - position_value) / pos.peak_value;
        return drawdown >= (config.stop_loss_pct / 100.0f);
    }
    
    return false;
}

SimpleTrade SimpleTradeExecutor::ClosePosition(const Position& pos, double exit_time, float exit_price) {
    SimpleTrade trade;
    trade.entry_time = pos.entry_time;
    trade.exit_time = exit_time;
    trade.entry_price = pos.entry_price;
    trade.exit_price = exit_price;
    trade.quantity = pos.quantity;
    trade.is_long = pos.is_long;
    
    if (pos.is_long) {
        trade.pnl = (exit_price - pos.entry_price) * pos.quantity;
        trade.return_pct = 100.0f * (exit_price - pos.entry_price) / pos.entry_price;
    } else {
        trade.pnl = (pos.entry_price - exit_price) * pos.quantity;
        trade.return_pct = 100.0f * (pos.entry_price - exit_price) / pos.entry_price;
    }
    
    m_current_capital += trade.pnl;
    
    return trade;
}

SimpleTradeExecutor::Stats SimpleTradeExecutor::GetStatistics() const {
    Stats stats;
    stats.total_trades = m_trades.size();
    
    for (const auto& trade : m_trades) {
        if (trade.pnl > 0) {
            stats.winning_trades++;
        }
        stats.total_pnl += trade.pnl;
    }
    
    if (stats.total_trades > 0) {
        stats.win_rate = 100.0f * stats.winning_trades / stats.total_trades;
    }
    
    // Calculate max drawdown
    float peak = 0;
    float cumulative = 0;
    for (const auto& trade : m_trades) {
        cumulative += trade.pnl;
        peak = std::max(peak, cumulative);
        float drawdown = peak - cumulative;
        stats.max_drawdown = std::max(stats.max_drawdown, drawdown);
    }
    
    return stats;
}
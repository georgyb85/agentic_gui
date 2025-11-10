#include "TradeSimulator.h"
#include "simulation/SimulationTypes.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <tuple>
#include <numeric>

TradeSimulator::TradeSimulator() {
}

void TradeSimulator::ClearResults() {
    m_trades.clear();
    m_cumulative_pnl.clear();
    m_buy_hold_pnl.clear();
    m_buy_hold_timestamps.clear();
    m_current_position = Position();
    m_last_exit_timestamp = 0;
    m_last_exit_was_stop_loss = false;
    m_cached_report.reset();
}

void TradeSimulator::RunSimulation() {
    if (!m_candlestick_chart || !m_simulation_results) {
        std::cerr << "[TradeSimulator] Missing data sources" << std::endl;
        return;
    }
    
    if (!m_candlestick_chart->HasAnyData()) {
        std::cerr << "[TradeSimulator] No OHLCV data available" << std::endl;
        return;
    }
    
    ClearResults();
    
    // Cache timestamps for faster alignment
    auto& ohlcv_data = const_cast<OhlcvData&>(m_candlestick_chart->GetOhlcvData());
    // Ensure data is processed
    ohlcv_data.processData(false);
    m_ohlcv_timestamps.clear();
    // Convert OhlcvData times to milliseconds
    for (const auto& time : ohlcv_data.getOriginalTimes()) {
        m_ohlcv_timestamps.push_back(time * 1000.0);  // Convert seconds to ms
    }
    
    // Determine OHLCV resolution (minute vs hourly)
    // If we have at least 2 timestamps, check the difference
    m_ohlcv_is_hourly = false;
    if (m_ohlcv_timestamps.size() >= 2) {
        double diff_ms = m_ohlcv_timestamps[1] - m_ohlcv_timestamps[0];
        // If difference is around 1 hour (3600000 ms), data is hourly
        // Allow some tolerance for market gaps
        if (diff_ms >= 3000000) {  // >= 50 minutes suggests hourly data
            m_ohlcv_is_hourly = true;
            std::cout << "[TradeSimulator] Detected HOURLY OHLCV data (bar interval: " 
                      << (diff_ms / 3600000.0) << " hours)" << std::endl;
        } else {
            std::cout << "[TradeSimulator] Detected MINUTE OHLCV data (bar interval: " 
                      << (diff_ms / 60000.0) << " minutes)" << std::endl;
        }
    }
    
    // Process each fold sequentially and track simulation period
    m_first_simulation_timestamp = 0;
    m_last_simulation_timestamp = 0;
    
    for (size_t i = 0; i < m_simulation_results->foldResults.size(); ++i) {
        const auto& fold = m_simulation_results->foldResults[i];

        // Derive the prediction range for this fold (start_idx inclusive, end_idx exclusive)
        size_t start_idx = 0;
        size_t end_idx = 0;

        if (i < m_simulation_results->fold_prediction_offsets.size()) {
            start_idx = m_simulation_results->fold_prediction_offsets[i];
            end_idx = (i + 1 < m_simulation_results->fold_prediction_offsets.size()) ?
                m_simulation_results->fold_prediction_offsets[i + 1] :
                m_simulation_results->all_test_predictions.size();
        } else {
            for (size_t prev = 0; prev < i && prev < m_simulation_results->foldResults.size(); ++prev) {
                start_idx += static_cast<size_t>(m_simulation_results->foldResults[prev].n_test_samples);
            }
            end_idx = start_idx + static_cast<size_t>(fold.n_test_samples);
        }

        // Clamp to available prediction data to guard against empty folds
        const size_t predictions_size = m_simulation_results->all_test_predictions.size();
        start_idx = std::min(start_idx, predictions_size);
        end_idx = std::min(end_idx, predictions_size);
        const bool fold_has_predictions = end_idx > start_idx;

        // Track first timestamp from first fold that has predictions
        if (m_first_simulation_timestamp == 0 && fold_has_predictions) {
            // Try to use actual timestamps first
            if (!m_simulation_results->all_test_timestamps.empty() && 
                start_idx < m_simulation_results->all_test_timestamps.size()) {
                m_first_simulation_timestamp = static_cast<double>(
                    m_simulation_results->all_test_timestamps[start_idx]);
                std::cout << "[TradeSimulator] First simulation timestamp from actual timestamps: "
                          << m_first_simulation_timestamp << " (fold " << i << ")" << std::endl;
            } else {
                // Fallback to old method
                size_t first_row = static_cast<size_t>(std::max(0, fold.test_start));
                size_t ohlcv_index = m_ohlcv_is_hourly ? first_row : (first_row * 60);

                if (ohlcv_index < m_ohlcv_timestamps.size()) {
                    m_first_simulation_timestamp = m_ohlcv_timestamps[ohlcv_index];
                    std::cout << "[TradeSimulator] First simulation timestamp from fold " << i
                              << ", test_start=" << first_row
                              << " (row " << first_row << " = OHLCV index " << ohlcv_index << ")" << std::endl;
                }
            }
        }

        ProcessFold(i, &fold);

        // Track the last timestamp we actually processed
        if (fold_has_predictions) {
            double candidate_timestamp = 0.0;

            if (!m_simulation_results->all_test_timestamps.empty() &&
                end_idx - 1 < m_simulation_results->all_test_timestamps.size()) {
                candidate_timestamp = static_cast<double>(
                    m_simulation_results->all_test_timestamps[end_idx - 1]);
            } else {
                const size_t samples_in_fold = end_idx - start_idx;
                if (samples_in_fold > 0) {
                    size_t last_row = static_cast<size_t>(
                        std::max(0, fold.test_start) + static_cast<int>(samples_in_fold) - 1);
                    size_t ohlcv_index = m_ohlcv_is_hourly ? last_row : (last_row * 60);

                    if (ohlcv_index < m_ohlcv_timestamps.size()) {
                        candidate_timestamp = m_ohlcv_timestamps[ohlcv_index];
                    }
                }
            }

            if (candidate_timestamp > 0.0) {
                m_last_simulation_timestamp = std::max(m_last_simulation_timestamp, candidate_timestamp);
            }
        }
    }
    
    // Close any remaining position at the end of SIMULATION data, not OHLCV data
    if (m_current_position.is_open && m_last_simulation_timestamp > 0) {
        double aligned_timestamp = AlignToMinuteData(m_last_simulation_timestamp);
        float last_price = GetOhlcvPrice(aligned_timestamp, "close");
        ClosePosition(aligned_timestamp, last_price, 0);
    }
    
    UpdateCumulativePnL();
    
    std::cout << "[TradeSimulator] Completed: " << m_trades.size() << " trades, "
              << "Total P&L: " << GetTotalPnL() << ", "
              << "Win Rate: " << GetWinRate() << "%" << std::endl;

    m_cached_report = CalculatePerformanceReport();
}

TradeSimulator::PerformanceReport TradeSimulator::GetPerformanceReport() const {
    if (m_cached_report.has_value()) {
        return *m_cached_report;
    }
    m_cached_report = CalculatePerformanceReport();
    return *m_cached_report;
}

void TradeSimulator::ProcessFold(int fold_index, const simulation::FoldResult* fold) {
    if (!fold) return;
    
    // Debug: Log fold information for high-numbered folds and fold transitions
    if (fold_index >= 500 || (fold_index > 0 && fold_index <= 3)) {
        std::cout << "[TradeSimulator] Processing fold " << fold_index 
                  << ", test_start=" << fold->test_start 
                  << ", test_end=" << fold->test_end 
                  << ", n_test_samples=" << fold->n_test_samples;
        if (m_current_position.is_open) {
            std::cout << " [CARRYING POSITION from fold " << m_current_position.fold_index << "]";
        }
        std::cout << std::endl;
    }
    
    // Get prediction indices for this fold
    size_t start_idx = 0;
    size_t end_idx = 0;
    
    // Use fold_prediction_offsets if available
    if (fold_index < m_simulation_results->fold_prediction_offsets.size()) {
        start_idx = m_simulation_results->fold_prediction_offsets[fold_index];
        
        // End index is the start of next fold or end of all predictions
        if (fold_index + 1 < m_simulation_results->fold_prediction_offsets.size()) {
            end_idx = m_simulation_results->fold_prediction_offsets[fold_index + 1];
        } else {
            end_idx = m_simulation_results->all_test_predictions.size();
        }
    } else {
        // Fallback: calculate based on fold's test samples
        for (int i = 0; i < fold_index; ++i) {
            if (i < m_simulation_results->foldResults.size()) {
                start_idx += m_simulation_results->foldResults[i].n_test_samples;
            }
        }
        end_idx = start_idx + fold->n_test_samples;
    }
    
    if (start_idx >= m_simulation_results->all_test_predictions.size() ||
        end_idx > m_simulation_results->all_test_predictions.size()) {
        std::cerr << "[TradeSimulator] Invalid fold indices for fold " << fold_index 
                  << ": start=" << start_idx << ", end=" << end_idx 
                  << ", predictions_size=" << m_simulation_results->all_test_predictions.size() << std::endl;
        return;
    }
    
    // Get thresholds for this fold based on user configuration (applies to both long and short)
    float long_threshold = 0.0f;
    float short_threshold = 0.0f;

    if (m_config.threshold_choice == TradeSimulator::ThresholdChoice::OptimalROC) {
        // Use ROC-optimized thresholds from walkforward (per fold)
        // Long: prefer explicit long optimal threshold; fallback to per-config original threshold
        if (fold->long_threshold_optimal != 0.0f) {
            long_threshold = fold->long_threshold_optimal;
        } else if (fold->prediction_threshold_original != 0.0f) {
            long_threshold = fold->prediction_threshold_original;
        } else {
            long_threshold = 0.0f; // final fallback
        }

        // Short: prefer optimal short threshold; fallback to original
        if (fold->short_threshold_optimal != 0.0f) {
            short_threshold = fold->short_threshold_optimal;
        } else if (fold->short_threshold_original != 0.0f) {
            short_threshold = fold->short_threshold_original;
        } else {
            short_threshold = fold->short_threshold_5th; // last resort
        }
    } else if (m_config.threshold_choice == TradeSimulator::ThresholdChoice::Percentile) {
        // Percentile mode: use 95th (long) and 5th (short) percentile thresholds from walkforward
        if (fold->long_threshold_95th != 0.0f) {
            long_threshold = fold->long_threshold_95th;
        } else if (fold->prediction_threshold_original != 0.0f) {
            long_threshold = fold->prediction_threshold_original;
        } else {
            long_threshold = 0.0f;
        }

        if (fold->short_threshold_5th != 0.0f) {
            short_threshold = fold->short_threshold_5th;
        } else if (fold->short_threshold_original != 0.0f) {
            short_threshold = fold->short_threshold_original;
        } else {
            short_threshold = 0.0f;
        }
    } else {
        // Zero crossover mode: longs > 0, shorts < 0 (original-scale predictions)
        long_threshold = 0.0f;
        short_threshold = 0.0f;
    }
    
    // If carrying position from previous fold, update fold index but keep bars_held
    if (m_current_position.is_open && m_current_position.fold_index != fold_index) {
        // Dynamic threshold adjustment - make it harder to enter new positions
        long_threshold *= 1.1f;
        short_threshold *= 1.1f;
        
        // Log position carry-over for debugging
        if (fold_index <= 3 || fold_index >= 500) {
            std::cout << "[TradeSimulator] Carrying position from fold " << m_current_position.fold_index 
                      << " to fold " << fold_index 
                      << ", bars_held=" << m_current_position.bars_held << std::endl;
        }
        
        m_current_position.fold_index = fold_index;  // Update fold index but keep bars_held
    }
    
    // Track the previous bar's timestamp to prevent same-bar exit and re-entry
    double last_processed_timestamp = 0;
    
    // Process each prediction in this fold
    for (size_t i = start_idx; i < end_idx; ++i) {
        float prediction = m_simulation_results->all_test_predictions[i];
        
        // Get the actual timestamp for this prediction from the stored timestamps
        double hourly_timestamp;
        if (!m_simulation_results->all_test_timestamps.empty() && 
            i < m_simulation_results->all_test_timestamps.size()) {
            // Use actual timestamp from indicator data
            hourly_timestamp = (double)m_simulation_results->all_test_timestamps[i];
        } else {
            // Fallback to old method if timestamps not available
            size_t absolute_row = fold->test_start + (i - start_idx);
            size_t ohlcv_index = m_ohlcv_is_hourly ? absolute_row : (absolute_row * 60);
            
            // Check if this is within our OHLCV data range
            if (ohlcv_index >= m_ohlcv_timestamps.size()) {
                if (fold_index >= 500 || i == start_idx) {
                    std::cerr << "[TradeSimulator] ERROR: Fold " << fold_index 
                              << " indicator row " << absolute_row 
                              << " maps to OHLCV index " << ohlcv_index
                              << " but OHLCV only has " << m_ohlcv_timestamps.size() << " bars" << std::endl;
                }
                break; // Skip rest of this fold
            }
            
            hourly_timestamp = m_ohlcv_timestamps[ohlcv_index];
        }
        
        // Debug: Log first and last trades of high-numbered folds
        if (fold_index >= 500 && (i == start_idx || i == end_idx - 1)) {
            double aligned_ts = AlignToMinuteData(hourly_timestamp);
            float price = GetOhlcvPrice(aligned_ts, "close");
            std::cout << "[TradeSimulator] Fold " << fold_index 
                      << (i == start_idx ? " FIRST" : " LAST")
                      << " prediction:"
                      << " pred_idx=" << i
                      << ", hourly_ts=" << hourly_timestamp
                      << ", aligned_ts=" << aligned_ts
                      << ", price=" << price
                      << ", using_actual_timestamps=" << (!m_simulation_results->all_test_timestamps.empty())
                      << ", OHLCV range=[" << m_ohlcv_timestamps.front() 
                      << ", " << m_ohlcv_timestamps.back() << "]" << std::endl;
        }
        
        // Align to minute data
        double minute_timestamp = AlignToMinuteData(hourly_timestamp);
        
        // Sanity check: Make sure the timestamp is within OHLCV data range
        if (minute_timestamp < m_ohlcv_timestamps.front() || 
            minute_timestamp > m_ohlcv_timestamps.back()) {
            std::cerr << "[TradeSimulator] Warning: Timestamp out of OHLCV range. "
                      << "Fold " << fold_index << ", prediction " << i 
                      << ", timestamp: " << minute_timestamp 
                      << " (OHLCV range: " << m_ohlcv_timestamps.front() 
                      << " to " << m_ohlcv_timestamps.back() << ")" << std::endl;
            continue;  // Skip this prediction
        }
        
        // IMPORTANT: Check if this is the last bar of the fold
        bool is_last_bar_of_fold = (i == end_idx - 1);
        
        // Check for signal reversal first (if enabled and we have a position)
        // Reversal uses the SAME thresholds (Optimal ROC or Percentile) selected above
        if (m_config.honor_signal_reversal && m_current_position.is_open) {
            bool should_reverse = false;

            if (m_current_position.is_long && prediction < short_threshold) {
                // Holding long but signal says go short
                should_reverse = true;
            } else if (!m_current_position.is_long && prediction > long_threshold) {
                // Holding short but signal says go long
                should_reverse = true;
            }

            if (should_reverse && !is_last_bar_of_fold) {
                // Close current position at next bar's open and attempt opposite entry using thresholds
                double next_timestamp = GetNextTimestamp(minute_timestamp);
                if (next_timestamp > 0) {
                    float exit_price = GetOhlcvPrice(next_timestamp, "open");
                    ClosePosition(minute_timestamp, exit_price, prediction, false);

                    // Attempt entry of opposite direction (entries evaluate and fill at next open)
                    CheckEntrySignal(minute_timestamp, prediction, long_threshold, short_threshold, fold_index);
                }
            }
        }
        
        // Check exit conditions if position is still open
        if (m_current_position.is_open) {
            CheckExitSignal(minute_timestamp, prediction);
        }
        
        // Check entry conditions if no position
        // Don't enter new positions on the last bar of a fold (can't verify exit on next bar)
        if (!m_current_position.is_open && !is_last_bar_of_fold) {
            CheckEntrySignal(minute_timestamp, prediction, long_threshold, short_threshold, fold_index);
        }
        
        last_processed_timestamp = minute_timestamp;
    }
}

double TradeSimulator::AlignToMinuteData(double hourly_timestamp) const {
    // Find the closest OHLCV timestamp to the indicator timestamp
    auto it = std::lower_bound(m_ohlcv_timestamps.begin(), m_ohlcv_timestamps.end(), hourly_timestamp);
    
    if (it == m_ohlcv_timestamps.end()) {
        return m_ohlcv_timestamps.back();
    }
    if (it == m_ohlcv_timestamps.begin()) {
        return m_ohlcv_timestamps.front();
    }
    
    // Return closest timestamp
    double prev = *(it - 1);
    double next = *it;
    return (hourly_timestamp - prev < next - hourly_timestamp) ? prev : next;
}

void TradeSimulator::CheckEntrySignal(double timestamp, float prediction, float long_threshold, float short_threshold, int fold_index) {
    // Check if we're in cooldown period after stop loss (only if stop loss is enabled and cooldown > 0)
    if (m_config.use_stop_loss && m_config.stop_loss_cooldown_bars > 0 && m_last_exit_was_stop_loss && m_last_exit_timestamp > 0) {
        // Make sure we're not on the same bar as the exit
        if (timestamp <= m_last_exit_timestamp) {
            return;  // Same bar as exit, don't re-enter
        }
        
        // Count how many bars have passed since exit
        auto exit_it = std::lower_bound(m_ohlcv_timestamps.begin(), m_ohlcv_timestamps.end(), m_last_exit_timestamp);
        auto current_it = std::lower_bound(m_ohlcv_timestamps.begin(), m_ohlcv_timestamps.end(), timestamp);
        
        if (exit_it != m_ohlcv_timestamps.end() && current_it != m_ohlcv_timestamps.end()) {
            int bars_since_exit = std::distance(exit_it, current_it);
            if (bars_since_exit < m_config.stop_loss_cooldown_bars) {
                return;  // Still in cooldown period
            }
        }
    } else if (m_last_exit_timestamp > 0 && timestamp <= m_last_exit_timestamp) {
        // Always prevent re-entry on the same bar regardless of settings
        return;
    }
    
    // IMPORTANT: When using hourly data, we should use the OPEN price of the NEXT bar
    // to avoid look-ahead bias. The signal is generated at bar close, so we can only
    // trade at the next bar's open.
    float signal_bar_close = GetOhlcvPrice(timestamp, "close");
    if (signal_bar_close < 0) return;  // Invalid price
    
    bool enter_long = prediction > long_threshold;
    bool enter_short = prediction < short_threshold;
    
    if (!enter_long && !enter_short) return;
    
    // Get the NEXT bar's timestamp for entry
    double next_timestamp = GetNextTimestamp(timestamp);
    if (next_timestamp < 0) return;  // No next bar available
    
    float entry_price = GetOhlcvPrice(next_timestamp, "open");  // Enter at next bar's open
    if (entry_price < 0) return;  // Invalid price
    
    // Use limit order if configured
    if (m_config.use_limit_orders) {
        float limit_price = enter_long ? 
            signal_bar_close * (1 - m_config.limit_order_offset) :  // Buy below signal bar close
            signal_bar_close * (1 + m_config.limit_order_offset);   // Sell above signal bar close
            
        // Check if limit order would execute in the NEXT bar only (no look-ahead)
        bool executed = CheckLimitOrderExecution(next_timestamp, limit_price, enter_long);
        if (!executed) return;  // Order not filled
        
        entry_price = limit_price;
    }
    
    // Open position
    m_current_position.is_open = true;
    m_current_position.is_long = enter_long;
    m_current_position.entry_timestamp = timestamp;
    m_current_position.entry_price = entry_price;
    m_current_position.quantity = m_config.position_size / entry_price;
    m_current_position.entry_signal = prediction;
    m_current_position.fold_index = fold_index;
    m_current_position.peak_value = entry_price;
    m_current_position.bars_held = 0;
    
    // Calculate ATR-based stop loss if enabled
    if (m_config.use_stop_loss && m_config.use_atr_stop_loss) {
        float atr = CalculateATR(timestamp, m_config.atr_period);
        if (atr > 0) {
            if (enter_long) {
                m_current_position.atr_stop_loss = entry_price - (atr * m_config.atr_multiplier);
            } else {
                m_current_position.atr_stop_loss = entry_price + (atr * m_config.atr_multiplier);
            }
        } else {
            // Fallback to percentage-based if ATR can't be calculated
            m_current_position.atr_stop_loss = 0;
        }
    } else {
        m_current_position.atr_stop_loss = 0;
    }
    
    // Calculate ATR-based take profit if enabled
    if (m_config.use_take_profit && m_config.use_atr_take_profit) {
        float atr = CalculateATR(timestamp, m_config.atr_tp_period);
        if (atr > 0) {
            if (enter_long) {
                m_current_position.atr_take_profit = entry_price + (atr * m_config.atr_tp_multiplier);
            } else {
                m_current_position.atr_take_profit = entry_price - (atr * m_config.atr_tp_multiplier);
            }
        } else {
            // Fallback to percentage-based if ATR can't be calculated
            m_current_position.atr_take_profit = 0;
        }
    } else {
        m_current_position.atr_take_profit = 0;
    }
}

void TradeSimulator::CheckExitSignal(double timestamp, float prediction) {
    if (!m_current_position.is_open) return;
    
    // Increment bars held
    m_current_position.bars_held++;
    
    // For exit, we check conditions at bar close but execute at next bar's open
    float current_close = GetOhlcvPrice(timestamp, "close");
    float current_high = GetOhlcvPrice(timestamp, "high");
    float current_low = GetOhlcvPrice(timestamp, "low");
    if (current_close < 0) return;
    
    // Update peak for stop loss calculation using high/low (always track even if stop loss disabled)
    if (m_current_position.is_long && current_high > m_current_position.peak_value) {
        m_current_position.peak_value = current_high;
    } else if (!m_current_position.is_long && current_low < m_current_position.peak_value) {
        m_current_position.peak_value = current_low;
    }
    
    bool should_exit = false;
    bool is_stop_loss = false;
    
    // Get next bar for exit execution
    double next_timestamp = GetNextTimestamp(timestamp);
    float exit_price;
    bool has_next_bar = (next_timestamp > 0);
    
    if (!has_next_bar) {
        // No next bar, use current close as fallback
        exit_price = current_close;
    } else {
        // Default exit at next bar's open (no look-ahead)
        exit_price = GetOhlcvPrice(next_timestamp, "open");
    }
    
    // 1. Check take profit (if enabled)
    if (m_config.use_take_profit && !should_exit) {
        if (m_config.use_atr_take_profit && m_current_position.atr_take_profit > 0) {
            // ATR-based take profit
            float tp_check_price = m_current_position.is_long ? current_high : current_low;
            
            bool tp_hit = false;
            if (m_current_position.is_long) {
                tp_hit = (tp_check_price >= m_current_position.atr_take_profit);
            } else {
                tp_hit = (tp_check_price <= m_current_position.atr_take_profit);
            }
            
            if (tp_hit) {
                should_exit = true;
                exit_price = m_current_position.atr_take_profit;
            }
        } else {
            // Percentage-based take profit
            float profit_pct = m_current_position.is_long ?
                (current_high - m_current_position.entry_price) / m_current_position.entry_price * 100 :
                (m_current_position.entry_price - current_low) / m_current_position.entry_price * 100;
                
            if (profit_pct >= m_config.take_profit_pct) {
                should_exit = true;
                // For intrabar exits (stop/take profit hit during bar), use the stop/take profit price
                // This assumes the order was triggered intrabar at the exact level
                float tp_price = m_current_position.is_long ?
                    m_current_position.entry_price * (1 + m_config.take_profit_pct / 100.0f) :
                    m_current_position.entry_price * (1 - m_config.take_profit_pct / 100.0f);
                
                // Use take profit price only if it would have been hit during the bar
                // Otherwise use next bar's open (for signal-based exits)
                exit_price = tp_price;
            }
        }
    }
    
    // 2. Check stop loss (if enabled)
    if (m_config.use_stop_loss && !should_exit) {
        if (m_config.use_atr_stop_loss && m_current_position.atr_stop_loss > 0) {
            // ATR-based stop loss
            float stop_check_price = m_current_position.is_long ? current_low : current_high;
            
            bool stop_hit = false;
            if (m_current_position.is_long) {
                stop_hit = (stop_check_price <= m_current_position.atr_stop_loss);
            } else {
                stop_hit = (stop_check_price >= m_current_position.atr_stop_loss);
            }
            
            if (stop_hit) {
                should_exit = true;
                is_stop_loss = true;
                exit_price = m_current_position.atr_stop_loss;
            }
        } else {
            // Percentage-based stop loss (trailing from peak)
            float stop_check_price = m_current_position.is_long ? current_low : current_high;
            float drawdown_pct = m_current_position.is_long ?
                (m_current_position.peak_value - stop_check_price) / m_current_position.peak_value * 100 :
                (stop_check_price - m_current_position.peak_value) / m_current_position.peak_value * 100;
                
            if (drawdown_pct > m_config.stop_loss_pct) {
                should_exit = true;
                is_stop_loss = true;
                // Stop loss was hit during the bar, exit at stop price
                float sl_price = m_current_position.is_long ?
                    m_current_position.peak_value * (1 - m_config.stop_loss_pct / 100.0f) :
                    m_current_position.peak_value * (1 + m_config.stop_loss_pct / 100.0f);
                
                exit_price = sl_price;
            }
        }
    }
    
    // 3. Check time-based exit (if enabled)
    if (m_config.use_time_exit && !should_exit) {
        if (m_current_position.bars_held >= m_config.max_holding_bars) {
            should_exit = true;
            // Time-based exits happen at next bar's open (already set as default)
        }
    }
    
    // 4. Check signal-based exits (if enabled)
    if (m_config.use_signal_exit && !should_exit) {
        // Check signal strength exit
        float signal_strength = std::abs(prediction) / std::abs(m_current_position.entry_signal);
        if (signal_strength < m_config.exit_strength_pct) {
            should_exit = true;
            // Signal-based exits happen at next bar's open (already set as default)
        }
        
        // Check signal reversal
        if (!should_exit) {
            if (m_current_position.is_long && prediction < 0) {
                should_exit = true;
                // Signal reversal exits happen at next bar's open (already set as default)
            } else if (!m_current_position.is_long && prediction > 0) {
                should_exit = true;
                // Signal reversal exits happen at next bar's open (already set as default)
            }
        }
    }
    
    if (should_exit) {
        ClosePosition(timestamp, exit_price, prediction, is_stop_loss);
    }
}

void TradeSimulator::ClosePosition(double timestamp, float exit_price, float exit_signal, bool is_stop_loss) {
    if (!m_current_position.is_open) return;
    
    // Track stop loss exits
    m_last_exit_timestamp = timestamp;
    m_last_exit_was_stop_loss = is_stop_loss;
    
    ExecutedTrade trade;
    trade.entry_timestamp = m_current_position.entry_timestamp;
    trade.exit_timestamp = timestamp;
    trade.entry_price = m_current_position.entry_price;
    trade.exit_price = exit_price;
    trade.quantity = m_current_position.quantity;
    trade.is_long = m_current_position.is_long;
    trade.fold_index = m_current_position.fold_index;
    trade.entry_signal = m_current_position.entry_signal;
    trade.exit_signal = exit_signal;
    
    // Calculate P&L
    if (trade.is_long) {
        trade.pnl = (exit_price - trade.entry_price) * trade.quantity;
        trade.return_pct = (exit_price - trade.entry_price) / trade.entry_price * 100;
    } else {
        trade.pnl = (trade.entry_price - exit_price) * trade.quantity;
        trade.return_pct = (trade.entry_price - exit_price) / trade.entry_price * 100;
    }
    
    m_trades.push_back(trade);
    m_current_position.is_open = false;
}

float TradeSimulator::GetOhlcvPrice(double timestamp, const std::string& price_type) const {
    if (!m_candlestick_chart) return -1;
    
    auto& ohlcv = const_cast<OhlcvData&>(m_candlestick_chart->GetOhlcvData());
    // Ensure data is processed
    ohlcv.processData(false);
    const auto& times = ohlcv.getOriginalTimes();
    
    // Find closest timestamp (timestamp is in ms, times are in seconds)
    double target_seconds = timestamp / 1000.0;
    auto it = std::lower_bound(times.begin(), times.end(), target_seconds);
    if (it == times.end()) {
        if (times.empty()) return -1;
        it = times.end() - 1;
    }
    
    size_t idx = std::distance(times.begin(), it);
    if (idx >= ohlcv.getOpens().size()) return -1;
    
    if (price_type == "open") {
        return ohlcv.getOpens()[idx];
    } else if (price_type == "high") {
        return ohlcv.getHighs()[idx];
    } else if (price_type == "low") {
        return ohlcv.getLows()[idx];
    } else if (price_type == "close") {
        return ohlcv.getCloses()[idx];
    } else if (price_type == "volume") {
        return ohlcv.getVolumes()[idx];
    }
    
    return ohlcv.getCloses()[idx];  // Default to close
}

bool TradeSimulator::ExecuteLimitOrder(double timestamp, float target_price, bool is_buy, int window) {
    // Check if limit order would be executed within the window
    auto& ohlcv = const_cast<OhlcvData&>(m_candlestick_chart->GetOhlcvData());
    // Ensure data is processed
    ohlcv.processData(false);
    const auto& times = ohlcv.getOriginalTimes();
    
    double target_seconds = timestamp / 1000.0;
    auto it = std::lower_bound(times.begin(), times.end(), target_seconds);
    if (it == times.end()) return false;
    
    size_t start_idx = std::distance(times.begin(), it);
    size_t end_idx = std::min(start_idx + static_cast<size_t>(window), times.size());
    
    const auto& lows = ohlcv.getLows();
    const auto& highs = ohlcv.getHighs();
    
    for (size_t i = start_idx; i < end_idx; ++i) {
        if (is_buy) {
            // Buy limit order executes if price goes below target
            if (lows[i] <= target_price) {
                return true;
            }
        } else {
            // Sell limit order executes if price goes above target
            if (highs[i] >= target_price) {
                return true;
            }
        }
    }
    
    return false;
}

void TradeSimulator::UpdateCumulativePnL() {
    m_cumulative_pnl.clear();
    m_cumulative_pnl.reserve(m_trades.size());
    m_buy_hold_pnl.clear();
    m_buy_hold_timestamps.clear();
    
    if (m_first_simulation_timestamp == 0) {
        return;
    }
    
    // Calculate strategy cumulative P&L
    if (!m_trades.empty()) {
        float cumulative = 0;
        for (const auto& trade : m_trades) {
            cumulative += trade.pnl;
            m_cumulative_pnl.push_back(cumulative);
        }
    }
    
    // Calculate buy & hold P&L at EVERY bar in simulation period (independent of trades)
    double first_aligned = AlignToMinuteData(m_first_simulation_timestamp);
    double last_aligned = AlignToMinuteData(m_last_simulation_timestamp);
    float first_price = GetOhlcvPrice(first_aligned, "open");
    
    if (first_price > 0 && m_last_simulation_timestamp > 0) {
        float shares = m_config.position_size / first_price;
        
        // Get all bars within simulation period
        auto start_it = std::lower_bound(m_ohlcv_timestamps.begin(), 
                                        m_ohlcv_timestamps.end(), first_aligned);
        auto end_it = std::upper_bound(m_ohlcv_timestamps.begin(), 
                                      m_ohlcv_timestamps.end(), last_aligned);
        
        if (start_it != m_ohlcv_timestamps.end()) {
            if (end_it == m_ohlcv_timestamps.end()) {
                // include all remaining bars up to the end
            }
            // Calculate buy & hold P&L at each bar
            m_buy_hold_pnl.reserve(std::distance(start_it, end_it));
            m_buy_hold_timestamps.reserve(std::distance(start_it, end_it));
            
            for (auto it = start_it; it != end_it; ++it) {
                float price = GetOhlcvPrice(*it, "close");
                if (price > 0) {
                    float bh_value = shares * price;
                    float bh_pnl = bh_value - m_config.position_size;
                    m_buy_hold_pnl.push_back(bh_pnl);
                    m_buy_hold_timestamps.push_back(*it);  // Store actual timestamp
                }
            }
        }
    }
}

float TradeSimulator::GetWinRate() const {
    if (m_trades.empty()) return 0;
    
    int winning_trades = 0;
    for (const auto& trade : m_trades) {
        if (trade.pnl > 0) winning_trades++;
    }
    
    return (100.0f * winning_trades) / m_trades.size();
}

float TradeSimulator::CalculateATR(double timestamp, int period) const {
    if (!m_candlestick_chart || period <= 0) return 0;
    
    auto& ohlcv = const_cast<OhlcvData&>(m_candlestick_chart->GetOhlcvData());
    ohlcv.processData(false);
    const auto& times = ohlcv.getOriginalTimes();
    const auto& highs = ohlcv.getHighs();
    const auto& lows = ohlcv.getLows();
    const auto& closes = ohlcv.getCloses();
    
    // Find current bar index
    double target_seconds = timestamp / 1000.0;
    auto it = std::lower_bound(times.begin(), times.end(), target_seconds);
    if (it == times.end() || it == times.begin()) return 0;
    
    size_t current_idx = std::distance(times.begin(), it);
    
    // Need at least period + 1 bars for ATR calculation
    if (current_idx < period) return 0;
    
    float atr_sum = 0;
    int count = 0;
    
    // Calculate True Range for each bar and average
    // IMPORTANT: Exclude current bar to avoid future data leakage
    for (size_t i = current_idx - period; i < current_idx; ++i) {
        if (i >= highs.size() || i >= lows.size() || i >= closes.size()) break;
        
        float true_range = 0;
        
        if (i > 0) {
            // True Range = max of:
            // 1. Current High - Current Low
            // 2. |Current High - Previous Close|
            // 3. |Current Low - Previous Close|
            float hl = highs[i] - lows[i];
            float hc = std::abs(highs[i] - closes[i-1]);
            float lc = std::abs(lows[i] - closes[i-1]);
            
            true_range = std::max({hl, hc, lc});
        } else {
            // For first bar, just use high - low
            true_range = highs[i] - lows[i];
        }
        
        atr_sum += true_range;
        count++;
    }
    
    return (count > 0) ? (atr_sum / count) : 0;
}

float TradeSimulator::GetSharpeRatio() const {
    if (m_trades.size() < 2) return 0;
    
    // Calculate returns
    std::vector<float> returns;
    returns.reserve(m_trades.size());
    for (const auto& trade : m_trades) {
        returns.push_back(trade.return_pct);
    }
    
    // Calculate mean
    float mean = 0;
    for (float r : returns) mean += r;
    mean /= returns.size();
    
    // Calculate std dev
    float variance = 0;
    for (float r : returns) {
        variance += (r - mean) * (r - mean);
    }
    variance /= (returns.size() - 1);
    float std_dev = std::sqrt(variance);
    
    if (std_dev == 0) return 0;
    
    // Annualized Sharpe (assuming ~252 trading days)
    return (mean / std_dev) * std::sqrt(252);
}

double TradeSimulator::GetNextTimestamp(double current_timestamp) const {
    // Find the next timestamp in the data
    auto it = std::upper_bound(m_ohlcv_timestamps.begin(), m_ohlcv_timestamps.end(), current_timestamp);
    if (it != m_ohlcv_timestamps.end()) {
        return *it;
    }
    return -1;  // No next timestamp available
}

bool TradeSimulator::CheckLimitOrderExecution(double timestamp, float target_price, bool is_buy) const {
    // Check if limit order would execute in THIS bar only (no look-ahead)
    float low = GetOhlcvPrice(timestamp, "low");
    float high = GetOhlcvPrice(timestamp, "high");
    
    if (low < 0 || high < 0) return false;
    
    if (is_buy) {
        // Buy limit order executes if low of the bar touches our limit price
        return low <= target_price;
    } else {
        // Sell limit order executes if high of the bar touches our limit price
        return high >= target_price;
    }
}

TradeSimulator::PerformanceReport TradeSimulator::CalculatePerformanceReport() const {
    PerformanceReport report;
    
    if (m_ohlcv_timestamps.empty() || !m_candlestick_chart || 
        m_first_simulation_timestamp == 0 || m_last_simulation_timestamp == 0) {
        return report;
    }
    
    // Calculate buy & hold for SIMULATION PERIOD only
    double first_aligned = AlignToMinuteData(m_first_simulation_timestamp);
    double last_aligned = AlignToMinuteData(m_last_simulation_timestamp);
    float first_price = GetOhlcvPrice(first_aligned, "open");
    float last_price = GetOhlcvPrice(last_aligned, "close");
    
    if (first_price > 0 && last_price > 0) {
        report.buy_hold_return_pct = ((last_price - first_price) / first_price) * 100.0f;
    }
    
    // Separate trades by type
    std::vector<const ExecutedTrade*> long_trades;
    std::vector<const ExecutedTrade*> short_trades;
    
    for (const auto& trade : m_trades) {
        if (trade.is_long) {
            long_trades.push_back(&trade);
        } else {
            short_trades.push_back(&trade);
        }
    }
    
    // Helper lambda to calculate metrics for a set of trades WITH BAR-BY-BAR PROFIT FACTOR
    auto calculate_metrics = [&](const std::vector<const ExecutedTrade*>& trades) -> 
        std::tuple<float, float, float, int, int> {
        
        float total_pnl = 0;
        float gross_profit = 0;
        float gross_loss = 0;
        int winning = 0;
        std::vector<float> returns;
        
        // For profit factor, calculate bar-by-bar P&L when positions are open
        float bar_gross_profit = 0;
        float bar_gross_loss = 0;
        
        for (const auto* trade : trades) {
            total_pnl += trade->pnl;
            if (trade->pnl > 0) {
                winning++;
            }
            returns.push_back(trade->return_pct);
            
            // Calculate bar-by-bar P&L for this trade
            if (trade->entry_timestamp > 0 && trade->exit_timestamp > 0) {
                auto entry_it = std::lower_bound(m_ohlcv_timestamps.begin(), 
                                                m_ohlcv_timestamps.end(), trade->entry_timestamp);
                auto exit_it = std::lower_bound(m_ohlcv_timestamps.begin(), 
                                               m_ohlcv_timestamps.end(), trade->exit_timestamp);
                
                if (entry_it != m_ohlcv_timestamps.end() && exit_it != m_ohlcv_timestamps.end() && 
                    entry_it < exit_it) {
                    
                    float prev_price = trade->entry_price;
                    float position_size = trade->quantity;
                    
                    // Go through each bar while position is held
                    for (auto it = entry_it + 1; it <= exit_it && it != m_ohlcv_timestamps.end(); ++it) {
                        float curr_price = (it == exit_it) ? trade->exit_price : GetOhlcvPrice(*it, "close");
                        
                        if (curr_price > 0 && prev_price > 0) {
                            float bar_pnl;
                            if (trade->is_long) {
                                bar_pnl = (curr_price - prev_price) * position_size;
                            } else {
                                bar_pnl = (prev_price - curr_price) * position_size;
                            }
                            
                            if (bar_pnl > 0) {
                                bar_gross_profit += bar_pnl;
                            } else {
                                bar_gross_loss += std::abs(bar_pnl);
                            }
                            
                            prev_price = curr_price;
                        }
                    }
                }
            }
        }
        
        float return_pct = (m_config.position_size > 0) ? 
            (total_pnl / m_config.position_size) * 100.0f : 0;
        
        // Use bar-by-bar profit factor (like buy & hold)
        float profit_factor = (bar_gross_loss > 0) ? (bar_gross_profit / bar_gross_loss) : 
            (bar_gross_profit > 0 ? 999.99f : 0);
        
        // Calculate Sharpe ratio
        float sharpe = 0;
        if (returns.size() >= 2) {
            float mean = 0;
            for (float r : returns) mean += r;
            mean /= returns.size();
            
            float variance = 0;
            for (float r : returns) {
                variance += (r - mean) * (r - mean);
            }
            variance /= (returns.size() - 1);
            float std_dev = std::sqrt(variance);
            
            if (std_dev > 0) {
                sharpe = (mean / std_dev) * std::sqrt(252); // Annualized
            }
        }
        
        return std::make_tuple(return_pct, profit_factor, sharpe, (int)trades.size(), winning);
    };
    
    // Calculate combined metrics (all trades)
    auto all_trades_ptrs = long_trades;
    all_trades_ptrs.insert(all_trades_ptrs.end(), short_trades.begin(), short_trades.end());
    auto [total_ret, total_pf, total_sharpe, total_count, total_wins] = calculate_metrics(all_trades_ptrs);
    report.total_return_pct = total_ret;
    report.profit_factor = total_pf;
    report.sharpe_ratio = total_sharpe;
    report.total_trades = total_count;
    report.winning_trades = total_wins;
    
    // Calculate long-only metrics
    if (!long_trades.empty()) {
        auto [long_ret, long_pf, long_sharpe, long_count, long_wins] = calculate_metrics(long_trades);
        report.long_return_pct = long_ret;
        report.long_profit_factor = long_pf;
        report.long_sharpe_ratio = long_sharpe;
        report.long_trades = long_count;
        report.long_winning_trades = long_wins;
    }
    
    // Calculate short-only metrics
    if (!short_trades.empty()) {
        auto [short_ret, short_pf, short_sharpe, short_count, short_wins] = calculate_metrics(short_trades);
        report.short_return_pct = short_ret;
        report.short_profit_factor = short_pf;
        report.short_sharpe_ratio = short_sharpe;
        report.short_trades = short_count;
        report.short_winning_trades = short_wins;
    }
    
    // Calculate bars in position (separated by long/short)
    for (const auto& trade : m_trades) {
        if (trade.entry_timestamp > 0 && trade.exit_timestamp > 0) {
            auto entry_it = std::lower_bound(m_ohlcv_timestamps.begin(), 
                                            m_ohlcv_timestamps.end(), trade.entry_timestamp);
            auto exit_it = std::lower_bound(m_ohlcv_timestamps.begin(), 
                                           m_ohlcv_timestamps.end(), trade.exit_timestamp);
            if (entry_it != m_ohlcv_timestamps.end() && exit_it != m_ohlcv_timestamps.end()) {
                int bars_in_trade = std::distance(entry_it, exit_it);
                report.total_bars_in_position += bars_in_trade;
                
                if (trade.is_long) {
                    report.long_bars_in_position += bars_in_trade;
                } else {
                    report.short_bars_in_position += bars_in_trade;
                }
            }
        }
    }
    
    // Calculate drawdown metrics for strategy
    if (!m_cumulative_pnl.empty()) {
        float peak = 0;
        float max_dd = 0;
        float sum_dd = 0;
        int dd_count = 0;
        int current_dd_duration = 0;
        int max_dd_duration = 0;
        
        for (float pnl : m_cumulative_pnl) {
            float equity = m_config.position_size + pnl;
            if (equity > peak) {
                peak = equity;
                current_dd_duration = 0;
            } else {
                current_dd_duration++;
                if (current_dd_duration > max_dd_duration) {
                    max_dd_duration = current_dd_duration;
                }
            }
            
            float dd = (peak > 0) ? ((peak - equity) / peak) * 100.0f : 0;
            if (dd > max_dd) {
                max_dd = dd;
            }
            if (dd > 0) {
                sum_dd += dd;
                dd_count++;
            }
        }
        
        report.max_drawdown_pct = max_dd;
        report.avg_drawdown_pct = (dd_count > 0) ? (sum_dd / dd_count) : 0;
        report.max_drawdown_duration = max_dd_duration;
        
        // Calculate drawdowns for long-only trades
        float long_peak = m_config.position_size;
        float long_max_dd = 0;
        float long_cumul = 0;
        
        for (const auto& trade : m_trades) {
            if (trade.is_long) {
                long_cumul += trade.pnl;
                float long_equity = m_config.position_size + long_cumul;
                if (long_equity > long_peak) {
                    long_peak = long_equity;
                }
                float long_dd = (long_peak > 0) ? ((long_peak - long_equity) / long_peak) * 100.0f : 0;
                if (long_dd > long_max_dd) {
                    long_max_dd = long_dd;
                }
            }
        }
        report.long_max_drawdown_pct = long_max_dd;
        
        // Calculate drawdowns for short-only trades
        float short_peak = m_config.position_size;
        float short_max_dd = 0;
        float short_cumul = 0;
        
        for (const auto& trade : m_trades) {
            if (!trade.is_long) {
                short_cumul += trade.pnl;
                float short_equity = m_config.position_size + short_cumul;
                if (short_equity > short_peak) {
                    short_peak = short_equity;
                }
                float short_dd = (short_peak > 0) ? ((short_peak - short_equity) / short_peak) * 100.0f : 0;
                if (short_dd > short_max_dd) {
                    short_max_dd = short_dd;
                }
            }
        }
        report.short_max_drawdown_pct = short_max_dd;
    }
    
    // Calculate buy & hold metrics for simulation period
    std::vector<float> bh_returns;
    float bh_gross_profit = 0;
    float bh_gross_loss = 0;
    
    if (m_ohlcv_timestamps.size() > 1) {
        // Get all bars within simulation period
        auto start_it = std::lower_bound(m_ohlcv_timestamps.begin(), 
                                        m_ohlcv_timestamps.end(), first_aligned);
        auto end_it = std::lower_bound(m_ohlcv_timestamps.begin(), 
                                      m_ohlcv_timestamps.end(), last_aligned);
        
        if (start_it != m_ohlcv_timestamps.end() && end_it != m_ohlcv_timestamps.end() && 
            start_it < end_it) {
            // Calculate bar-to-bar returns for profit factor (treating each bar as a trade)
            double prev_ts = *start_it;
            float prev_price = GetOhlcvPrice(prev_ts, "close");
            
            // Go through each bar (or sample for performance)
            int step = m_ohlcv_is_hourly ? 1 : 60; // Sample every bar for hourly, every hour for minute data
            for (auto it = start_it + step; it < end_it; it += step) {
                float curr_price = GetOhlcvPrice(*it, "close");
                if (prev_price > 0 && curr_price > 0) {
                    float bar_return = ((curr_price - prev_price) / prev_price) * 100.0f;
                    float bar_pnl = (curr_price - prev_price) * (m_config.position_size / first_price);
                    
                    bh_returns.push_back(bar_return);
                    
                    // Track profit/loss for profit factor
                    if (bar_pnl > 0) {
                        bh_gross_profit += bar_pnl;
                    } else {
                        bh_gross_loss += std::abs(bar_pnl);
                    }
                    
                    prev_price = curr_price;
                }
            }
        }
    }
    
    // Calculate buy & hold profit factor
    report.buy_hold_profit_factor = (bh_gross_loss > 0) ? 
        (bh_gross_profit / bh_gross_loss) : 
        (bh_gross_profit > 0 ? 999.99f : 0);
    
    // Calculate buy & hold Sharpe
    if (bh_returns.size() >= 2) {
        float mean = 0;
        for (float r : bh_returns) mean += r;
        mean /= bh_returns.size();
        
        float variance = 0;
        for (float r : bh_returns) {
            variance += (r - mean) * (r - mean);
        }
        variance /= (bh_returns.size() - 1);
        float std_dev = std::sqrt(variance);
        
        if (std_dev > 0) {
            report.buy_hold_sharpe_ratio = (mean / std_dev) * std::sqrt(252);
        }
    }
    
    // Calculate buy & hold max drawdown
    if (!m_buy_hold_pnl.empty()) {
        float bh_peak = 0;
        float bh_max_dd = 0;
        
        for (float pnl : m_buy_hold_pnl) {
            float bh_equity = m_config.position_size + pnl;
            if (bh_equity > bh_peak) {
                bh_peak = bh_equity;
            }
            float bh_dd = (bh_peak > 0) ? ((bh_peak - bh_equity) / bh_peak) * 100.0f : 0;
            if (bh_dd > bh_max_dd) {
                bh_max_dd = bh_dd;
            }
        }
        report.buy_hold_max_drawdown_pct = bh_max_dd;
    }

    std::vector<double> trade_returns_pct;
    std::vector<double> trade_pnls;
    trade_returns_pct.reserve(m_trades.size());
    trade_pnls.reserve(m_trades.size());
    for (const auto& trade : m_trades) {
        trade_returns_pct.push_back(static_cast<double>(trade.return_pct));
        trade_pnls.push_back(static_cast<double>(trade.pnl));
    }
    report.stress = simulation::RunStressTests(trade_returns_pct, trade_pnls, m_config.position_size, m_stress_config);
    
    return report;
}

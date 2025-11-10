#include "TradeSimulationWindow.h"
#include "simulation/SimulationWindowNew.h"
#include "TimeSeriesWindow.h"
#include "stage1_metadata_writer.h"
#include "QuestDbExports.h"
#include "RunConfigSerializer.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <map>
#include <cstdint>
#include <ctime>
#include <string>
#include <optional>
#include <algorithm>
#include <random>

namespace {

std::string ToSlug(const std::string& value) {
    if (value.empty()) return {};
    std::string slug;
    slug.reserve(value.size());
    bool lastUnderscore = false;
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            lastUnderscore = false;
        } else {
            if (!lastUnderscore) {
                slug.push_back('_');
                lastUnderscore = true;
            }
        }
    }
    while (!slug.empty() && slug.back() == '_') slug.pop_back();
    if (!slug.empty() && slug.front() == '_') slug.erase(slug.begin());
    return slug;
}

std::string SerializeTradeConfig(const TradeSimulator::Config& config) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"position_size\":" << config.position_size << ",";
    oss << "\"use_signal_exit\":" << (config.use_signal_exit ? "true" : "false") << ",";
    oss << "\"exit_strength_pct\":" << config.exit_strength_pct << ",";
    oss << "\"honor_signal_reversal\":" << (config.honor_signal_reversal ? "true" : "false") << ",";
    oss << "\"use_stop_loss\":" << (config.use_stop_loss ? "true" : "false") << ",";
    oss << "\"use_atr_stop_loss\":" << (config.use_atr_stop_loss ? "true" : "false") << ",";
    oss << "\"stop_loss_pct\":" << config.stop_loss_pct << ",";
    oss << "\"atr_multiplier\":" << config.atr_multiplier << ",";
    oss << "\"atr_period\":" << config.atr_period << ",";
    oss << "\"stop_loss_cooldown_bars\":" << config.stop_loss_cooldown_bars << ",";
    oss << "\"use_take_profit\":" << (config.use_take_profit ? "true" : "false") << ",";
    oss << "\"use_atr_take_profit\":" << (config.use_atr_take_profit ? "true" : "false") << ",";
    oss << "\"take_profit_pct\":" << config.take_profit_pct << ",";
    oss << "\"atr_tp_multiplier\":" << config.atr_tp_multiplier << ",";
    oss << "\"atr_tp_period\":" << config.atr_tp_period << ",";
    oss << "\"use_time_exit\":" << (config.use_time_exit ? "true" : "false") << ",";
    oss << "\"max_holding_bars\":" << config.max_holding_bars << ",";
    oss << "\"use_limit_orders\":" << (config.use_limit_orders ? "true" : "false") << ",";
    oss << "\"limit_order_window\":" << config.limit_order_window << ",";
    oss << "\"limit_order_offset\":" << config.limit_order_offset << ",";
    oss << "\"threshold_choice\":" << static_cast<int>(config.threshold_choice);
    oss << "}";
    return oss.str();
}

std::string SerializePerformanceReport(const TradeSimulator::PerformanceReport& report) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"total_return_pct\":" << report.total_return_pct << ",";
    oss << "\"profit_factor\":" << report.profit_factor << ",";
    oss << "\"sharpe_ratio\":" << report.sharpe_ratio << ",";
    oss << "\"total_trades\":" << report.total_trades << ",";
    oss << "\"winning_trades\":" << report.winning_trades << ",";
    oss << "\"max_drawdown_pct\":" << report.max_drawdown_pct << ",";
    oss << "\"long_return_pct\":" << report.long_return_pct << ",";
    oss << "\"long_profit_factor\":" << report.long_profit_factor << ",";
    oss << "\"long_trades\":" << report.long_trades << ",";
    oss << "\"short_return_pct\":" << report.short_return_pct << ",";
    oss << "\"short_profit_factor\":" << report.short_profit_factor << ",";
    oss << "\"short_trades\":" << report.short_trades << ",";
    oss << "\"buy_hold_return_pct\":" << report.buy_hold_return_pct << ",";
    oss << "\"stress\":{";
    oss << "\"computed\":" << (report.stress.computed ? "true" : "false") << ",";
    oss << "\"bootstrap_iterations\":" << report.stress.bootstrap_iterations << ",";
    oss << "\"mcpt_iterations\":" << report.stress.mcpt_iterations << ",";
    oss << "\"sample_size\":" << report.stress.sample_size << ",";
    oss << "\"sharpe_ci\":{";
    oss << "\"estimate\":" << report.stress.sharpe_ci.estimate << ",";
    oss << "\"lower90\":" << report.stress.sharpe_ci.lower_90 << ",";
    oss << "\"upper90\":" << report.stress.sharpe_ci.upper_90 << ",";
    oss << "\"lower95\":" << report.stress.sharpe_ci.lower_95 << ",";
    oss << "\"upper95\":" << report.stress.sharpe_ci.upper_95 << "},";
    oss << "\"profit_factor_ci\":{";
    oss << "\"estimate\":" << report.stress.profit_factor_ci.estimate << ",";
    oss << "\"lower90\":" << report.stress.profit_factor_ci.lower_90 << ",";
    oss << "\"upper90\":" << report.stress.profit_factor_ci.upper_90 << ",";
    oss << "\"lower95\":" << report.stress.profit_factor_ci.lower_95 << ",";
    oss << "\"upper95\":" << report.stress.profit_factor_ci.upper_95 << "},";
    oss << "\"total_return_ci\":{";
    oss << "\"estimate\":" << report.stress.total_return_ci.estimate << ",";
    oss << "\"lower90\":" << report.stress.total_return_ci.lower_90 << ",";
    oss << "\"upper90\":" << report.stress.total_return_ci.upper_90 << ",";
    oss << "\"lower95\":" << report.stress.total_return_ci.lower_95 << ",";
    oss << "\"upper95\":" << report.stress.total_return_ci.upper_95 << "},";
    oss << "\"drawdown_quantiles\":{";
    oss << "\"q50\":" << report.stress.drawdown_quantiles.q50 << ",";
    oss << "\"q90\":" << report.stress.drawdown_quantiles.q90 << ",";
    oss << "\"q95\":" << report.stress.drawdown_quantiles.q95 << ",";
    oss << "\"q99\":" << report.stress.drawdown_quantiles.q99 << "},";
    oss << "\"monte_carlo\":{";
    oss << "\"total_return_pvalue\":" << report.stress.monte_carlo.total_return_pvalue << ",";
    oss << "\"max_drawdown_pvalue\":" << report.stress.monte_carlo.max_drawdown_pvalue << ",";
    oss << "\"sharpe_pvalue\":" << report.stress.monte_carlo.sharpe_pvalue << ",";
    oss << "\"profit_factor_pvalue\":" << report.stress.monte_carlo.profit_factor_pvalue << "}";
    oss << "}";
    oss << "}";
    return oss.str();
}



std::string ThresholdChoiceToString(TradeSimulator::ThresholdChoice choice) {
    switch (choice) {
        case TradeSimulator::ThresholdChoice::OptimalROC:
            return "OptimalROC";
        case TradeSimulator::ThresholdChoice::Percentile:
            return "Percentile95_5";
        case TradeSimulator::ThresholdChoice::ZeroCrossover:
            return "ZeroCrossover";
    }
    return "Unknown";
}

std::string BuildClipboardPayload(const TradeSimulator::Config& config) {
    std::ostringstream oss;
    oss << std::boolalpha;
    oss << "# Trade Simulation Parameters\n";
    oss << "position_size: " << config.position_size << "\n";
    oss << "use_signal_exit: " << config.use_signal_exit << "\n";
    oss << "exit_strength_pct: " << config.exit_strength_pct << "\n";
    oss << "honor_signal_reversal: " << config.honor_signal_reversal << "\n";
    oss << "use_stop_loss: " << config.use_stop_loss << "\n";
    oss << "use_atr_stop_loss: " << config.use_atr_stop_loss << "\n";
    oss << "stop_loss_pct: " << config.stop_loss_pct << "\n";
    oss << "atr_multiplier: " << config.atr_multiplier << "\n";
    oss << "atr_period: " << config.atr_period << "\n";
    oss << "stop_loss_cooldown_bars: " << config.stop_loss_cooldown_bars << "\n";
    oss << "use_take_profit: " << config.use_take_profit << "\n";
    oss << "use_atr_take_profit: " << config.use_atr_take_profit << "\n";
    oss << "take_profit_pct: " << config.take_profit_pct << "\n";
    oss << "atr_tp_multiplier: " << config.atr_tp_multiplier << "\n";
    oss << "atr_tp_period: " << config.atr_tp_period << "\n";
    oss << "use_time_exit: " << config.use_time_exit << "\n";
    oss << "max_holding_bars: " << config.max_holding_bars << "\n";
    oss << "use_limit_orders: " << config.use_limit_orders << "\n";
    oss << "limit_order_window: " << config.limit_order_window << "\n";
    oss << "limit_order_offset: " << config.limit_order_offset << "\n";
    oss << "threshold_choice: " << ThresholdChoiceToString(config.threshold_choice) << "\n";
    return oss.str();
}

std::string FormatTimestamp(double timestamp_ms) {
    if (timestamp_ms <= 0) {
        return "-";
    }

    std::time_t seconds = static_cast<std::time_t>(timestamp_ms / 1000.0);
    std::tm tm_result{};
#if defined(_WIN32)
    if (localtime_s(&tm_result, &seconds) != 0) {
        return "-";
    }
#else
    if (!localtime_r(&seconds, &tm_result)) {
        return "-";
    }
#endif

    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm_result) == 0) {
        return "-";
    }
    return std::string(buffer);
}

} // namespace

TradeSimulationWindow::TradeSimulationWindow() {
    // Initialize default config
    m_config.position_size = 1000.0f;
    
    // Exit methods
    m_config.use_signal_exit = true;
    m_config.exit_strength_pct = 0.8f;
    m_config.honor_signal_reversal = true;
    
    m_config.use_stop_loss = true;
    m_config.use_atr_stop_loss = false;
    m_config.stop_loss_pct = 3.0f;
    m_config.atr_multiplier = 2.0f;
    m_config.atr_period = 14;
    m_config.stop_loss_cooldown_bars = 3;
    
    m_config.use_take_profit = true;
    m_config.take_profit_pct = 3.0f;
    
    m_config.use_time_exit = false;
    m_config.max_holding_bars = 10;
    
    // Entry config
    m_config.use_limit_orders = false;
    m_config.limit_order_window = 5;
    m_config.limit_order_offset = 0.001f;

    std::random_device rd;
    m_stress_seed = rd();
}

void TradeSimulationWindow::SetSimulationWindow(simulation::SimulationWindow* window) {
    m_simulation_window = window;
}

void TradeSimulationWindow::Draw() {
    if (!m_visible) return;
    
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Trade Simulation", &m_visible)) {
        DrawConfiguration();
        ImGui::Separator();
        DrawExecutionControls();
        
        if (m_has_results) {
            ImGui::Separator();
            DrawResults();
            
            if (m_show_performance_report) {
                ImGui::Separator();
                DrawPerformanceReport();
            }
            
            if (m_show_trade_list) {
                ImGui::Separator();
                DrawTradeList();
            }
            
            if (m_show_pnl_chart) {
                ImGui::Separator();
                DrawPnLChart();
                ImGui::Separator();
                DrawDrawdownChart();
            }
        }
    }
    ImGui::End();
}

bool TradeSimulationWindow::PasteTradeConfigFromClipboard(std::string* statusMessage) {
    const char* clipboard = ImGui::GetClipboardText();
    if (!clipboard || clipboard[0] == '\0') {
        if (statusMessage) *statusMessage = "Clipboard is empty.";
        return false;
    }

    RunConfigSerializer::Snapshot snapshot;
    std::string error;
    if (!RunConfigSerializer::Deserialize(clipboard, &snapshot, &error)) {
        if (statusMessage) {
            *statusMessage = error.empty() ? "Clipboard does not contain a valid configuration." : error;
        }
        return false;
    }

    if (!snapshot.hasTradeConfig) {
        if (statusMessage) *statusMessage = "Clipboard payload does not include trade settings.";
        return false;
    }

    m_config = snapshot.trade;
    if (statusMessage) *statusMessage = "Trade parameters pasted from clipboard.";
    return true;
}

void TradeSimulationWindow::SetClipboardStatus(const std::string& message, bool success) {
    m_clipboardStatusMessage = message;
    m_clipboardStatusSuccess = success;
}

void TradeSimulationWindow::DrawConfiguration() {
    if (ImGui::CollapsingHeader("Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputFloat("Position Size", &m_config.position_size, 100.0f, 1000.0f);

        const simulation::SimulationRun* contextRun = nullptr;
        if (m_simulation_window && m_selected_run_index >= 0) {
            contextRun = m_simulation_window->GetRunByIndex(m_selected_run_index);
        }

        bool params_copied = false;
        if (ImGui::Button("Copy Parameters", ImVec2(160, 0))) {
            RunConfigSerializer::Snapshot snapshot;
            snapshot.modelType = "TradeSimulator";
            if (contextRun) {
                snapshot.dataset = contextRun->dataset_measurement;
                snapshot.runName = contextRun->name;
            } else if (m_time_series_window) {
                snapshot.dataset = m_time_series_window->GetSuggestedDatasetId();
            }
            snapshot.hasTradeConfig = true;
            snapshot.trade = m_config;

            std::string clipboardPayload = RunConfigSerializer::Serialize(
                snapshot,
                RunConfigSerializer::SectionMetadata | RunConfigSerializer::SectionTrade);
            ImGui::SetClipboardText(clipboardPayload.c_str());
            params_copied = true;
            SetClipboardStatus("Parameters copied to clipboard.", true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy current trade simulation settings to the clipboard.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Paste Parameters", ImVec2(160, 0))) {
            std::string status;
            const bool pasted = PasteTradeConfigFromClipboard(&status);
            SetClipboardStatus(status, pasted);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Paste trade simulation settings from the clipboard.");
        }
        if (params_copied) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Parameters copied!");
        }
        if (!m_clipboardStatusMessage.empty()) {
            const ImVec4 color = m_clipboardStatusSuccess
                ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", m_clipboardStatusMessage.c_str());
        }
        
        ImGui::Separator();
        ImGui::Text("Exit Methods (each can be enabled/disabled independently):");
        
        // Signal-based exit (decay)
        ImGui::Checkbox("Use Signal Decay Exit", &m_config.use_signal_exit);
        if (m_config.use_signal_exit) {
            ImGui::Indent();
            ImGui::SliderFloat("Exit Signal Strength", &m_config.exit_strength_pct, -1.0f, 1.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Exit when signal strength drops below this ratio of entry signal\n"
                                 "Positive: Exit when signal weakens below threshold\n"
                                 "Negative: Exit when signal reverses beyond threshold");
            }
            ImGui::Unindent();
        }
        
        // Signal reversal (independent control)
        ImGui::Checkbox("Honor Signal Reversal", &m_config.honor_signal_reversal);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When checked: Close current position and open opposite position on signal reversal\n"
                             "When unchecked: Ignore signal reversal and wait for other exit conditions\n"
                             "This is independent of signal decay exit");
        }
        
        // Take profit
        ImGui::Checkbox("Use Take Profit", &m_config.use_take_profit);
        if (m_config.use_take_profit) {
            ImGui::Indent();
            
            // Take profit type selection
            if (ImGui::RadioButton("Fixed % TP", !m_config.use_atr_take_profit)) {
                m_config.use_atr_take_profit = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("ATR-based TP", m_config.use_atr_take_profit)) {
                m_config.use_atr_take_profit = true;
            }
            
            if (!m_config.use_atr_take_profit) {
                // Fixed percentage take profit
                ImGui::SliderFloat("Take Profit %", &m_config.take_profit_pct, 0.0f, 30.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Exit position when profit reaches this percentage");
                }
            } else {
                // ATR-based take profit
                ImGui::SliderFloat("ATR TP Multiplier", &m_config.atr_tp_multiplier, 0.5f, 10.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Take profit = Entry Price + (ATR * Multiplier) for longs\n"
                                     "Take profit = Entry Price - (ATR * Multiplier) for shorts");
                }
                ImGui::SliderInt("ATR TP Period", &m_config.atr_tp_period, 5, 50);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Number of bars to calculate Average True Range for take profit");
                }
            }
            
            ImGui::Unindent();
        }
        
        // Stop loss
        ImGui::Checkbox("Use Stop Loss", &m_config.use_stop_loss);
        if (m_config.use_stop_loss) {
            ImGui::Indent();
            
            // Stop loss type selection
            if (ImGui::RadioButton("Fixed %", !m_config.use_atr_stop_loss)) {
                m_config.use_atr_stop_loss = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("ATR-based", m_config.use_atr_stop_loss)) {
                m_config.use_atr_stop_loss = true;
            }
            
            if (!m_config.use_atr_stop_loss) {
                // Fixed percentage stop loss
                ImGui::SliderFloat("Stop Loss %", &m_config.stop_loss_pct, 1.0f, 10.0f);
            } else {
                // ATR-based stop loss
                ImGui::SliderFloat("ATR Multiplier", &m_config.atr_multiplier, 0.5f, 5.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Stop loss = Entry Price - (ATR * Multiplier) for longs\n"
                                     "Stop loss = Entry Price + (ATR * Multiplier) for shorts");
                }
                ImGui::SliderInt("ATR Period", &m_config.atr_period, 5, 50);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Number of bars to calculate Average True Range");
                }
            }
            
            ImGui::SliderInt("Stop Loss Cooldown (bars)", &m_config.stop_loss_cooldown_bars, 0, 10);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Number of bars to wait after stop loss before allowing re-entry.\n"
                                 "Set to 0 to allow immediate re-entry (but not on same bar).");
            }
            ImGui::Unindent();
        }
        
        // Time-based exit
        ImGui::Checkbox("Use Time-Based Exit", &m_config.use_time_exit);
        if (m_config.use_time_exit) {
            ImGui::Indent();
            ImGui::SliderInt("Max Holding Period (bars)", &m_config.max_holding_bars, 1, 50);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Exit position after this many bars regardless of other conditions");
            }
            ImGui::Unindent();
        }
        
        // Warning if no exit method is enabled
        if (!m_config.use_signal_exit && !m_config.use_take_profit && 
            !m_config.use_stop_loss && !m_config.use_time_exit) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Warning: No exit methods enabled!");
        }
        
        ImGui::Separator();
        ImGui::Text("Entry Options:");
        ImGui::Checkbox("Use Limit Orders", &m_config.use_limit_orders);
        if (m_config.use_limit_orders) {
            ImGui::Indent();
            ImGui::SliderInt("Limit Order Window", &m_config.limit_order_window, 1, 20);
            ImGui::SliderFloat("Limit Order Offset", &m_config.limit_order_offset, 0.0001f, 0.01f, "%.4f");
            ImGui::Unindent();
        }
        
        ImGui::Separator();
        ImGui::Text("Entry Thresholds:");
        // Choose thresholds for entries (and reversals if enabled)
        bool roc_selected = (m_config.threshold_choice == TradeSimulator::ThresholdChoice::OptimalROC);
        bool pct_selected = (m_config.threshold_choice == TradeSimulator::ThresholdChoice::Percentile);
        bool zero_selected = (m_config.threshold_choice == TradeSimulator::ThresholdChoice::ZeroCrossover);

        if (ImGui::RadioButton("Optimal ROC thresholds", roc_selected)) {
            m_config.threshold_choice = TradeSimulator::ThresholdChoice::OptimalROC;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use per-fold thresholds optimized by profit factor on training data (long/short), computed in walk-forward.");
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Percentile 95/5 thresholds", pct_selected)) {
            m_config.threshold_choice = TradeSimulator::ThresholdChoice::Percentile;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use per-fold 95th percentile for longs and 5th percentile for shorts, computed in walk-forward.");
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Zero crossover", zero_selected)) {
            m_config.threshold_choice = TradeSimulator::ThresholdChoice::ZeroCrossover;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Long: prediction > 0; Short: prediction < 0. Leak-free and similar to original behavior.");
        }
        ImGui::TextWrapped("Note: These thresholds apply to both entries and, when 'Honor Signal Reversal' is enabled, to reversal signals as well.");

        ImGui::Separator();
        ImGui::Text("Stress Test Settings:");
        ImGui::Checkbox("Enable Stress Tests", &m_enable_stress_tests);
        ImGui::SetNextItemWidth(180);
        ImGui::InputInt("Bootstrap Iterations", &m_bootstrap_iterations, 100, 500);
        ImGui::SetNextItemWidth(180);
        ImGui::InputInt("MCPT Iterations", &m_mcpt_iterations, 100, 500);
        ImGui::SetNextItemWidth(180);
        int seed_as_int = static_cast<int>(m_stress_seed & 0x7fffffff);
        if (ImGui::InputInt("Stress Seed", &seed_as_int, 1, 1000)) {
            if (seed_as_int < 0) seed_as_int = -seed_as_int;
            m_stress_seed = static_cast<unsigned int>(seed_as_int);
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize##StressSeed")) {
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            m_stress_seed = static_cast<unsigned int>(now);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use current time to randomize bootstrap seed.");
        }

        ImGui::Separator();
        ImGui::Text("Display Options:");
        ImGui::Checkbox("Show Trade List", &m_show_trade_list);
        ImGui::SameLine();
        ImGui::Checkbox("Show P&L Chart", &m_show_pnl_chart);
        ImGui::SameLine();
        ImGui::Checkbox("Show Per-Fold Stats", &m_show_per_fold_stats);
        ImGui::SameLine();
        ImGui::Checkbox("Show Performance Report", &m_show_performance_report);
    }
}

void TradeSimulationWindow::DrawExecutionControls() {
    // Check data availability
    bool has_ohlcv = m_candlestick_chart && m_candlestick_chart->HasAnyData();
    bool has_simulation = m_simulation_window && m_simulation_window->HasResults();
    if (!has_simulation) {
        m_selected_run_index = -1;
    }
    
    if (!has_ohlcv) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Warning: No OHLCV data loaded");
    }
    if (!has_simulation) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Warning: No simulation results available");
    }
    
    // Simulation run selection
    if (has_simulation) {
        auto run_names = m_simulation_window->GetRunNames();
        if (run_names.empty()) {
            m_selected_run_index = -1;
        } else {
            if (m_selected_run_index >= static_cast<int>(run_names.size())) {
                m_selected_run_index = static_cast<int>(run_names.size() - 1);
            }
            if (m_selected_run_index < 0) {
                m_selected_run_index = static_cast<int>(run_names.size() - 1);
            }
            ImGui::Text("Select Simulation Run:");
            
            const char* preview = m_selected_run_index >= 0 && m_selected_run_index < (int)run_names.size() ?
                run_names[m_selected_run_index].c_str() : "Select a run...";
            
            if (ImGui::BeginCombo("##SimulationRun", preview)) {
                for (size_t i = 0; i < run_names.size(); ++i) {
                    bool is_selected = (m_selected_run_index == (int)i);
                    if (ImGui::Selectable(run_names[i].c_str(), is_selected)) {
                        m_selected_run_index = (int)i;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
    }
    
    // Show feature schedule for selected run (scrollable) to avoid pushing fold stats off screen
    if (has_simulation && m_selected_run_index >= 0) {
        const simulation::SimulationRun* run = m_simulation_window->GetRunByIndex(m_selected_run_index);
        if (run && run->using_feature_schedule && !run->feature_schedule.empty()) {
            ImGui::Separator();
            ImGui::Text("Feature Schedule (selected run):");
            ImGui::BeginChild("FeatureScheduleChild", ImVec2(0, 120), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(run->feature_schedule.c_str());
            ImGui::EndChild();
        }
    }

    bool can_run = has_ohlcv && has_simulation && m_selected_run_index >= 0;
    
    if (!can_run) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Run Trade Simulation", ImVec2(200, 30))) {
        RunTradeSimulation();
    }
    
    if (!can_run) {
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Clear Results")) {
        m_simulator.ClearResults();
        m_has_results = false;
    }
}

void TradeSimulationWindow::DrawResults() {
    const auto& all_trades = m_simulator.GetTrades();

    if (ImGui::Button("Save Simulation")) {
        SaveSimulation();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Label", m_simulationLabel, IM_ARRAYSIZE(m_simulationLabel));
    if (!m_saveStatusMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", m_saveStatusMessage.c_str());
    }
    ImGui::Separator();
    
    // Apply trade filter
    std::vector<const ExecutedTrade*> filtered_trades;
    for (const auto& trade : all_trades) {
        if (m_trade_filter == TradeFilter::All ||
            (m_trade_filter == TradeFilter::LongOnly && trade.is_long) ||
            (m_trade_filter == TradeFilter::ShortOnly && !trade.is_long)) {
            filtered_trades.push_back(&trade);
        }
    }
    
    // Trade filter selection
    ImGui::Text("Trade Filter:");
    ImGui::SameLine();
    if (ImGui::RadioButton("All", m_trade_filter == TradeFilter::All)) {
        m_trade_filter = TradeFilter::All;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Long Only", m_trade_filter == TradeFilter::LongOnly)) {
        m_trade_filter = TradeFilter::LongOnly;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Short Only", m_trade_filter == TradeFilter::ShortOnly)) {
        m_trade_filter = TradeFilter::ShortOnly;
    }
    
    ImGui::Separator();
    ImGui::Text("Results Summary (Filtered)");
    ImGui::Separator();
    
    // Calculate filtered stats
    float filtered_pnl = 0;
    int winning_trades = 0;
    float cumulative_return = 0;
    
    for (const auto* trade : filtered_trades) {
        filtered_pnl += trade->pnl;
        if (trade->pnl > 0) winning_trades++;
        cumulative_return += trade->return_pct;
    }
    
    float filtered_win_rate = filtered_trades.empty() ? 0 : 
        (100.0f * winning_trades) / filtered_trades.size();
    
    // Summary stats
    ImGui::Text("Filtered Trades: %zu / %zu", filtered_trades.size(), all_trades.size());
    ImGui::Text("Filtered P&L: %.2f", filtered_pnl);
    ImGui::Text("Filtered Win Rate: %.1f%%", filtered_win_rate);
    ImGui::Text("Cumulative Return: %.2f%%", cumulative_return);
    
    if (m_show_per_fold_stats && !filtered_trades.empty()) {
        // Calculate per-fold statistics
        std::map<int, std::vector<const ExecutedTrade*>> fold_trades;
        for (const auto* trade : filtered_trades) {
            fold_trades[trade->fold_index].push_back(trade);
        }
        
        ImGui::Separator();
        ImGui::Text("Per-Fold Statistics:");
        
        if (ImGui::BeginTable("FoldStats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Fold");
            ImGui::TableSetupColumn("Trades");
            ImGui::TableSetupColumn("P&L");
            ImGui::TableSetupColumn("Win Rate");
            ImGui::TableSetupColumn("Avg Return");
            ImGui::TableHeadersRow();
            
            for (auto it = fold_trades.begin(); it != fold_trades.end(); ++it) {
                int fold_idx = it->first;
                const auto& fold_trade_ptrs = it->second;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", fold_idx);
                
                ImGui::TableNextColumn();
                ImGui::Text("%zu", fold_trade_ptrs.size());
                
                // Calculate fold P&L
                float fold_pnl = 0;
                int wins = 0;
                float total_return = 0;
                for (const auto* trade : fold_trade_ptrs) {
                    fold_pnl += trade->pnl;
                    if (trade->pnl > 0) wins++;
                    total_return += trade->return_pct;
                }
                
                ImGui::TableNextColumn();
                if (fold_pnl > 0) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "+%.2f", fold_pnl);
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f", fold_pnl);
                }
                
                ImGui::TableNextColumn();
                float win_rate = fold_trade_ptrs.empty() ? 0 : (100.0f * wins / fold_trade_ptrs.size());
                ImGui::Text("%.1f%%", win_rate);
                
                ImGui::TableNextColumn();
                float avg_return = fold_trade_ptrs.empty() ? 0 : (total_return / fold_trade_ptrs.size());
                if (avg_return > 0) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "+%.2f%%", avg_return);
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f%%", avg_return);
                }
            }
            
            ImGui::EndTable();
        }
    }
}

void TradeSimulationWindow::DrawTradeList() {
    const auto& all_trades = m_simulator.GetTrades();
    
    // Apply trade filter
    std::vector<const ExecutedTrade*> filtered_trades;
    for (const auto& trade : all_trades) {
        if (m_trade_filter == TradeFilter::All ||
            (m_trade_filter == TradeFilter::LongOnly && trade.is_long) ||
            (m_trade_filter == TradeFilter::ShortOnly && !trade.is_long)) {
            filtered_trades.push_back(&trade);
        }
    }
    
    ImGui::Text("Trade List (Filtered: %zu/%zu trades)", filtered_trades.size(), all_trades.size());
    
    // Always call BeginChild/EndChild pair properly
    bool child_opened = ImGui::BeginChild("TradeListChild", ImVec2(0, 200), true);
    if (child_opened) {
        if (ImGui::BeginTable("Trades", 11, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Fold");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Entry Time");
            ImGui::TableSetupColumn("Exit Time");
            ImGui::TableSetupColumn("Entry Price");
            ImGui::TableSetupColumn("Exit Price");
            ImGui::TableSetupColumn("Entry Signal");
            ImGui::TableSetupColumn("Exit Signal");
            ImGui::TableSetupColumn("P&L");
            ImGui::TableSetupColumn("Return %");
            ImGui::TableSetupColumn("Cumul. Return %");
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            
            float cumulative_return = 0;
            for (const auto* trade : filtered_trades) {
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                ImGui::Text("%d", trade->fold_index);
                
                ImGui::TableNextColumn();
                ImGui::Text(trade->is_long ? "Long" : "Short");

                const std::string entry_time = FormatTimestamp(trade->entry_timestamp);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(entry_time.c_str());

                const std::string exit_time = FormatTimestamp(trade->exit_timestamp);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(exit_time.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", trade->entry_price);
                
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", trade->exit_price);
                
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", trade->entry_signal);
                
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", trade->exit_signal);
                
                ImGui::TableNextColumn();
                if (trade->pnl > 0) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "+%.2f", trade->pnl);
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f", trade->pnl);
                }
                
                ImGui::TableNextColumn();
                if (trade->return_pct > 0) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "+%.2f%%", trade->return_pct);
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f%%", trade->return_pct);
                }
                
                // Add cumulative return column
                ImGui::TableNextColumn();
                cumulative_return += trade->return_pct;
                if (cumulative_return > 0) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "+%.2f%%", cumulative_return);
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f%%", cumulative_return);
                }
            }
            
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();  // This must ALWAYS be called after BeginChild
}

void TradeSimulationWindow::DrawPnLChart() {
    const auto& all_trades = m_simulator.GetTrades();
    if (all_trades.empty()) {
        // Even without trades, show buy & hold if available
        const auto& buy_hold_pnl = m_simulator.GetBuyHoldPnL();
        const auto& buy_hold_timestamps_ms = m_simulator.GetBuyHoldTimestamps();
        if (buy_hold_pnl.empty() || buy_hold_timestamps_ms.empty()) {
            return;
        }
    }
    
    // Apply trade filter and calculate filtered cumulative P&L
    // Use actual timestamps for x-axis (using exit timestamps of trades)
    std::vector<float> filtered_cumulative_pnl;
    std::vector<double> x_data_timestamps;  // Use actual timestamps (in seconds for ImPlot)
    
    // First, find the first filtered trade for the initial point
    const ExecutedTrade* first_filtered_trade = nullptr;
    for (const auto& trade : all_trades) {
        if (m_trade_filter == TradeFilter::All ||
            (m_trade_filter == TradeFilter::LongOnly && trade.is_long) ||
            (m_trade_filter == TradeFilter::ShortOnly && !trade.is_long)) {
            first_filtered_trade = &trade;
            break;
        }
    }
    
    // Add initial point at time 0 with 0 P&L using first FILTERED trade's entry
    if (first_filtered_trade != nullptr) {
        // Convert milliseconds to seconds for ImPlot time axis
        x_data_timestamps.push_back(first_filtered_trade->entry_timestamp / 1000.0);
        filtered_cumulative_pnl.push_back(0);
    }
    
    float cumulative = 0.0f;
    // Now add the filtered trades
    for (size_t i = 0; i < all_trades.size(); ++i) {
        const auto& trade = all_trades[i];
        if (m_trade_filter == TradeFilter::All ||
            (m_trade_filter == TradeFilter::LongOnly && trade.is_long) ||
            (m_trade_filter == TradeFilter::ShortOnly && !trade.is_long)) {
            cumulative += trade.pnl;
            filtered_cumulative_pnl.push_back(cumulative);
            // Convert milliseconds to seconds for ImPlot time axis
            x_data_timestamps.push_back(trade.exit_timestamp / 1000.0);
        }
    }
    
    if (filtered_cumulative_pnl.empty()) {
        filtered_cumulative_pnl.push_back(0.0f);
        x_data_timestamps.push_back(0.0);
    }
    
    // Get buy & hold P&L (already calculated at each bar)
    const auto& buy_hold_pnl = m_simulator.GetBuyHoldPnL();
    const auto& buy_hold_timestamps_ms = m_simulator.GetBuyHoldTimestamps();
    std::vector<double> buy_hold_timestamps_sec;
    if (!buy_hold_pnl.empty() && !buy_hold_timestamps_ms.empty()) {
        buy_hold_timestamps_sec.reserve(buy_hold_timestamps_ms.size());
        for (double ts_ms : buy_hold_timestamps_ms) {
            buy_hold_timestamps_sec.push_back(ts_ms / 1000.0);
        }
    }
    
    // Get time range from trades (already in seconds)
    double min_time = x_data_timestamps.front();
    double max_time = x_data_timestamps.back();
    
    if (ImPlot::BeginPlot("Cumulative P&L (Filtered)", ImVec2(-1, 250))) {
        // Setup time axis
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%m/%d %H:%M");
        
        // Plot strategy P&L at actual timestamps
        // Convert float P&L to double for consistent types with timestamps
        std::vector<double> strategy_pnl_double(filtered_cumulative_pnl.begin(), filtered_cumulative_pnl.end());
        ImPlot::PlotLine("Strategy", x_data_timestamps.data(), strategy_pnl_double.data(), 
                        (int)strategy_pnl_double.size());
        
        // Plot buy & hold P&L using actual timestamps
        // Plot buy & hold P&L using actual timestamps (converted to seconds)
        if (!buy_hold_pnl.empty() && buy_hold_timestamps_sec.size() == buy_hold_pnl.size()) {
            std::vector<double> bh_pnl_double(buy_hold_pnl.begin(), buy_hold_pnl.end());
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImPlot::PlotLine("Buy & Hold", buy_hold_timestamps_sec.data(), bh_pnl_double.data(),
                             static_cast<int>(bh_pnl_double.size()));
            ImPlot::PopStyleColor();
        }
        
        // Draw zero line
        double zero_line[2] = {0, 0};
        double x_zero[2] = {min_time, max_time};
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
        ImPlot::PlotLine("##Zero", x_zero, zero_line, 2);
        ImPlot::PopStyleColor();
        
        ImPlot::EndPlot();
    }
}

void TradeSimulationWindow::RunTradeSimulation() {
    if (!m_simulation_window || !m_simulation_window->HasResults() || m_selected_run_index < 0) {
        std::cerr << "[TradeSimulationWindow] No simulation results selected" << std::endl;
        return;
    }
    
    // Get the selected simulation run
    const simulation::SimulationRun* sim_results = m_simulation_window->GetRunByIndex(m_selected_run_index);
    if (!sim_results) {
        std::cerr << "[TradeSimulationWindow] Failed to get selected simulation run" << std::endl;
        return;
    }
    
    std::cout << "[TradeSimulationWindow] Using simulation run: " << 
              (sim_results->name.empty() ? "Run " + std::to_string(m_selected_run_index + 1) : sim_results->name) 
              << " with " << sim_results->foldResults.size() << " folds" << std::endl;
    
    m_simulator.SetSimulationResults(sim_results);
    
    // Update configuration
    m_simulator.SetConfig(m_config);
    simulation::StressTestConfig stress_cfg;
    stress_cfg.enable = m_enable_stress_tests;
    stress_cfg.bootstrap_iterations = std::max(100, m_bootstrap_iterations);
    stress_cfg.mcpt_iterations = std::max(100, m_mcpt_iterations);
    stress_cfg.seed = m_stress_seed;
    m_simulator.SetStressTestConfig(stress_cfg);
    
    m_saveStatusMessage.clear();
    m_stress_cache_valid = false;
    // Run the simulation
    m_lastSimulationStart = std::chrono::system_clock::now();
    m_simulator.RunSimulation();
    m_lastSimulationEnd = std::chrono::system_clock::now();
    
    m_has_results = true;
    RecomputeStressReports();
}

void TradeSimulationWindow::RecomputeStressReports() {
    m_cached_stress_all = {};
    m_cached_stress_long = {};
    m_cached_stress_short = {};
    m_stress_cache_valid = false;

    if (!m_has_results) {
        m_stress_cache_valid = true;
        return;
    }

    const auto& trades = m_simulator.GetTrades();
    simulation::StressTestConfig base_cfg = m_simulator.GetStressTestConfig();
    const double position_size = static_cast<double>(m_simulator.GetPositionSize());

    auto compute_for_filter = [&](TradeFilter filter) {
        simulation::StressTestReport sr;
        sr.bootstrap_iterations = base_cfg.bootstrap_iterations;
        sr.mcpt_iterations = base_cfg.mcpt_iterations;

        std::vector<double> returns;
        std::vector<double> pnls;
        returns.reserve(trades.size());
        pnls.reserve(trades.size());

        for (const auto& trade : trades) {
            bool include = false;
            switch (filter) {
                case TradeFilter::All:
                    include = true;
                    break;
                case TradeFilter::LongOnly:
                    include = trade.is_long;
                    break;
                case TradeFilter::ShortOnly:
                    include = !trade.is_long;
                    break;
            }
            if (!include) continue;
            returns.push_back(static_cast<double>(trade.return_pct));
            pnls.push_back(static_cast<double>(trade.pnl));
        }

        sr.sample_size = static_cast<int>(returns.size());
        if (!base_cfg.enable || returns.empty() || position_size <= 0.0) {
            return sr;
        }

        auto cfg = base_cfg;
        const std::uint64_t filter_mix = static_cast<std::uint64_t>(static_cast<int>(filter) + 1);
        cfg.seed ^= filter_mix * 0x9e3779b97f4a7c15ULL;
        return simulation::RunStressTests(returns, pnls, position_size, cfg);
    };

    // Ensure performance report cache is populated
    (void)m_simulator.GetPerformanceReport();

    m_cached_stress_all = compute_for_filter(TradeFilter::All);
    m_cached_stress_long = compute_for_filter(TradeFilter::LongOnly);
    m_cached_stress_short = compute_for_filter(TradeFilter::ShortOnly);

    m_stress_cache_valid = true;
}

void TradeSimulationWindow::SaveSimulation() {
    if (!m_has_results) {
        m_saveStatusMessage = "No simulation results to save.";
        return;
    }
    if (!m_simulation_window || !m_simulation_window->HasResults() || m_selected_run_index < 0) {
        m_saveStatusMessage = "No walkforward run selected.";
        return;
    }

    const simulation::SimulationRun* run = m_simulation_window->GetRunByIndex(m_selected_run_index);
    if (!run) {
        m_saveStatusMessage = "Failed to resolve selected run.";
        return;
    }

    std::string datasetSlug = run->dataset_measurement.empty() ? ToSlug(run->name) : run->dataset_measurement;
    if (datasetSlug.empty() && m_time_series_window) {
        datasetSlug = m_time_series_window->GetSuggestedDatasetId();
    }
    datasetSlug = ToSlug(datasetSlug);
    if (datasetSlug.empty()) {
        datasetSlug = "dataset";
    }

    std::string runMeasurement = run->prediction_measurement.empty() ? (datasetSlug + "_run" + std::to_string(m_selected_run_index + 1)) : run->prediction_measurement;
    runMeasurement = ToSlug(runMeasurement);
    if (runMeasurement.empty()) {
        runMeasurement = datasetSlug + "_run" + std::to_string(m_selected_run_index + 1);
    }

    std::string baseLabel = m_simulationLabel[0] ? ToSlug(m_simulationLabel) : datasetSlug;
    if (baseLabel.empty()) {
        baseLabel = "sim";
    }

    std::string simulationMeasurement = baseLabel + "_sim" + std::to_string(++m_simulation_counter);

    Stage1MetadataWriter::SimulationRecord record;
    record.simulation_id = Stage1MetadataWriter::MakeDeterministicUuid(simulationMeasurement);
    record.run_id = Stage1MetadataWriter::MakeDeterministicUuid(runMeasurement);
    record.dataset_id = Stage1MetadataWriter::MakeDeterministicUuid(datasetSlug);
    record.input_run_measurement = run->prediction_measurement.empty() ? runMeasurement : run->prediction_measurement;
    record.questdb_namespace = simulationMeasurement;
    record.mode = "dual";
    record.config_json = SerializeTradeConfig(m_config);
    record.summary_metrics_json = SerializePerformanceReport(m_simulator.GetPerformanceReport());
    record.started_at = (m_lastSimulationStart.time_since_epoch().count() == 0)
        ? std::chrono::system_clock::now() : m_lastSimulationStart;
    record.completed_at = (m_lastSimulationEnd.time_since_epoch().count() == 0)
        ? record.started_at : m_lastSimulationEnd;
    record.status = "COMPLETED";

    const auto report = m_simulator.GetPerformanceReport();
    Stage1MetadataWriter::SimulationBucketRecord dualBucket;
    dualBucket.side = "dual";
    dualBucket.trade_count = report.total_trades;
    dualBucket.win_count = report.winning_trades;
    dualBucket.profit_factor = report.profit_factor;
    dualBucket.avg_return_pct = report.total_return_pct;
    dualBucket.max_drawdown_pct = report.max_drawdown_pct;
    dualBucket.notes = "Combined strategy";
    record.buckets.push_back(dualBucket);

    Stage1MetadataWriter::SimulationBucketRecord longBucket;
    longBucket.side = "long";
    longBucket.trade_count = report.long_trades;
    longBucket.win_count = report.long_winning_trades;
    longBucket.profit_factor = report.long_profit_factor;
    longBucket.avg_return_pct = report.long_return_pct;
    longBucket.max_drawdown_pct = report.long_max_drawdown_pct;
    longBucket.notes = "Long-only slice";
    record.buckets.push_back(longBucket);

    Stage1MetadataWriter::SimulationBucketRecord shortBucket;
    shortBucket.side = "short";
    shortBucket.trade_count = report.short_trades;
    shortBucket.win_count = report.short_winning_trades;
    shortBucket.profit_factor = report.short_profit_factor;
    shortBucket.avg_return_pct = report.short_return_pct;
    shortBucket.max_drawdown_pct = report.short_max_drawdown_pct;
    shortBucket.notes = "Short-only slice";
    record.buckets.push_back(shortBucket);

    const auto& trades = m_simulator.GetTrades();
    Stage1MetadataWriter::Instance().RecordSimulationRun(record, trades);
    std::string ilpError;
    if (!questdb::ExportTradingSimulation(record, trades, {}, &ilpError)) {
        std::cerr << "[QuestDB] Failed to export trading simulation: " << ilpError << std::endl;
    }
    m_saveStatusMessage = "Recorded simulation " + simulationMeasurement + " with " + std::to_string(trades.size()) + " trades.";
}

void TradeSimulationWindow::DrawDrawdownChart() {
    const auto& all_trades = m_simulator.GetTrades();
    if (all_trades.empty()) return;
    
    // Apply trade filter and calculate filtered cumulative returns
    std::vector<double> drawdown;
    std::vector<double> bh_drawdown;
    std::vector<double> x_axis_timestamps;  // Use actual timestamps (in seconds)
    
    float cumulative_return = 100.0f;  // Start at 100%
    float peak_return = 100.0f;
    
    const auto& buy_hold_pnl = m_simulator.GetBuyHoldPnL();
    const auto& buy_hold_timestamps_ms = m_simulator.GetBuyHoldTimestamps();
    
    std::vector<double> buy_hold_timestamps;
    
    // First, find the first filtered trade for the initial point
    const ExecutedTrade* first_filtered_trade = nullptr;
    for (const auto& trade : all_trades) {
        if (m_trade_filter == TradeFilter::All ||
            (m_trade_filter == TradeFilter::LongOnly && trade.is_long) ||
            (m_trade_filter == TradeFilter::ShortOnly && !trade.is_long)) {
            first_filtered_trade = &trade;
            break;
        }
    }
    
    // Add initial point at time 0 with 0 drawdown using first FILTERED trade's entry
    if (first_filtered_trade != nullptr) {
        // Convert milliseconds to seconds for ImPlot time axis
        x_axis_timestamps.push_back(first_filtered_trade->entry_timestamp / 1000.0);
        drawdown.push_back(0);
    }
    
    for (size_t i = 0; i < all_trades.size(); ++i) {
        const auto& trade = all_trades[i];
        if (m_trade_filter == TradeFilter::All ||
            (m_trade_filter == TradeFilter::LongOnly && trade.is_long) ||
            (m_trade_filter == TradeFilter::ShortOnly && !trade.is_long)) {
            
            // Calculate strategy cumulative return
            cumulative_return += trade.return_pct;
            
            if (cumulative_return > peak_return) {
                peak_return = cumulative_return;
            }
            
            // Calculate strategy drawdown
            float dd = 0;
            if (peak_return > 0) {
                dd = ((peak_return - cumulative_return) / peak_return) * 100.0f;
            }
            
            drawdown.push_back(-dd);  // Negative for display
            // Convert milliseconds to seconds for ImPlot time axis
            x_axis_timestamps.push_back(trade.exit_timestamp / 1000.0);
        }
    }
    
    if (drawdown.empty()) return;
    
    // Get time range from trades (already in seconds)
    double min_time = x_axis_timestamps.front();
    double max_time = x_axis_timestamps.back();
    
    // Calculate buy & hold drawdown INDEPENDENTLY of strategy trades
    if (!buy_hold_pnl.empty() && !buy_hold_timestamps.empty()) {
        float bh_peak = 100.0f;
        bh_drawdown.reserve(buy_hold_pnl.size());
        
        for (size_t i = 0; i < buy_hold_pnl.size(); ++i) {
            float bh_equity = 1000.0f + buy_hold_pnl[i];
            float bh_return = ((bh_equity - 1000.0f) / 1000.0f) * 100.0f + 100.0f;
            
            if (bh_return > bh_peak) {
                bh_peak = bh_return;
            }
            
            float bh_dd = 0;
            if (bh_peak > 0) {
                bh_dd = ((bh_peak - bh_return) / bh_peak) * 100.0f;
            }
            bh_drawdown.push_back(-bh_dd);
        }
    }
    
    // Find max drawdown for auto-scaling
    auto max_dd_it = std::min_element(drawdown.begin(), drawdown.end());
    double max_dd = (max_dd_it != drawdown.end()) ? *max_dd_it : 0;
    
    // Also check buy & hold max drawdown
    if (!bh_drawdown.empty()) {
        auto bh_max_dd_it = std::min_element(bh_drawdown.begin(), bh_drawdown.end());
        if (bh_max_dd_it != bh_drawdown.end() && *bh_max_dd_it < max_dd) {
            max_dd = *bh_max_dd_it;
        }
    }
    
    double y_min = std::min(-20.0, max_dd * 1.2);  // At least -20%, or 20% more than max DD
    
    if (ImPlot::BeginPlot("Drawdown % (Filtered)", ImVec2(-1, 200))) {
        // Setup time axis
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%m/%d %H:%M");
        ImPlot::SetupAxes("Time", "Drawdown %");
        ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, 2, ImGuiCond_Always);
        
        // Plot strategy drawdown at actual timestamps
        ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.3f);
        ImPlot::PlotShaded("Strategy DD", x_axis_timestamps.data(), drawdown.data(), (int)drawdown.size(), 0);
        ImPlot::PopStyleVar();
        ImPlot::PlotLine("Strategy", x_axis_timestamps.data(), drawdown.data(), (int)drawdown.size());
        
        // Plot buy & hold drawdown at corresponding timestamps
        if (!bh_drawdown.empty() && !buy_hold_timestamps.empty()) {
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImPlot::PlotLine("Buy & Hold", buy_hold_timestamps.data(), bh_drawdown.data(), (int)bh_drawdown.size());
            ImPlot::PopStyleColor();
        }
        
        // Draw zero line
        double x_zero[] = {min_time, max_time};
        double zero_line[] = {0, 0};
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
        ImPlot::PlotLine("##Zero", x_zero, zero_line, 2);
        ImPlot::PopStyleColor();
        
        // Show max drawdown annotation for strategy
        if (max_dd_it != drawdown.end()) {
            size_t max_dd_idx = std::distance(drawdown.begin(), max_dd_it);
            double max_dd_value = *max_dd_it;
            double max_dd_time = x_axis_timestamps[max_dd_idx];
            
            ImPlot::Annotation(max_dd_time, max_dd_value, 
                              ImVec4(1, 0, 0, 1), ImVec2(10, -10), true,
                              "Strategy Max DD: %.2f%%", -max_dd_value);
        }
        
        ImPlot::EndPlot();
    }
}void TradeSimulationWindow::DrawPerformanceReport() {
    auto report = m_simulator.GetPerformanceReport();
    
    if (!m_stress_cache_valid) {
        RecomputeStressReports();
    }

    const simulation::StressTestReport* stress_report_ptr = &m_cached_stress_all;
    const char* stress_label = "Combined";
    float stress_total_return = report.total_return_pct;

    switch (m_trade_filter) {
        case TradeFilter::All:
            stress_report_ptr = &m_cached_stress_all;
            stress_label = "Combined";
            break;
        case TradeFilter::LongOnly:
            stress_report_ptr = &m_cached_stress_long;
            stress_label = "Long Only";
            stress_total_return = report.long_return_pct;
            break;
        case TradeFilter::ShortOnly:
            stress_report_ptr = &m_cached_stress_short;
            stress_label = "Short Only";
            stress_total_return = report.short_return_pct;
            break;
    }

    const auto& stress_report = *stress_report_ptr;
    
    ImGui::Text("Performance Report");
    ImGui::Separator();
    
    // Create a table comparing all strategies
    if (ImGui::BeginTable("PerformanceReport", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Combined", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Long Only", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Short Only", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Buy & Hold", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();
        
        // Helper to color returns
        auto show_return = [](float ret) {
            if (ret > 0) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "+%.2f%%", ret);
            } else if (ret < 0) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f%%", ret);
            } else {
                ImGui::Text("0.00%%");
            }
        };
        
        // Total Return
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Total Return");
        ImGui::TableNextColumn();
        show_return(report.total_return_pct);
        ImGui::TableNextColumn();
        show_return(report.long_return_pct);
        ImGui::TableNextColumn();
        show_return(report.short_return_pct);
        ImGui::TableNextColumn();
        show_return(report.buy_hold_return_pct);
        
        // Profit Factor
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Profit Factor");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Gross profit / Gross loss\nValues > 1.0 are profitable");
        }
        
        auto show_pf = [](float pf) {
            if (pf > 1.0f) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.2f", pf);
            } else if (pf > 0) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f", pf);
            } else {
                ImGui::Text("N/A");
            }
        };
        
        ImGui::TableNextColumn();
        show_pf(report.profit_factor);
        ImGui::TableNextColumn();
        show_pf(report.long_profit_factor);
        ImGui::TableNextColumn();
        show_pf(report.short_profit_factor);
        ImGui::TableNextColumn();
        show_pf(report.buy_hold_profit_factor);
        
        // Sharpe Ratio
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Sharpe Ratio");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Risk-adjusted return\nHigher is better, > 1.0 is good");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", report.sharpe_ratio);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", report.long_sharpe_ratio);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", report.short_sharpe_ratio);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", report.buy_hold_sharpe_ratio);
        
        // Number of Trades
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Trades");
        ImGui::TableNextColumn();
        ImGui::Text("%d", report.total_trades);
        ImGui::TableNextColumn();
        ImGui::Text("%d", report.long_trades);
        ImGui::TableNextColumn();
        ImGui::Text("%d", report.short_trades);
        ImGui::TableNextColumn();
        ImGui::Text("N/A");
        
        // Win Rate
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Win Rate");
        
        auto show_winrate = [](int wins, int total) {
            if (total > 0) {
                float wr = (100.0f * wins) / total;
                if (wr > 50) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.1f%%", wr);
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.1f%%", wr);
                }
            } else {
                ImGui::Text("N/A");
            }
        };
        
        ImGui::TableNextColumn();
        show_winrate(report.winning_trades, report.total_trades);
        ImGui::TableNextColumn();
        show_winrate(report.long_winning_trades, report.long_trades);
        ImGui::TableNextColumn();
        show_winrate(report.short_winning_trades, report.short_trades);
        ImGui::TableNextColumn();
        ImGui::Text("N/A");
        
        // Bars in Position
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Bars in Position");
        ImGui::TableNextColumn();
        ImGui::Text("%d", report.total_bars_in_position);
        ImGui::TableNextColumn();
        ImGui::Text("%d", report.long_bars_in_position);
        ImGui::TableNextColumn();
        ImGui::Text("%d", report.short_bars_in_position);
        ImGui::TableNextColumn();
        ImGui::Text("Always");
        
        // Max Drawdown
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Max Drawdown");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maximum peak-to-trough decline");
        }
        
        auto show_dd = [](float dd) {
            if (dd > 0) {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "%.2f%%", dd);
            } else {
                ImGui::Text("0.00%%");
            }
        };
        
        ImGui::TableNextColumn();
        show_dd(report.max_drawdown_pct);
        ImGui::TableNextColumn();
        show_dd(report.long_max_drawdown_pct);
        ImGui::TableNextColumn();
        show_dd(report.short_max_drawdown_pct);
        ImGui::TableNextColumn();
        show_dd(report.buy_hold_max_drawdown_pct);
        
        // Average Drawdown
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Avg Drawdown");
        ImGui::TableNextColumn();
        ImGui::Text("%.2f%%", report.avg_drawdown_pct);
        ImGui::TableNextColumn();
        ImGui::Text("-");
        ImGui::TableNextColumn();
        ImGui::Text("-");
        ImGui::TableNextColumn();
        ImGui::Text("-");
        
        // Max DD Duration
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Max DD Duration");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Longest period in drawdown (bars)");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d bars", report.max_drawdown_duration);
        ImGui::TableNextColumn();
        ImGui::Text("-");
        ImGui::TableNextColumn();
        ImGui::Text("-");
        ImGui::TableNextColumn();
        ImGui::Text("-");
        
        ImGui::EndTable();
    }
    
    // Show outperformance comparisons
    ImGui::Separator();
    ImGui::Text("Performance vs Buy & Hold:");
    
    auto show_comparison = [](const char* label, float strategy_ret, float bh_ret) {
        float diff = strategy_ret - bh_ret;
        ImGui::Text("%s: ", label);
        ImGui::SameLine();
        if (diff > 0) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Outperformed by +%.2f%%", diff);
        } else if (diff < 0) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Underperformed by %.2f%%", diff);
        } else {
            ImGui::Text("Same performance");
        }
    };
    
    show_comparison("Combined", report.total_return_pct, report.buy_hold_return_pct);
    if (report.long_trades > 0) {
        show_comparison("Long Only", report.long_return_pct, report.buy_hold_return_pct);
    }
    if (report.short_trades > 0) {
        show_comparison("Short Only", report.short_return_pct, report.buy_hold_return_pct);
    }

    ImGui::Separator();
    ImGui::Text("Stress Tests (%s)", stress_label);
    ImGui::SameLine();
    ImGui::TextDisabled("[%d trades | %d bootstrap | %d MCPT]",
                        stress_report.sample_size,
                        stress_report.bootstrap_iterations,
                        stress_report.mcpt_iterations);

    if (!stress_report.computed) {
        ImGui::TextWrapped("Not enough trades to produce stress statistics for this slice or stress testing disabled.");
        return;
    }

    if (ImGui::BeginTable("StressCI", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Estimate", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("90% CI", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("95% CI", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("p-value", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        auto show_ci_row = [](const char* label,
                              const simulation::BootstrapInterval& ci,
                              double pvalue,
                              const char* estimate_suffix = "") {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f%s", ci.estimate, estimate_suffix);
            ImGui::TableNextColumn();
            ImGui::Text("[%.3f, %.3f]", ci.lower_90, ci.upper_90);
            ImGui::TableNextColumn();
            ImGui::Text("[%.3f, %.3f]", ci.lower_95, ci.upper_95);
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", pvalue);
        };

        show_ci_row("Sharpe Ratio", stress_report.sharpe_ci, stress_report.monte_carlo.sharpe_pvalue);
        show_ci_row("Profit Factor", stress_report.profit_factor_ci, stress_report.monte_carlo.profit_factor_pvalue);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Total Return %%");
        ImGui::TableNextColumn();
        ImGui::Text("%.3f%%", stress_total_return);
        ImGui::TableNextColumn();
        ImGui::Text("[%.3f, %.3f]", stress_report.total_return_ci.lower_90, stress_report.total_return_ci.upper_90);
        ImGui::TableNextColumn();
        ImGui::Text("[%.3f, %.3f]", stress_report.total_return_ci.lower_95, stress_report.total_return_ci.upper_95);
        ImGui::TableNextColumn();
        ImGui::Text("%.4f", stress_report.monte_carlo.total_return_pvalue);

        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Drawdown Quantiles (%%)");
    if (ImGui::BeginTable("StressDrawdown", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("50%%", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("90%%", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("95%%", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("99%%", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%.2f%%", stress_report.drawdown_quantiles.q50);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f%%", stress_report.drawdown_quantiles.q90);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f%%", stress_report.drawdown_quantiles.q95);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f%%", stress_report.drawdown_quantiles.q99);

        ImGui::EndTable();
    }

    ImGui::Text("Probability observed max drawdown or worse: %.4f", stress_report.monte_carlo.max_drawdown_pvalue);
}

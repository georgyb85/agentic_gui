#pragma once

#include "implot.h" // For ImPlot types like ImVec4
#include "imgui.h"  // For ImU32, ImDrawList, etc.

// Forward declaration if needed, or ensure imgui.h is included before implot.h if it defines ImVec4
// (Typically implot.h includes imgui.h or expects it to be included)

namespace MyImPlot {

/// Plots a candlestick chart.
/// @param label_id Unique identifier for the plot item.
/// @param xs Pointer to the x-axis data (e.g., timestamps or indices).
/// @param opens Pointer to the opening prices.
/// @param closes Pointer to the closing prices.
/// @param lows Pointer to the low prices.
/// @param highs Pointer to the high prices.
/// @param count Number of data points.
/// @param width_percent Percentage of the x-interval to use for the candle width (0.0 to 1.0).
/// @param bullCol Color for bullish (close > open) candles.
/// @param bearCol Color for bearish (close <= open) candles.
void PlotCandlestick(const char* label_id, const double* xs, const double* opens, const double* closes, const double* lows, const double* highs, int count, float width_percent, ImVec4 bullCol, ImVec4 bearCol, double candle_width_plot_units = -1.0);

} // namespace MyImPlot
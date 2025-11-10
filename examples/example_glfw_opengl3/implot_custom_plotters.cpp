#include "implot_custom_plotters.h"
#include "imgui.h"      // For ImGui::GetColorU32, ImDrawList
#include "implot.h"     // For ImPlot::GetPlotDrawList, ImPlot::BeginItem, etc.
#include "implot_internal.h" // For ImPlot::GetCurrentItem, FitPoint if still needed (though FitThisFrame handles it)

// Ensure this is included if not transitively by implot.h or imgui.h
// #include <cmath> // For fabs or other math functions if used directly, though not in this specific snippet

namespace MyImPlot {

void PlotCandlestick(const char* label_id, const double* xs, const double* opens, const double* closes, const double* lows, const double* highs, int count, float width_percent, ImVec4 bullCol, ImVec4 bearCol, double candle_width_plot_units) {
    // get ImGui window DrawList
    ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    // calc real value width
    double half_width;
    if (candle_width_plot_units > 0) {
        // Use the pre-calculated width from the main chart (same as volume bars)
        half_width = candle_width_plot_units * 0.5;
    } else if (count > 1) {
        // Fallback to old calculation if width not provided
        double time_diff = xs[1] - xs[0];
        // Check if we're dealing with timestamps (large values) vs indices (small values)
        if (time_diff > 100) { // Timestamp mode: time_diff is in seconds
            half_width = time_diff * width_percent * 0.5;
        } else { // Index mode: time_diff is around 1.0
            half_width = time_diff * width_percent * 0.5;
        }
    } else {
        half_width = width_percent * 0.5;
    }

    // begin plot item
    if (ImPlot::BeginItem(label_id)) {
        // override legend icon color
        ImPlot::GetCurrentItem()->Color = IM_COL32(64,64,64,255);
        // fit data if requested
        if (ImPlot::FitThisFrame()) {
            for (int i = 0; i < count; ++i) {
                ImPlot::FitPoint(ImPlotPoint(xs[i], lows[i]));
                ImPlot::FitPoint(ImPlotPoint(xs[i], highs[i]));
            }
        }
        // render data
        for (int i = 0; i < count; ++i) {
            // Ensure points are plotted within the visible plot area
            // This basic check might be needed if points can go way off-screen
            // though ImPlot's clipping usually handles this.
            // For extreme cases, more robust culling might be desired.

            ImVec2 open_pos  = ImPlot::PlotToPixels(xs[i] - half_width, opens[i]);
            ImVec2 close_pos = ImPlot::PlotToPixels(xs[i] + half_width, closes[i]);
            ImVec2 low_pos   = ImPlot::PlotToPixels(xs[i], lows[i]);
            ImVec2 high_pos  = ImPlot::PlotToPixels(xs[i], highs[i]);
            
            ImU32 color = ImGui::GetColorU32(opens[i] > closes[i] ? bearCol : bullCol);
            
            // Ensure the wick is drawn correctly regardless of open/close order for y-coordinates
            float wick_top_y = ImMin(ImPlot::PlotToPixels(xs[i], highs[i]).y, ImPlot::PlotToPixels(xs[i], lows[i]).y);
            float wick_bottom_y = ImMax(ImPlot::PlotToPixels(xs[i], highs[i]).y, ImPlot::PlotToPixels(xs[i], lows[i]).y);
            ImVec2 wick_start(low_pos.x, wick_top_y); // Use low_pos.x for the center line
            ImVec2 wick_end(low_pos.x, wick_bottom_y); // Use low_pos.x for the center line

            draw_list->AddLine(wick_start, wick_end, color);

            // Ensure the body is drawn correctly regardless of open/close order for y-coordinates
            float body_top_y = ImMin(open_pos.y, close_pos.y);
            float body_bottom_y = ImMax(open_pos.y, close_pos.y);
            ImVec2 body_top_left(open_pos.x, body_top_y);
            ImVec2 body_bottom_right(close_pos.x, body_bottom_y);

            draw_list->AddRectFilled(body_top_left, body_bottom_right, color);
        }

        // end plot item
        ImPlot::EndItem();
    }
}

} // namespace MyImPlot
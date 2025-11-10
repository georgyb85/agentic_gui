#include "HistogramWindow.h"
#include "TimeSeriesWindow.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <immintrin.h>  // For SIMD intrinsics
#include <chrono>       // For performance timing
#include <arrow/scalar.h>
#include <arrow/type.h>
#include <arrow/array.h> // For array types
#include <functional>

HistogramWindow::HistogramWindow()
    : m_isVisible(false)
    , m_isDocked(false)
    , m_hasError(false)
    , m_dataSource(nullptr)
    , m_currentColumnIndex(0)
    , m_cachedDataSize(0)
    , m_cachedDataHash(0)
    , m_lastComputeDuration(0.0)
{
}

void HistogramWindow::Draw() {
    if (!m_isVisible) {
        return;
    }

    // Set window size only on first use, then let user control position/size freely
    ImGui::SetNextWindowSize(ImVec2(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT), ImGuiCond_FirstUseEver);

    bool windowOpen = true;
    // Keep window title constant so ImGui treats it as the same window
    const char* windowTitle = "Histogram";

    if (ImGui::Begin(windowTitle, &windowOpen)) {
        // Show current indicator inside the window instead of in title
        if (!m_currentIndicator.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Indicator: %s", m_currentIndicator.c_str());
            ImGui::Separator();
        }
        m_isDocked = false;

        if (m_hasError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::Text("Error: %s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        DrawControls();
        ImGui::Separator();

        ImVec2 contentRegion = ImGui::GetContentRegionAvail();
        float contentHeight = contentRegion.y - STATUS_BAR_HEIGHT - 5.0f;

        if (!m_currentIndicator.empty()) {
            auto& settings = GetCurrentSettings();
            
            if (settings.histogramDirty && IsDataValid()) {
                ComputeHistogram();
            }
            if (settings.statisticsDirty && IsDataValid()) {
                ComputeStatistics();
            }

            float plotWidth = settings.showStatistics ?
                contentRegion.x - STATISTICS_WIDTH - 10.0f : contentRegion.x;

            if (IsDataValid() && !settings.binCounts.empty()) {
                ImGui::BeginChild("HistogramPlot", ImVec2(plotWidth, contentHeight), true);
                DrawHistogramPlot();
                ImGui::EndChild();

                if (settings.showStatistics) {
                    ImGui::SameLine();
                    ImGui::BeginChild("Statistics", ImVec2(STATISTICS_WIDTH, contentHeight), true);
                    DrawStatistics();
                    ImGui::EndChild();
                }
            } else {
                ImGui::BeginChild("PlaceholderArea", ImVec2(0, contentHeight), true);
                ImVec2 textSize = ImGui::CalcTextSize("Computing histogram...");
                ImVec2 windowSize = ImGui::GetWindowSize();
                ImGui::SetCursorPos(ImVec2(
                    (windowSize.x - textSize.x) * 0.5f,
                    (windowSize.y - textSize.y) * 0.5f
                ));
                ImGui::Text("Computing histogram...");
                ImGui::EndChild();
            }
        }
        else {
            ImGui::BeginChild("PlaceholderArea", ImVec2(0, contentHeight), true);
            ImVec2 textSize = ImGui::CalcTextSize("No data selected");
            ImVec2 windowSize = ImGui::GetWindowSize();
            ImGui::SetCursorPos(ImVec2(
                (windowSize.x - textSize.x) * 0.5f,
                (windowSize.y - textSize.y) * 0.5f
            ));

            if (!IsDataValid()) {
                ImGui::Text("No data selected");
                ImGui::Text("Click on a column header in the Time Series window");
            }
            else {
                ImGui::Text("Computing histogram...");
            }
            ImGui::EndChild();
        }

        DrawStatusBar();
    }
    ImGui::End();

    if (!windowOpen) {
        m_isVisible = false;
    }
}

void HistogramWindow::SetDataSource(const TimeSeriesWindow* source) {
    if (m_dataSource != source) {
        m_dataSource = source;
        ClearHistogram();
    }
}

void HistogramWindow::UpdateHistogram(const std::string& indicatorName, size_t columnIndex) {
    if (m_currentIndicator != indicatorName || m_currentColumnIndex != columnIndex) {
        m_currentIndicator = indicatorName;
        m_currentColumnIndex = columnIndex;
        
        // Get or create settings for this indicator
        auto& settings = GetCurrentSettings();
        settings.histogramDirty = true;
        settings.statisticsDirty = true;
        
        m_hasError = false;
        m_errorMessage.clear();
    }
}

void HistogramWindow::ClearHistogram() {
    // Clear current indicator settings
    if (!m_currentIndicator.empty()) {
        auto& settings = GetCurrentSettings();
        settings.binEdges.clear();
        settings.binCounts.clear();
        settings.binCenters.clear();
        settings.lowerTailCount = 0.0;
        settings.upperTailCount = 0.0;
        settings.stats = {};
        settings.histogramDirty = true;
        settings.statisticsDirty = true;
    }
    
    m_currentIndicator.clear();
    m_currentColumnIndex = 0;
    m_cachedIndicatorName.clear();
    m_cachedDataSize = 0;
    m_cachedDataHash = 0;
    m_hasError = false;
    m_errorMessage.clear();
}

void HistogramWindow::SetBinCount(int binCount) {
    if (m_currentIndicator.empty()) return;
    
    auto& settings = GetCurrentSettings();
    int clampedBinCount = std::clamp(binCount, MIN_BIN_COUNT, MAX_BIN_COUNT);
    if (settings.binCount != clampedBinCount) {
        settings.binCount = clampedBinCount;
        settings.histogramDirty = true;
    }
}

int HistogramWindow::GetBinCount() const {
    if (m_currentIndicator.empty()) return DEFAULT_BIN_COUNT;
    
    auto it = m_indicatorSettings.find(m_currentIndicator);
    if (it != m_indicatorSettings.end()) {
        return it->second.binCount;
    }
    return DEFAULT_BIN_COUNT;
}

void HistogramWindow::DrawControls() {
    if (m_currentIndicator.empty()) {
        ImGui::Text("No indicator selected");
        return;
    }
    
    auto& settings = GetCurrentSettings();
    
    ImGui::Text("Bins:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    int binCount = settings.binCount;
    if (ImGui::SliderInt("##BinCount", &binCount, MIN_BIN_COUNT, MAX_BIN_COUNT)) {
        settings.binCount = std::clamp(binCount, MIN_BIN_COUNT, MAX_BIN_COUNT);
        settings.histogramDirty = true;
    }

    ImGui::SameLine();
    bool autoRangeState = settings.autoRange;
    if (ImGui::Checkbox("Auto Range", &autoRangeState)) {
        // When switching to manual range, ensure data bounds are available and initialize range
        if (!autoRangeState && settings.autoRange && IsDataValid()) { // Switching from auto to manual
            UpdateDataBounds();
            
            // Get fresh reference after UpdateDataBounds (in case map was modified)
            auto& freshSettings = GetCurrentSettings();
            
            // Always initialize manual range to data bounds when switching from auto to manual
            if (freshSettings.hasDataBounds && freshSettings.dataMax > freshSettings.dataMin) {
                freshSettings.manualMin = freshSettings.dataMin;
                freshSettings.manualMax = freshSettings.dataMax;
                freshSettings.minRangePercent = 0.0f;
                freshSettings.maxRangePercent = 100.0f;
            } else {
                // Fallback values if no data bounds available
                freshSettings.manualMin = 0.0f;
                freshSettings.manualMax = 100.0f;
                freshSettings.minRangePercent = 0.0f;
                freshSettings.maxRangePercent = 100.0f;
            }
            freshSettings.autoRange = autoRangeState;
            freshSettings.histogramDirty = true;
        } else {
            // Simple state change
            settings.autoRange = autoRangeState;
            settings.histogramDirty = true;
        }
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Normalize", &settings.normalizeHistogram)) {
        settings.histogramDirty = true;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Show Stats", &settings.showStatistics);

    ImGui::SameLine();
    if (ImGui::Checkbox("Show Tails", &settings.showTails)) {
        settings.histogramDirty = true;
    }

    if (!settings.autoRange) {
        // Numeric range inputs with safety checks
        ImGui::Text("Range:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        
        // Create local copies to avoid potential reference invalidation
        float tempMin = settings.manualMin;
        float tempMax = settings.manualMax;
        
        // Fix: Remove ImGuiInputTextFlags_EnterReturnsTrue - it conflicts with InputFloat
        if (ImGui::InputFloat("##MinRange", &tempMin, 0.0f, 0.0f, "%.2f")) {
            settings.manualMin = tempMin;
            ConstrainManualRange();
            settings.histogramDirty = true;
        }
        ImGui::SameLine();
        ImGui::Text("to");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputFloat("##MaxRange", &tempMax, 0.0f, 0.0f, "%.2f")) {
            settings.manualMax = tempMax;
            ConstrainManualRange();
            settings.histogramDirty = true;
        }

        // Percentage sliders
        if (settings.hasDataBounds && settings.dataMax > settings.dataMin) {
            ImGui::Text("Range %%:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            float minPercent = settings.minRangePercent;
            if (ImGui::SliderFloat("##MinRangePercent", &minPercent, 0.0f, 100.0f, "%.1f%%")) {
                settings.minRangePercent = std::clamp(minPercent, 0.0f, settings.maxRangePercent - 0.1f);
                UpdateValuesFromPercentage();
                settings.histogramDirty = true;
            }
            ImGui::SameLine();
            ImGui::Text("to");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            float maxPercent = settings.maxRangePercent;
            if (ImGui::SliderFloat("##MaxRangePercent", &maxPercent, 0.0f, 100.0f, "%.1f%%")) {
                settings.maxRangePercent = std::clamp(maxPercent, settings.minRangePercent + 0.1f, 100.0f);
                UpdateValuesFromPercentage();
                settings.histogramDirty = true;
            }

            // Show data bounds info
            ImGui::Text("Data bounds: %.2f to %.2f", settings.dataMin, settings.dataMax);
        } else if (!settings.hasDataBounds) {
            ImGui::Text("No data bounds available");
        }
    }
}

void HistogramWindow::DrawHistogramPlot() {
    if (m_currentIndicator.empty()) {
        ImGui::Text("No indicator selected");
        return;
    }
    
    auto& settings = GetCurrentSettings();
    if (settings.binCounts.empty() || settings.binCenters.empty()) {
        ImGui::Text("No histogram data available");
        return;
    }

    std::string plotTitle = m_currentIndicator;
    if (settings.normalizeHistogram) {
        plotTitle += " (Normalized)";
    }
    if (settings.showTails && !settings.autoRange) {
        plotTitle += " (with tails)";
    }

    if (ImPlot::BeginPlot(plotTitle.c_str(), ImVec2(-1, -1))) {
        // Calculate plot range considering tails
        double plotMinX = settings.binEdges.front();
        double plotMaxX = settings.binEdges.back();
        double maxCount = *std::max_element(settings.binCounts.begin(), settings.binCounts.end());
        
        if (settings.showTails && !settings.autoRange) {
            // Extend plot range to show tail bars
            double range = plotMaxX - plotMinX;
            double tailBarWidth = range * 0.1; // Tail bars are 10% of the range width
            plotMinX -= tailBarWidth * 2;
            plotMaxX += tailBarWidth * 2;
            
            // Update max count considering tails
            maxCount = std::max({maxCount, settings.lowerTailCount, settings.upperTailCount});
        }

        ImPlot::SetupAxisLimits(ImAxis_X1, plotMinX, plotMaxX, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, maxCount * 1.1, ImGuiCond_Always);

        ImPlot::SetupAxis(ImAxis_X1, m_currentIndicator.c_str());
        ImPlot::SetupAxis(ImAxis_Y1, settings.normalizeHistogram ? "Frequency" : "Count");

        double binWidth = (settings.binEdges.size() > 1) ?
            (settings.binEdges[1] - settings.binEdges[0]) : 1.0;

        // Draw main histogram bars
        ImPlot::PlotBars("##Histogram", settings.binCenters.data(), settings.binCounts.data(),
            static_cast<int>(settings.binCounts.size()), binWidth * 0.9);

        // Draw tail bars if enabled
        if (settings.showTails && !settings.autoRange && (settings.lowerTailCount > 0.0 || settings.upperTailCount > 0.0)) {
            double range = settings.binEdges.back() - settings.binEdges.front();
            double tailBarWidth = range * 0.08;
            
            // Lower tail bar (values < minVal)
            if (settings.lowerTailCount > 0.0) {
                double lowerTailX = settings.binEdges.front() - range * 0.15;
                double lowerTailXArray[1] = { lowerTailX };
                double lowerTailCountArray[1] = { settings.lowerTailCount };
                
                ImPlot::PushStyleColor(ImPlotCol_Fill, ImVec4(0.8f, 0.4f, 0.4f, 0.8f)); // Red-ish
                ImPlot::PlotBars("Lower Tail", lowerTailXArray, lowerTailCountArray, 1, tailBarWidth);
                ImPlot::PopStyleColor();
            }
            
            // Upper tail bar (values > maxVal)
            if (settings.upperTailCount > 0.0) {
                double upperTailX = settings.binEdges.back() + range * 0.15;
                double upperTailXArray[1] = { upperTailX };
                double upperTailCountArray[1] = { settings.upperTailCount };
                
                ImPlot::PushStyleColor(ImPlotCol_Fill, ImVec4(0.4f, 0.4f, 0.8f, 0.8f)); // Blue-ish
                ImPlot::PlotBars("Upper Tail", upperTailXArray, upperTailCountArray, 1, tailBarWidth);
                ImPlot::PopStyleColor();
            }
        }

        ImPlot::EndPlot();
    }
}

void HistogramWindow::DrawStatistics() {
    ImGui::Text("Statistics");
    ImGui::Separator();

    if (m_currentIndicator.empty()) {
        ImGui::Text("No indicator selected");
        return;
    }

    auto& settings = GetCurrentSettings();
    if (settings.stats.validSamples == 0) {
        ImGui::Text("No valid data");
        return;
    }

    ImGui::Text("Mean: %.3f", settings.stats.mean);
    ImGui::Text("Median: %.3f", settings.stats.median);
    ImGui::Text("Std Dev: %.3f", settings.stats.stdDev);
    ImGui::Text("Min: %.3f", settings.stats.min);
    ImGui::Text("Max: %.3f", settings.stats.max);
    ImGui::Separator();
    ImGui::Text("Skewness: %.3f", settings.stats.skewness);
    ImGui::Text("Kurtosis: %.3f", settings.stats.kurtosis);
    ImGui::Separator();
    ImGui::Text("Samples: %zu/%zu", settings.stats.validSamples, settings.stats.totalSamples);
    if (settings.stats.totalSamples > settings.stats.validSamples) {
        ImGui::Text("NaN values: %zu", settings.stats.totalSamples - settings.stats.validSamples);
    }
    
    // Show tail information when enabled
    if (settings.showTails && !settings.autoRange) {
        ImGui::Separator();
        ImGui::Text("Tail counts:");
        if (settings.normalizeHistogram) {
            ImGui::Text("Lower: %.4f", settings.lowerTailCount);
            ImGui::Text("Upper: %.4f", settings.upperTailCount);
        } else {
            ImGui::Text("Lower: %.0f", settings.lowerTailCount);
            ImGui::Text("Upper: %.0f", settings.upperTailCount);
        }
        ImGui::Text("Total tails: %.0f", settings.lowerTailCount + settings.upperTailCount);
    }
    
    ImGui::Separator();
    ImGui::Text("Compute time:");
    ImGui::Text("%.2f ms", m_lastComputeDuration);
}

void HistogramWindow::DrawStatusBar() {
    ImGui::Separator();
    if (!m_currentIndicator.empty()) {
        auto& settings = GetCurrentSettings();
        ImGui::Text("%s | %d bins", m_currentIndicator.c_str(), settings.binCount);
        if (settings.stats.validSamples > 0) {
            ImGui::SameLine();
            ImGui::Text("| %zu samples", settings.stats.validSamples);
        }
    }
    else {
        ImGui::Text("No indicator selected");
    }
}

void HistogramWindow::ComputeHistogram() {
    auto startTime = std::chrono::steady_clock::now();

    if (!IsDataValid() || m_currentIndicator.empty()) {
        return;
    }

    // Update data bounds first
    UpdateDataBounds();

    auto& settings = GetCurrentSettings();

    const chronosflow::AnalyticsDataFrame* dataFrame = m_dataSource->GetDataFrame();
    auto table = dataFrame->get_cpu_table();
    auto column = table->column(static_cast<int>(m_currentColumnIndex));
    size_t dataSize = static_cast<size_t>(column->length());

    std::vector<float> validData;
    validData.reserve(dataSize);

    // Use the original, syntactically correct GetScalar loop to gather data
    for (int64_t i = 0; i < column->length(); ++i) {
        auto scalar_result = column->GetScalar(i);
        if (scalar_result.ok()) {
            auto scalar = scalar_result.ValueOrDie();
            if (scalar->is_valid) {
                double value = 0.0;
                // This block handles multiple numeric types
                if (scalar->type->id() == arrow::Type::DOUBLE) {
                    value = std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
                }
                else if (scalar->type->id() == arrow::Type::INT64) {
                    value = static_cast<double>(std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value);
                }
                else if (scalar->type->id() == arrow::Type::FLOAT) {
                    value = static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value);
                }
                else {
                    continue; // Skip non-numeric columns
                }

                float floatValue = static_cast<float>(value);
                if (std::isfinite(floatValue)) {
                    validData.push_back(floatValue);
                }
            }
        }
    }

    if (validData.empty()) {
        settings.binEdges.clear();
        settings.binCounts.clear();
        settings.binCenters.clear();
        settings.lowerTailCount = 0.0;
        settings.upperTailCount = 0.0;
        m_hasError = true;
        m_errorMessage = "No valid data points found for this indicator.";
        settings.histogramDirty = false; // Mark as clean
        return;
    }

    float minVal, maxVal;
    if (settings.autoRange) {
        auto [computedMin, computedMax] = ComputeDataRange(validData.data(), validData.size());
        minVal = computedMin;
        maxVal = computedMax;
    }
    else {
        minVal = settings.manualMin;
        maxVal = settings.manualMax;
    }

    if (maxVal - minVal < std::numeric_limits<float>::epsilon()) {
        minVal -= 0.5f;
        maxVal += 0.5f;
    }

    const double range = maxVal - minVal;
    const double binWidth = (range > 0) ? (range / settings.binCount) : 1.0;

    settings.binEdges.resize(settings.binCount + 1);
    for (int i = 0; i <= settings.binCount; ++i) {
        settings.binEdges[i] = static_cast<double>(minVal) + i * binWidth;
    }
    settings.binEdges[settings.binCount] = static_cast<double>(maxVal);

    settings.binCounts.assign(settings.binCount, 0.0);
    settings.lowerTailCount = 0.0;
    settings.upperTailCount = 0.0;

    // Robust binning loop with tail aggregation
    for (float value : validData) {
        if (settings.showTails && !settings.autoRange) {
            // When showing tails, aggregate values outside manual range
            if (value < minVal) {
                settings.lowerTailCount++;
                continue;
            }
            else if (value > maxVal) {
                settings.upperTailCount++;
                continue;
            }
        }
        
        if (value >= minVal && value <= maxVal) {
            int binIndex = static_cast<int>((value - minVal) / binWidth);
            // This robustly handles the case where value == maxVal
            if (binIndex >= settings.binCount) {
                binIndex = settings.binCount - 1;
            }
            settings.binCounts[binIndex]++;
        }
    }

    if (settings.normalizeHistogram && !validData.empty()) {
        double totalCount = static_cast<double>(validData.size());
        if (totalCount > 0) {
            for (double& count : settings.binCounts) {
                count /= totalCount;
            }
            // Normalize tail counts too
            settings.lowerTailCount /= totalCount;
            settings.upperTailCount /= totalCount;
        }
    }

    settings.binCenters.resize(settings.binCount);
    for (int i = 0; i < settings.binCount; ++i) {
        settings.binCenters[i] = settings.binEdges[i] + binWidth * 0.5;
    }

    m_hasError = false;
    m_errorMessage.clear();
    settings.histogramDirty = false;
    settings.statisticsDirty = true;

    auto endTime = std::chrono::steady_clock::now();
    m_lastComputeDuration = std::chrono::duration<double, std::milli>(endTime - startTime).count();
}

void HistogramWindow::ComputeStatistics() {
    if (!IsDataValid() || m_currentIndicator.empty()) {
        return;
    }

    auto& settings = GetCurrentSettings();

    const chronosflow::AnalyticsDataFrame* dataFrame = m_dataSource->GetDataFrame();
    auto table = dataFrame->get_cpu_table();
    auto column = table->column(static_cast<int>(m_currentColumnIndex));
    size_t dataSize = static_cast<size_t>(column->length());

    std::vector<float> validData;
    validData.reserve(dataSize);

    // Use the same robust data gathering logic
    for (int64_t i = 0; i < column->length(); ++i) {
        auto scalar_result = column->GetScalar(i);
        if (scalar_result.ok()) {
            auto scalar = scalar_result.ValueOrDie();
            if (scalar->is_valid) {
                double value = 0.0;
                if (scalar->type->id() == arrow::Type::DOUBLE) {
                    value = std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
                }
                else if (scalar->type->id() == arrow::Type::INT64) {
                    value = static_cast<double>(std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value);
                }
                else if (scalar->type->id() == arrow::Type::FLOAT) {
                    value = static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value);
                }
                else {
                    continue;
                }

                float floatValue = static_cast<float>(value);
                if (std::isfinite(floatValue)) {
                    validData.push_back(floatValue);
                }
            }
        }
    }

    settings.stats.totalSamples = dataSize;
    settings.stats.validSamples = validData.size();

    if (validData.empty()) {
        settings.stats = {}; // Clear all stats
        settings.stats.totalSamples = dataSize; // Keep total sample count
        settings.statisticsDirty = false;
        return;
    }

    ComputeBasicStatistics(validData);

    std::vector<float> sortedData = validData;
    std::sort(sortedData.begin(), sortedData.end());

    size_t medianIndex = sortedData.size() / 2;
    if (sortedData.size() % 2 == 0) {
        settings.stats.median = (sortedData[medianIndex - 1] + sortedData[medianIndex]) / 2.0f;
    }
    else {
        settings.stats.median = sortedData[medianIndex];
    }

    ComputeHigherOrderMoments(validData);

    settings.statisticsDirty = false;
}

void HistogramWindow::ComputeBasicStatistics(const std::vector<float>& validData) {
    if (m_currentIndicator.empty()) return;
    
    auto& settings = GetCurrentSettings();
    if (validData.empty()) {
        settings.stats.mean = settings.stats.stdDev = settings.stats.min = settings.stats.max = NAN;
        return;
    }

    double sum = 0.0;
    float minVal = validData[0];
    float maxVal = validData[0];

    for (float value : validData) {
        sum += value;
        minVal = std::min(minVal, value);
        maxVal = std::max(maxVal, value);
    }

    settings.stats.mean = static_cast<float>(sum / validData.size());
    settings.stats.min = minVal;
    settings.stats.max = maxVal;

    double variance = 0.0;
    for (float value : validData) {
        double diff = static_cast<double>(value) - settings.stats.mean;
        variance += diff * diff;
    }

    settings.stats.stdDev = static_cast<float>(std::sqrt(variance / validData.size()));
}

std::pair<float, float> HistogramWindow::ComputeDataRange(const float* data, size_t size) {
    if (!data || size == 0) {
        return { 0.0f, 0.0f };
    }

    if (size < 8) {
        float minVal = data[0];
        float maxVal = data[0];

        for (size_t i = 1; i < size; ++i) {
            minVal = std::min(minVal, data[i]);
            maxVal = std::max(maxVal, data[i]);
        }

        return { minVal, maxVal };
    }

    size_t simdSize = size - (size % 8);
    float minVal = data[0];
    float maxVal = data[0];

    __m256 minVec = _mm256_set1_ps(data[0]);
    __m256 maxVec = _mm256_set1_ps(data[0]);

    if (reinterpret_cast<uintptr_t>(data) % 32 == 0) {
        for (size_t i = 0; i < simdSize; i += 8) {
            __m256 dataVec = _mm256_load_ps(data + i);
            minVec = _mm256_min_ps(minVec, dataVec);
            maxVec = _mm256_max_ps(maxVec, dataVec);
        }
    }
    else {
        for (size_t i = 0; i < simdSize; i += 8) {
            __m256 dataVec = _mm256_loadu_ps(data + i);
            minVec = _mm256_min_ps(minVec, dataVec);
            maxVec = _mm256_max_ps(maxVec, dataVec);
        }
    }

    float minArr[8], maxArr[8];
    _mm256_storeu_ps(minArr, minVec);
    _mm256_storeu_ps(maxArr, maxVec);

    for (int i = 0; i < 8; ++i) {
        minVal = std::min(minVal, minArr[i]);
        maxVal = std::max(maxVal, maxArr[i]);
    }

    for (size_t i = simdSize; i < size; ++i) {
        minVal = std::min(minVal, data[i]);
        maxVal = std::max(maxVal, data[i]);
    }

    return { minVal, maxVal };
}

// UpdateBinEdges function was removed as it is now redundant.

bool HistogramWindow::IsDataValid() const {
    if (!m_dataSource || !m_dataSource->HasData()) {
        return false;
    }

    if (m_currentIndicator.empty()) {
        return false;
    }

    const chronosflow::AnalyticsDataFrame* dataFrame = m_dataSource->GetDataFrame();
    if (!dataFrame) {
        return false;
    }

    auto table = dataFrame->get_cpu_table();
    if (!table || m_currentColumnIndex >= static_cast<size_t>(table->num_columns())) {
        return false;
    }

    return true;
}

size_t HistogramWindow::ComputeDataHash(const float* data, size_t size) const {
    if (!data || size == 0) {
        return 0;
    }

    size_t hash = size;
    hash ^= std::hash<float>{}(data[0]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    if (size > 1) {
        hash ^= std::hash<float>{}(data[size / 2]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(data[size - 1]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }

    return hash;
}

void HistogramWindow::ComputeHigherOrderMoments(const std::vector<float>& validData) {
    if (m_currentIndicator.empty()) return;
    
    auto& settings = GetCurrentSettings();
    if (validData.size() < 3 || settings.stats.stdDev <= 1e-6f) {
        settings.stats.skewness = NAN;
        settings.stats.kurtosis = NAN;
        return;
    }

    size_t n = validData.size();
    double mean = settings.stats.mean;

    double m3 = 0.0;
    double m4 = 0.0;

    for (float value : validData) {
        double dev = static_cast<double>(value) - mean;
        double dev2 = dev * dev;
        m3 += dev2 * dev;
        m4 += dev2 * dev2;
    }

    m3 /= n;
    m4 /= n;

    double var = static_cast<double>(settings.stats.stdDev * settings.stats.stdDev);
    double var2 = var * var;

    settings.stats.skewness = static_cast<float>(m3 / (var * settings.stats.stdDev));
    settings.stats.kurtosis = static_cast<float>((m4 / var2) - 3.0);
}

void HistogramWindow::UpdateDataBounds() {
    if (!IsDataValid() || m_currentIndicator.empty()) {
        return;
    }

    auto& settings = GetCurrentSettings();

    const chronosflow::AnalyticsDataFrame* dataFrame = m_dataSource->GetDataFrame();
    auto table = dataFrame->get_cpu_table();
    auto column = table->column(static_cast<int>(m_currentColumnIndex));
    size_t dataSize = static_cast<size_t>(column->length());

    std::vector<float> validData;
    validData.reserve(dataSize);

    for (int64_t i = 0; i < column->length(); ++i) {
        auto scalar_result = column->GetScalar(i);
        if (scalar_result.ok()) {
            auto scalar = scalar_result.ValueOrDie();
            if (scalar->is_valid) {
                double value = 0.0;
                if (scalar->type->id() == arrow::Type::DOUBLE) {
                    value = std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
                }
                else if (scalar->type->id() == arrow::Type::INT64) {
                    value = static_cast<double>(std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value);
                }
                else if (scalar->type->id() == arrow::Type::FLOAT) {
                    value = static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value);
                }
                else {
                    continue;
                }

                float floatValue = static_cast<float>(value);
                if (std::isfinite(floatValue)) {
                    validData.push_back(floatValue);
                }
            }
        }
    }

    if (!validData.empty()) {
        auto [minVal, maxVal] = ComputeDataRange(validData.data(), validData.size());
        settings.dataMin = minVal;
        settings.dataMax = maxVal;
        settings.hasDataBounds = true;
        
        // Initialize manual range to data bounds if not set
        if (!settings.autoRange && (settings.manualMin < settings.dataMin || settings.manualMax > settings.dataMax)) {
            settings.manualMin = std::max(settings.manualMin, settings.dataMin);
            settings.manualMax = std::min(settings.manualMax, settings.dataMax);
        }
        
        UpdatePercentageFromValues();
    } else {
        settings.hasDataBounds = false;
    }
}

void HistogramWindow::ConstrainManualRange() {
    if (m_currentIndicator.empty()) return;
    
    auto& settings = GetCurrentSettings();
    if (!settings.hasDataBounds || settings.dataMax <= settings.dataMin) {
        // If no valid data bounds, try to update them
        if (IsDataValid()) {
            UpdateDataBounds();
        }
        if (!settings.hasDataBounds || settings.dataMax <= settings.dataMin) {
            return; // Still no valid bounds, skip constraining
        }
    }
    
    // Constrain manual values to data bounds
    float oldMin = settings.manualMin;
    float oldMax = settings.manualMax;
    
    settings.manualMin = std::clamp(settings.manualMin, settings.dataMin, settings.dataMax);
    settings.manualMax = std::clamp(settings.manualMax, settings.dataMin, settings.dataMax);
    
    // Ensure min < max
    if (settings.manualMin >= settings.manualMax) {
        float minGap = (settings.dataMax - settings.dataMin) * 0.01f;
        if (minGap <= 0.0f) minGap = 1.0f; // Fallback for edge case
        
        if (oldMin != settings.manualMin) {
            settings.manualMax = std::min(settings.manualMin + minGap, settings.dataMax);
        } else {
            settings.manualMin = std::max(settings.manualMax - minGap, settings.dataMin);
        }
    }
    
    UpdatePercentageFromValues(settings);
}

void HistogramWindow::UpdatePercentageFromValues() {
    if (m_currentIndicator.empty()) return;
    
    auto& settings = GetCurrentSettings();
    UpdatePercentageFromValues(settings);
}

void HistogramWindow::UpdatePercentageFromValues(IndicatorSettings& settings) {
    if (!settings.hasDataBounds || settings.dataMax <= settings.dataMin) {
        return;
    }
    
    float range = settings.dataMax - settings.dataMin;
    settings.minRangePercent = ((settings.manualMin - settings.dataMin) / range) * 100.0f;
    settings.maxRangePercent = ((settings.manualMax - settings.dataMin) / range) * 100.0f;
}

void HistogramWindow::UpdateValuesFromPercentage() {
    if (m_currentIndicator.empty()) return;
    
    auto& settings = GetCurrentSettings();
    if (!settings.hasDataBounds || settings.dataMax <= settings.dataMin) {
        return;
    }
    
    float range = settings.dataMax - settings.dataMin;
    if (range <= 0.0f) return; // Additional safety check
    
    settings.manualMin = settings.dataMin + (settings.minRangePercent / 100.0f) * range;
    settings.manualMax = settings.dataMin + (settings.maxRangePercent / 100.0f) * range;
    
    // Ensure the values are still within bounds (floating point precision issues)
    settings.manualMin = std::clamp(settings.manualMin, settings.dataMin, settings.dataMax);
    settings.manualMax = std::clamp(settings.manualMax, settings.dataMin, settings.dataMax);
}

IndicatorSettings& HistogramWindow::GetCurrentSettings() {
    if (m_currentIndicator.empty()) {
        // Return a temporary settings object for safety (though this shouldn't happen)
        static IndicatorSettings dummy;
        return dummy;
    }
    
    // Lazy initialization - create settings if they don't exist
    auto it = m_indicatorSettings.find(m_currentIndicator);
    if (it == m_indicatorSettings.end()) {
        InitializeIndicatorSettings(m_currentIndicator);
        it = m_indicatorSettings.find(m_currentIndicator);
        if (it == m_indicatorSettings.end()) {
            static IndicatorSettings errorDummy;
            return errorDummy;
        }
    }
    
    return it->second;
}

void HistogramWindow::InitializeIndicatorSettings(const std::string& indicatorName) {
    if (indicatorName.empty()) return;
    
    IndicatorSettings& settings = m_indicatorSettings[indicatorName];
    
    // Settings are already initialized with defaults in the struct
    // But we need to reserve space for vectors
    settings.binEdges.reserve(MAX_BIN_COUNT + 1);
    settings.binCounts.reserve(MAX_BIN_COUNT);
    settings.binCenters.reserve(MAX_BIN_COUNT);
    settings.isInitialized = true;
}

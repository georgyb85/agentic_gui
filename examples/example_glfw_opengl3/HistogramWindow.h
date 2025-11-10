#pragma once

#include "imgui.h"
#include "implot.h"
#include "TimeSeries.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <unordered_map>

// Forward declaration to avoid circular dependency
class TimeSeriesWindow;

// Statistics structure used by HistogramWindow
struct HistogramStats {
    float mean = 0.0f;               ///< Arithmetic mean
    float median = 0.0f;             ///< Median value
    float stdDev = 0.0f;             ///< Standard deviation
    float min = 0.0f;                ///< Minimum value
    float max = 0.0f;                ///< Maximum value
    float skewness = 0.0f;           ///< Skewness measure
    float kurtosis = 0.0f;           ///< Kurtosis measure
    size_t totalSamples = 0;         ///< Total number of samples
    size_t validSamples = 0;         ///< Number of valid (non-NaN) samples
};

// Per-indicator settings structure used by HistogramWindow
struct IndicatorSettings {
    // Histogram configuration
    int binCount = 40;                              ///< Number of histogram bins (using literal instead of DEFAULT_BIN_COUNT)
    bool autoRange = true;                          ///< Use automatic range detection
    float manualMin = 0.0f;                         ///< Manual minimum value (when not auto)
    float manualMax = 100.0f;                       ///< Manual maximum value (when not auto)
    bool showStatistics = true;                     ///< Show statistics panel
    bool normalizeHistogram = false;                ///< Normalize histogram counts
    bool showTails = false;                         ///< Show tails aggregation
    
    // Range percentage controls
    float minRangePercent = 0.0f;                   ///< Minimum range as percentage (0-100)
    float maxRangePercent = 100.0f;                 ///< Maximum range as percentage (0-100)
    
    // Data range bounds (for constraining manual inputs)
    float dataMin = 0.0f;                           ///< Actual minimum value in data
    float dataMax = 100.0f;                         ///< Actual maximum value in data
    bool hasDataBounds = false;                     ///< Whether data bounds are valid
    bool isInitialized = false;                     ///< Whether this indicator has been initialized
    
    // Computed histogram data
    std::vector<double> binEdges;                   ///< Bin edge positions
    std::vector<double> binCounts;                  ///< Bin count values
    std::vector<double> binCenters;                 ///< Bin center positions for plotting
    double lowerTailCount = 0.0;                    ///< Count of values below manual range
    double upperTailCount = 0.0;                    ///< Count of values above manual range
    
    // Cached statistics
    HistogramStats stats = {};                      ///< Statistical measures
    bool histogramDirty = true;                     ///< Flag indicating histogram needs recomputation
    bool statisticsDirty = true;                    ///< Flag indicating statistics need recomputation
};

/**
 * HistogramWindow - A dockable panel that displays statistical distribution histograms
 * for selected time series indicators. Integrates with TimeSeriesWindow to provide
 * real-time histogram visualization with configurable binning and statistics.
 */
class HistogramWindow {
public:
    /**
     * Constructor - Initializes histogram window with default settings
     */
    HistogramWindow();
    
    /**
     * Destructor - Default destructor
     */
    ~HistogramWindow() = default;
    
    // Core window interface (following established patterns)
    
    /**
     * Draw - Main rendering method, called each frame
     * Handles window visibility, docking, and all UI rendering
     */
    void Draw();
    
    /**
     * IsVisible - Check if window is currently visible
     * @return true if window is visible, false otherwise
     */
    bool IsVisible() const { return m_isVisible; }
    
    /**
     * SetVisible - Set window visibility state
     * @param visible - true to show window, false to hide
     */
    void SetVisible(bool visible) { m_isVisible = visible; }
    
    // Data interface
    
    /**
     * SetDataSource - Connect to a TimeSeriesWindow data source
     * @param source - Pointer to TimeSeriesWindow providing data
     */
    void SetDataSource(const TimeSeriesWindow* source);
    
    /**
     * UpdateHistogram - Update histogram for specified indicator
     * @param indicatorName - Name of the indicator to display
     * @param columnIndex - Index of the column in TimeSeries data
     */
    void UpdateHistogram(const std::string& indicatorName, size_t columnIndex);
    
    /**
     * ClearHistogram - Clear current histogram data and reset state
     */
    void ClearHistogram();
    
    // Configuration
    
    /**
     * SetBinCount - Set number of histogram bins
     * @param binCount - Number of bins (clamped to MIN_BIN_COUNT - MAX_BIN_COUNT)
     */
    void SetBinCount(int binCount);
    
    /**
     * GetBinCount - Get current number of histogram bins
     * @return Current bin count
     */
    int GetBinCount() const;

private:
    // UI rendering methods
    
    /**
     * DrawControls - Render the controls section (bin count, range, options)
     */
    void DrawControls();
    
    /**
     * DrawHistogramPlot - Render the main histogram plot using ImPlot
     */
    void DrawHistogramPlot();
    
    /**
     * DrawStatistics - Render the statistics panel with computed stats
     */
    void DrawStatistics();
    
    /**
     * DrawStatusBar - Render the status bar with current state info
     */
    void DrawStatusBar();
    
    // Histogram computation
    
    /**
     * ComputeHistogram - Calculate histogram bins and counts from current data
     */
    void ComputeHistogram();
    
    /**
     * ComputeStatistics - Calculate statistical measures for current data
     */
    void ComputeStatistics();
    
    // Helper methods
    
    /**
     * ComputeDataRange - Calculate min/max range of valid (non-NaN) data
     * @param data - Pointer to data array
     * @param size - Size of data array
     * @return Pair of (min, max) values
     */
    std::pair<float, float> ComputeDataRange(const float* data, size_t size);
    
    /**
     * UpdateBinEdges - Calculate bin edge positions based on current range and bin count
     */
    void UpdateBinEdges();
    
    /**
     * IsDataValid - Check if current data source and selection are valid
     * @return true if data is valid for histogram computation
     */
    bool IsDataValid() const;
    
    /**
     * ComputeDataHash - Calculate simple hash of data for cache validation
     * @param data - Pointer to data array
     * @param size - Size of data array
     * @return Hash value for cache comparison
     */
    size_t ComputeDataHash(const float* data, size_t size) const;
    
    /**
     * ComputeHigherOrderMoments - Calculate skewness and kurtosis
     * @param validData - Vector of valid (non-NaN) data values
     */
    void ComputeHigherOrderMoments(const std::vector<float>& validData);
    
    /**
     * ComputeBasicStatistics - Calculate basic statistics (mean, std dev, min, max)
     * @param validData - Vector of valid (non-NaN) data values
     */
    void ComputeBasicStatistics(const std::vector<float>& validData);
    
    /**
     * UpdateDataBounds - Update the data min/max bounds from current data
     */
    void UpdateDataBounds();
    
    /**
     * ConstrainManualRange - Constrain manual range values to data bounds
     */
    void ConstrainManualRange();
    
    /**
     * UpdatePercentageFromValues - Update percentage sliders from numeric values
     */
    void UpdatePercentageFromValues();
    
    /**
     * UpdatePercentageFromValues - Update percentage sliders from numeric values (with settings reference)
     * @param settings - Reference to settings to update
     */
    void UpdatePercentageFromValues(IndicatorSettings& settings);
    
    /**
     * UpdateValuesFromPercentage - Update numeric values from percentage sliders
     */
    void UpdateValuesFromPercentage();
    
    /**
     * GetCurrentSettings - Get or create settings for current indicator
     * @return Reference to current indicator settings
     */
    IndicatorSettings& GetCurrentSettings();
    
    /**
     * InitializeIndicatorSettings - Initialize settings for a new indicator
     * @param indicatorName - Name of the indicator to initialize
     */
    void InitializeIndicatorSettings(const std::string& indicatorName);

private:
    // Window state
    bool m_isVisible;                    ///< Window visibility state
    bool m_isDocked;                     ///< Whether window is currently docked
    bool m_hasError;                     ///< Error state flag
    std::string m_errorMessage;          ///< Current error message
    
    // Data source
    const TimeSeriesWindow* m_dataSource;    ///< Pointer to data source window
    std::string m_currentIndicator;          ///< Currently selected indicator name
    size_t m_currentColumnIndex;             ///< Currently selected column index
    
    // Per-indicator settings storage
    std::unordered_map<std::string, IndicatorSettings> m_indicatorSettings;
    
    // Performance optimization
    std::string m_cachedIndicatorName;   ///< Cached indicator name for validation
    size_t m_cachedDataSize;             ///< Cached data size for validation
    size_t m_cachedDataHash;             ///< Cached data hash for validation
    std::chrono::steady_clock::time_point m_lastComputeTime; ///< Last computation timestamp
    double m_lastComputeDuration;        ///< Last computation duration in milliseconds
    
    // UI layout constants
    static constexpr float CONTROLS_HEIGHT = 60.0f;        ///< Height of controls section
    static constexpr float STATISTICS_WIDTH = 200.0f;      ///< Width of statistics panel
    static constexpr float MIN_PLOT_HEIGHT = 200.0f;       ///< Minimum plot area height
    static constexpr float STATUS_BAR_HEIGHT = 25.0f;      ///< Height of status bar
    static constexpr int DEFAULT_BIN_COUNT = 40;           ///< Default number of bins
    static constexpr int MIN_BIN_COUNT = 5;                ///< Minimum allowed bins
    static constexpr int MAX_BIN_COUNT = 200;              ///< Maximum allowed bins
    static constexpr float DEFAULT_WINDOW_WIDTH = 500.0f;  ///< Default window width
    static constexpr float DEFAULT_WINDOW_HEIGHT = 400.0f; ///< Default window height
};
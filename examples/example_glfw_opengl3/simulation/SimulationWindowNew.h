#pragma once

#include <memory>
#include <vector>
#include <string>

// Forward declarations
class TimeSeriesWindow;

namespace simulation {

// Forward declare types
struct SimulationRun;

// Initialize all simulation models (call once at startup)
void InitializeSimulationModels();

// New, clean SimulationWindow - just a thin coordinator
// Replaces the 3000-line monolithic version
class SimulationWindow {
public:
    SimulationWindow();
    ~SimulationWindow();
    
    // Window management
    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible);
    
    // Data source
    void SetTimeSeriesWindow(TimeSeriesWindow* tsWindow);
    
    // Access to results (for trade simulation)
    bool HasResults() const;
    const SimulationRun* GetLastResults() const;
    std::vector<std::string> GetRunNames() const;
    const SimulationRun* GetRunByIndex(int index) const;
    int GetRunCount() const;
    
private:
    // Use pImpl idiom to hide implementation details
    class Impl;
    std::unique_ptr<Impl> pImpl;
    
    // Window state
    bool m_isVisible;
    bool autoScrollResults = true;
    bool autoFitPlot = true;
};

} // namespace simulation

// For backward compatibility
using SimulationWindow = simulation::SimulationWindow;
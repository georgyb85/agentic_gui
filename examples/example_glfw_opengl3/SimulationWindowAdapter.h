#pragma once

// Adapter to use the new simulation architecture with the old interface
// This allows us to remove the 3000-line SimulationWindow.cpp

#include "simulation/SimulationWindowNew.h"

// Create an alias for backward compatibility
using SimulationWindow = simulation::SimulationWindow;
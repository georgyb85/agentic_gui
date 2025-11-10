#pragma once

#include <string>
#include <vector>

#include "simulation/SimulationTypes.h"
#include "stage1_metadata_writer.h"
#include "TradeSimulator.h"

namespace questdb {

struct ExportOptions {
    std::string host = "45.85.147.236";
    int port = 9009;
};

// Attempts to stream per-bar walk-forward predictions to QuestDB using ILP.
// Returns true on success; on failure `error` (if provided) receives a message.
bool ExportWalkforwardPredictions(const simulation::SimulationRun& run,
                                  const Stage1MetadataWriter::WalkforwardRecord& record,
                                  const ExportOptions& options = {},
                                  std::string* error = nullptr);

// Attempts to stream executed trade traces for a simulation run.
bool ExportTradingSimulation(const Stage1MetadataWriter::SimulationRecord& record,
                             const std::vector<ExecutedTrade>& trades,
                             const ExportOptions& options = {},
                             std::string* error = nullptr);

} // namespace questdb

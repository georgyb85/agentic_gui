#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>

#include "stage1_metadata_writer.h"
#include "simulation/SimulationTypes.h"
#include "simulation/XGBoostConfig.h"

class Stage1MetadataReader {
public:
    struct RunSummary {
        std::string run_id;
        std::string measurement;
        std::string status;
        std::string started_at;
        std::string completed_at;
    };

    struct RunPayload {
        std::string run_id;
        std::string dataset_id;
        std::string dataset_slug;
        std::string prediction_measurement;
        std::string target_column;
        std::vector<std::string> feature_columns;
        simulation::WalkForwardConfig walk_config;
        simulation::XGBoostConfig hyperparameters;
        std::string summary_metrics_json;
        std::chrono::system_clock::time_point started_at;
        std::chrono::system_clock::time_point completed_at;
        std::string status;
        std::vector<Stage1MetadataWriter::WalkforwardFoldRecord> folds;
    };

    static bool ListRunsForDataset(const std::string& dataset_id,
                                   std::vector<RunSummary>* runs,
                                   std::string* error);

    static bool LoadRunPayload(const std::string& run_id,
                               RunPayload* payload,
                               std::string* error);

private:
    static bool ParseWalkConfig(const std::string& json,
                                simulation::WalkForwardConfig* cfg);
    static bool ParseHyperparameters(const std::string& json,
                                     simulation::XGBoostConfig* config);
    static void ParseFoldJson(Stage1MetadataWriter::WalkforwardFoldRecord* record,
                              const std::string& thresholds_json,
                              const std::string& metrics_json);
};

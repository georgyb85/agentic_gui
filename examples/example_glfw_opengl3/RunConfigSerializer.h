#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "TradeSimulator.h"
#include "simulation/SimulationTypes.h"
#include "simulation/XGBoostConfig.h"
#include "simulation/PerformanceStressTests.h"

class RunConfigSerializer {
public:
    struct Snapshot {
        std::string modelType;
        std::string runName;
        std::string dataset;
        std::string description;

        std::vector<std::string> features;
        std::string target;
        std::string featureSchedule;
        bool hasFeatureSchedule = false;

        simulation::WalkForwardConfig walkForward;
        bool hasWalkForward = false;

        bool hasHyperparameters = false;
        std::string hyperparameterType;
        std::optional<simulation::XGBoostConfig> xgboost;

        bool hasTradeConfig = false;
        TradeSimulator::Config trade;

        bool hasStressConfig = false;
        simulation::StressTestConfig stress;
    };

    enum Section : uint32_t {
        SectionMetadata = 1u << 0,
        SectionFeatures = 1u << 1,
        SectionFeatureSchedule = 1u << 2,
        SectionWalkForward = 1u << 3,
        SectionHyperparameters = 1u << 4,
        SectionTrade = 1u << 5,
        SectionAll = 0xFFFFFFFFu
    };

    static std::string Serialize(const Snapshot& snapshot, uint32_t sections = SectionAll);
    static bool Deserialize(const std::string& text, Snapshot* snapshot, std::string* error = nullptr);
    static bool LooksLikeSerializedConfig(const std::string& text);
};

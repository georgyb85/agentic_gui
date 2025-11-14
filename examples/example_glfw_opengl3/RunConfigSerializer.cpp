#include "RunConfigSerializer.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace {

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void AppendKeyValue(std::ostringstream& oss, const std::string& key, const std::string& value) {
    if (value.empty()) {
        return;
    }
    oss << key << '=' << value << '\n';
}

void AppendBool(std::ostringstream& oss, const std::string& key, bool value) {
    oss << key << '=' << (value ? "true" : "false") << '\n';
}

template <typename T>
void AppendNumeric(std::ostringstream& oss, const std::string& key, T value, int precision = 6) {
    oss << key << '=';
    if (std::is_integral<T>::value) {
        oss << value;
    } else {
        oss.setf(std::ios::fixed, std::ios::floatfield);
        oss << std::setprecision(precision) << value;
        oss.unsetf(std::ios::floatfield);
    }
    oss << '\n';
}

void AppendFeatureCsv(std::vector<std::string>& features, const std::string& csv) {
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto trimmed = Trim(item);
        if (!trimmed.empty()) {
            features.push_back(trimmed);
        }
    }
}

std::string NormalizeKey(const std::string& key) {
    std::string lowered = ToLower(key);
    lowered.erase(std::remove_if(lowered.begin(), lowered.end(), [](char ch) {
        return ch == ' ' || ch == '_' || ch == '-';
    }), lowered.end());
    return lowered;
}

bool ParseBoolValue(const std::string& value, bool defaultValue, bool* out) {
    if (!out) return false;
    std::string lower = ToLower(value);
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "y") {
        *out = true;
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "n") {
        *out = false;
        return true;
    }
    *out = defaultValue;
    return false;
}

template <typename T>
bool ParseIntegral(const std::string& value, T* out) {
    if (!out) return false;
    try {
        long long parsed = std::stoll(value);
        *out = static_cast<T>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

template <typename T>
bool ParseFloating(const std::string& value, T* out) {
    if (!out) return false;
    try {
        double parsed = std::stod(value);
        *out = static_cast<T>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

const char* ThresholdChoiceToString(TradeSimulator::ThresholdChoice choice) {
    switch (choice) {
        case TradeSimulator::ThresholdChoice::OptimalROC:
            return "OptimalROC";
        case TradeSimulator::ThresholdChoice::Percentile:
            return "Percentile95_5";
        case TradeSimulator::ThresholdChoice::ZeroCrossover:
            return "ZeroCrossover";
    }
    return "OptimalROC";
}

TradeSimulator::ThresholdChoice ParseThresholdChoice(const std::string& value) {
    std::string lower = ToLower(value);
    if (lower.find("zero") != std::string::npos) {
        return TradeSimulator::ThresholdChoice::ZeroCrossover;
    }
    if (lower.find("percentile") != std::string::npos) {
        return TradeSimulator::ThresholdChoice::Percentile;
    }
    return TradeSimulator::ThresholdChoice::OptimalROC;
}

const char* ThresholdMethodToString(simulation::ThresholdMethod method) {
    switch (method) {
        case simulation::ThresholdMethod::Percentile95:
            return "Percentile95";
        case simulation::ThresholdMethod::OptimalROC:
            return "OptimalROC";
    }
    return "Percentile95";
}

simulation::ThresholdMethod ParseThresholdMethod(const std::string& value) {
    std::string lower = ToLower(value);
    if (lower.find("roc") != std::string::npos) {
        return simulation::ThresholdMethod::OptimalROC;
    }
    return simulation::ThresholdMethod::Percentile95;
}

bool HasPrintableContent(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
}

} // namespace

std::string RunConfigSerializer::Serialize(const Snapshot& snapshot, uint32_t sections) {
    std::ostringstream oss;
    oss << "# Stage1 RunConfig v1\n";

    auto hasMetadata = !snapshot.modelType.empty() || !snapshot.runName.empty() || !snapshot.dataset.empty();
    if (hasMetadata && (sections & SectionMetadata)) {
        oss << "[METADATA]\n";
        AppendKeyValue(oss, "model", snapshot.modelType);
        AppendKeyValue(oss, "run", snapshot.runName);
        AppendKeyValue(oss, "dataset", snapshot.dataset);
        AppendKeyValue(oss, "description", snapshot.description);
        oss << '\n';
    }

    if ((sections & SectionFeatures) &&
        (!snapshot.features.empty() || !snapshot.target.empty())) {
        oss << "[FEATURES]\n";
        AppendKeyValue(oss, "target", snapshot.target);
        for (const auto& feature : snapshot.features) {
            AppendKeyValue(oss, "feature", feature);
        }
        oss << '\n';
    }

    if ((sections & SectionFeatureSchedule) &&
        snapshot.hasFeatureSchedule && !snapshot.featureSchedule.empty()) {
        oss << "[FEATURE_SCHEDULE]\n" << snapshot.featureSchedule << "\n[/FEATURE_SCHEDULE]\n\n";
    }

    if ((sections & SectionWalkForward) && snapshot.hasWalkForward) {
        const auto& wf = snapshot.walkForward;
        oss << "[WALKFORWARD]\n";
        AppendNumeric(oss, "train_size", wf.train_size, 0);
        AppendNumeric(oss, "test_size", wf.test_size, 0);
        AppendNumeric(oss, "train_test_gap", wf.train_test_gap, 0);
        AppendNumeric(oss, "fold_step", wf.fold_step, 0);
        AppendNumeric(oss, "start_fold", wf.start_fold, 0);
        AppendNumeric(oss, "end_fold", wf.end_fold, 0);
        AppendNumeric(oss, "initial_offset", wf.initial_offset, 0);
        oss << '\n';
    }

    if ((sections & SectionHyperparameters) && snapshot.hasHyperparameters &&
        snapshot.hyperparameterType == "XGBoost" && snapshot.xgboost.has_value()) {
        const auto& cfg = snapshot.xgboost.value();
        oss << "[HYPERPARAMETERS]\n";
        AppendKeyValue(oss, "type", "XGBoost");
        AppendNumeric(oss, "learning_rate", cfg.learning_rate);
        AppendNumeric(oss, "max_depth", cfg.max_depth, 0);
        AppendNumeric(oss, "min_child_weight", cfg.min_child_weight);
        AppendNumeric(oss, "subsample", cfg.subsample);
        AppendNumeric(oss, "colsample_bytree", cfg.colsample_bytree);
        AppendNumeric(oss, "lambda", cfg.lambda);
        AppendNumeric(oss, "num_boost_round", cfg.num_boost_round, 0);
        AppendNumeric(oss, "early_stopping_rounds", cfg.early_stopping_rounds, 0);
        AppendNumeric(oss, "min_boost_rounds", cfg.min_boost_rounds, 0);
        AppendBool(oss, "force_minimum_training", cfg.force_minimum_training);
        AppendKeyValue(oss, "objective", cfg.objective);
        AppendNumeric(oss, "quantile_alpha", cfg.quantile_alpha);
        AppendKeyValue(oss, "tree_method", cfg.tree_method);
        AppendKeyValue(oss, "device", cfg.device);
        AppendNumeric(oss, "random_seed", cfg.random_seed, 0);
        AppendNumeric(oss, "val_split_ratio", cfg.val_split_ratio);
        AppendBool(oss, "use_tanh_transform", cfg.use_tanh_transform);
        AppendNumeric(oss, "tanh_scaling_factor", cfg.tanh_scaling_factor);
        AppendBool(oss, "use_standardization", cfg.use_standardization);
        AppendKeyValue(oss, "threshold_method", ThresholdMethodToString(cfg.threshold_method));
        oss << '\n';
    }

    if ((sections & SectionTrade) && snapshot.hasTradeConfig) {
        const auto& cfg = snapshot.trade;
        oss << "[TRADE]\n";
        AppendNumeric(oss, "position_size", cfg.position_size);
        AppendBool(oss, "use_signal_exit", cfg.use_signal_exit);
        AppendNumeric(oss, "exit_strength_pct", cfg.exit_strength_pct);
        AppendBool(oss, "honor_signal_reversal", cfg.honor_signal_reversal);
        AppendBool(oss, "use_stop_loss", cfg.use_stop_loss);
        AppendBool(oss, "use_atr_stop_loss", cfg.use_atr_stop_loss);
        AppendNumeric(oss, "stop_loss_pct", cfg.stop_loss_pct);
        AppendNumeric(oss, "atr_multiplier", cfg.atr_multiplier);
        AppendNumeric(oss, "atr_period", cfg.atr_period, 0);
        AppendNumeric(oss, "stop_loss_cooldown_bars", cfg.stop_loss_cooldown_bars, 0);
        AppendBool(oss, "use_take_profit", cfg.use_take_profit);
        AppendBool(oss, "use_atr_take_profit", cfg.use_atr_take_profit);
        AppendNumeric(oss, "take_profit_pct", cfg.take_profit_pct);
        AppendNumeric(oss, "atr_tp_multiplier", cfg.atr_tp_multiplier);
        AppendNumeric(oss, "atr_tp_period", cfg.atr_tp_period, 0);
        AppendBool(oss, "use_time_exit", cfg.use_time_exit);
        AppendNumeric(oss, "max_holding_bars", cfg.max_holding_bars, 0);
        AppendBool(oss, "use_limit_orders", cfg.use_limit_orders);
        AppendNumeric(oss, "limit_order_window", cfg.limit_order_window, 0);
        AppendNumeric(oss, "limit_order_offset", cfg.limit_order_offset);
        AppendKeyValue(oss, "threshold_choice", ThresholdChoiceToString(cfg.threshold_choice));
        oss << '\n';
    }

    if (snapshot.hasStressConfig) {
        const auto& stress = snapshot.stress;
        oss << "[STRESS_TEST]\n";
        AppendBool(oss, "enable", stress.enable);
        AppendNumeric(oss, "bootstrap_iterations", stress.bootstrap_iterations, 0);
        AppendNumeric(oss, "mcpt_iterations", stress.mcpt_iterations, 0);
        AppendNumeric(oss, "seed", static_cast<long long>(stress.seed), 0);
        oss << '\n';
    }

    return oss.str();
}

bool RunConfigSerializer::Deserialize(const std::string& text, Snapshot* snapshot, std::string* error) {
    if (!snapshot) {
        if (error) *error = "Snapshot pointer is null.";
        return false;
    }

    Snapshot result;
    enum class ParseSection {
        None,
        Metadata,
        Features,
        FeatureSchedule,
        Walkforward,
        Hyperparameters,
        Trade,
        Stress,
        Target
    };
    ParseSection section = ParseSection::None;
    std::ostringstream scheduleBuffer;
    bool collectingSchedule = false;

    auto flushSchedule = [&]() {
        if (collectingSchedule) {
            result.featureSchedule = scheduleBuffer.str();
            result.hasFeatureSchedule = HasPrintableContent(result.featureSchedule);
            scheduleBuffer.str("");
            scheduleBuffer.clear();
            collectingSchedule = false;
        }
    };

    std::istringstream stream(text);
    std::string rawLine;
    while (std::getline(stream, rawLine)) {
        std::string line = Trim(rawLine);
        if (line.empty()) {
            continue;
        }
        if (!line.empty() && line[0] == '#') {
            line.erase(0, 1);
            line = Trim(line);
            if (line.empty()) {
                continue;
            }
        }

        std::string lowerLine = ToLower(line);
        if (!line.empty() && line.front() == '[' && line.back() == ']') {
            flushSchedule();
            std::string tag = line.substr(1, line.size() - 2);
            std::string tagLower = ToLower(tag);
            if (tagLower == "metadata") { section = ParseSection::Metadata; continue; }
            if (tagLower == "features") { section = ParseSection::Features; continue; }
            if (tagLower == "feature_schedule") { section = ParseSection::FeatureSchedule; collectingSchedule = true; continue; }
            if (tagLower == "/feature_schedule") { flushSchedule(); section = ParseSection::None; continue; }
            if (tagLower == "walkforward") { flushSchedule(); section = ParseSection::Walkforward; continue; }
            if (tagLower == "hyperparameters") { flushSchedule(); section = ParseSection::Hyperparameters; continue; }
            if (tagLower == "trade") { flushSchedule(); section = ParseSection::Trade; continue; }
            if (tagLower == "stress_test" || tagLower == "stresstest") { flushSchedule(); section = ParseSection::Stress; continue; }
            continue;
        }

        if (lowerLine.find("feature schedule") != std::string::npos) {
            flushSchedule();
            section = ParseSection::FeatureSchedule;
            collectingSchedule = true;
            continue;
        }
        if (lowerLine.find("walk-forward") != std::string::npos) {
            flushSchedule();
            section = ParseSection::Walkforward;
            continue;
        }
        if (lowerLine.find("hyperparameter") != std::string::npos
            || lowerLine.find("xgboost hyperparameters") != std::string::npos) {
            flushSchedule();
            section = ParseSection::Hyperparameters;
            continue;
        }
        if (lowerLine.find("trade simulation parameters") != std::string::npos
            || lowerLine == "trade configuration") {
            flushSchedule();
            section = ParseSection::Trade;
            continue;
        }
        if (lowerLine.find("stress") != std::string::npos &&
            lowerLine.find("test") != std::string::npos) {
            flushSchedule();
            section = ParseSection::Stress;
            continue;
        }
        if (lowerLine == "target" || lowerLine == "target column") {
            section = ParseSection::Target;
            continue;
        }
        if (section == ParseSection::FeatureSchedule && collectingSchedule) {
            if (!scheduleBuffer.str().empty()) {
                scheduleBuffer << '\n';
            }
            scheduleBuffer << rawLine;
            continue;
        }
        if (section == ParseSection::Target) {
            result.target = Trim(line);
            section = ParseSection::Features;
            continue;
        }

        std::string key;
        std::string value;
        size_t delimiter = line.find_first_of(":=");
        if (delimiter != std::string::npos) {
            key = Trim(line.substr(0, delimiter));
            value = Trim(line.substr(delimiter + 1));
        } else {
            value = line;
        }
        std::string keyLower = ToLower(key);
        std::string normalizedKey = NormalizeKey(key);

        auto assignMetadata = [&](const std::string& k, const std::string& v) {
            if (k == "model" || k == "modeltype") {
                result.modelType = v;
                return true;
            }
            if (k == "run" || k == "runname" || k == "name") {
                result.runName = v;
                return true;
            }
            if (k == "dataset" || k == "datasetid") {
                result.dataset = v;
                return true;
            }
            if (k == "description") {
                result.description = v;
                return true;
            }
            return false;
        };

        if (assignMetadata(normalizedKey, value)) {
            continue;
        }

        switch (section) {
            case ParseSection::Metadata:
                if (assignMetadata(normalizedKey, value)) {
                    continue;
                }
                break;
            case ParseSection::Features: {
                if (normalizedKey == "target" || normalizedKey == "targetcolumn") {
                    result.target = value;
                    continue;
                }
                if (normalizedKey == "feature" || normalizedKey == "features" || key.empty()) {
                    AppendFeatureCsv(result.features, value);
                    continue;
                }
                if (normalizedKey == "schedule" || normalizedKey == "featureschedule") {
                    result.featureSchedule = value;
                    result.hasFeatureSchedule = HasPrintableContent(value);
                    continue;
                }
                break;
            }
            case ParseSection::Walkforward: {
                auto& wf = result.walkForward;
                if (normalizedKey == "trainsize") { ParseIntegral(value, &wf.train_size); result.hasWalkForward = true; continue; }
                if (normalizedKey == "testsize") { ParseIntegral(value, &wf.test_size); result.hasWalkForward = true; continue; }
                if (normalizedKey == "traintestgap") { ParseIntegral(value, &wf.train_test_gap); result.hasWalkForward = true; continue; }
                if (normalizedKey == "foldstep") { ParseIntegral(value, &wf.fold_step); result.hasWalkForward = true; continue; }
                if (normalizedKey == "startfold") { ParseIntegral(value, &wf.start_fold); result.hasWalkForward = true; continue; }
                if (normalizedKey == "endfold") { ParseIntegral(value, &wf.end_fold); result.hasWalkForward = true; continue; }
                if (normalizedKey == "initialoffset") { ParseIntegral(value, &wf.initial_offset); result.hasWalkForward = true; continue; }
                break;
            }
            case ParseSection::Hyperparameters: {
                result.hyperparameterType = "XGBoost";
                if (!result.xgboost.has_value()) {
                    result.xgboost = simulation::XGBoostConfig();
                }
                auto& cfg = result.xgboost.value();
                result.hasHyperparameters = true;
                if (normalizedKey == "learningrate") { ParseFloating(value, &cfg.learning_rate); continue; }
                if (normalizedKey == "maxdepth") { ParseIntegral(value, &cfg.max_depth); continue; }
                if (normalizedKey == "minchildweight") { ParseFloating(value, &cfg.min_child_weight); continue; }
                if (normalizedKey == "subsample") { ParseFloating(value, &cfg.subsample); continue; }
                if (normalizedKey == "colsamplebytree") { ParseFloating(value, &cfg.colsample_bytree); continue; }
                if (normalizedKey == "lambda") { ParseFloating(value, &cfg.lambda); continue; }
                if (normalizedKey == "numboostround") { ParseIntegral(value, &cfg.num_boost_round); continue; }
                if (normalizedKey == "earlystoppingrounds") { ParseIntegral(value, &cfg.early_stopping_rounds); continue; }
                if (normalizedKey == "minboostrounds") { ParseIntegral(value, &cfg.min_boost_rounds); continue; }
                if (normalizedKey == "forceminimumtraining") { bool parsed; ParseBoolValue(value, cfg.force_minimum_training, &parsed); cfg.force_minimum_training = parsed; continue; }
                if (normalizedKey == "objective") { cfg.objective = value; continue; }
                if (normalizedKey == "quantilealpha") { ParseFloating(value, &cfg.quantile_alpha); continue; }
                if (normalizedKey == "treemethod") { cfg.tree_method = value; continue; }
                if (normalizedKey == "device") { cfg.device = value; continue; }
                if (normalizedKey == "randomseed") { ParseIntegral(value, &cfg.random_seed); continue; }
                if (normalizedKey == "valsplitratio") { ParseFloating(value, &cfg.val_split_ratio); continue; }
                if (normalizedKey == "usetanhtransform") { bool parsed; ParseBoolValue(value, cfg.use_tanh_transform, &parsed); cfg.use_tanh_transform = parsed; continue; }
                if (normalizedKey == "tanhscalingfactor") { ParseFloating(value, &cfg.tanh_scaling_factor); continue; }
                if (normalizedKey == "usestandardization") { bool parsed; ParseBoolValue(value, cfg.use_standardization, &parsed); cfg.use_standardization = parsed; continue; }
                if (normalizedKey == "thresholdmethod") { cfg.threshold_method = ParseThresholdMethod(value); continue; }
                break;
            }
            case ParseSection::Trade: {
                auto& cfg = result.trade;
                result.hasTradeConfig = true;
                if (normalizedKey == "positionsize") { ParseFloating(value, &cfg.position_size); continue; }
                if (normalizedKey == "usesignalexit") { bool parsed; ParseBoolValue(value, cfg.use_signal_exit, &parsed); cfg.use_signal_exit = parsed; continue; }
                if (normalizedKey == "exitstrengthpct") { ParseFloating(value, &cfg.exit_strength_pct); continue; }
                if (normalizedKey == "honorsignalreversal") { bool parsed; ParseBoolValue(value, cfg.honor_signal_reversal, &parsed); cfg.honor_signal_reversal = parsed; continue; }
                if (normalizedKey == "usestoploss") { bool parsed; ParseBoolValue(value, cfg.use_stop_loss, &parsed); cfg.use_stop_loss = parsed; continue; }
                if (normalizedKey == "useatrstoploss") { bool parsed; ParseBoolValue(value, cfg.use_atr_stop_loss, &parsed); cfg.use_atr_stop_loss = parsed; continue; }
                if (normalizedKey == "stoplosspct") { ParseFloating(value, &cfg.stop_loss_pct); continue; }
                if (normalizedKey == "atrmultiplier") { ParseFloating(value, &cfg.atr_multiplier); continue; }
                if (normalizedKey == "atrperiod") { ParseIntegral(value, &cfg.atr_period); continue; }
                if (normalizedKey == "stoplosscooldownbars") { ParseIntegral(value, &cfg.stop_loss_cooldown_bars); continue; }
                if (normalizedKey == "usetakeprofit") { bool parsed; ParseBoolValue(value, cfg.use_take_profit, &parsed); cfg.use_take_profit = parsed; continue; }
                if (normalizedKey == "useatrtakeprofit") { bool parsed; ParseBoolValue(value, cfg.use_atr_take_profit, &parsed); cfg.use_atr_take_profit = parsed; continue; }
                if (normalizedKey == "takeprofitpct") { ParseFloating(value, &cfg.take_profit_pct); continue; }
                if (normalizedKey == "atrtpmultiplier") { ParseFloating(value, &cfg.atr_tp_multiplier); continue; }
                if (normalizedKey == "atrtpperiod") { ParseIntegral(value, &cfg.atr_tp_period); continue; }
                if (normalizedKey == "usetimeexit") { bool parsed; ParseBoolValue(value, cfg.use_time_exit, &parsed); cfg.use_time_exit = parsed; continue; }
                if (normalizedKey == "maxholdingbars") { ParseIntegral(value, &cfg.max_holding_bars); continue; }
                if (normalizedKey == "uselimitorders") { bool parsed; ParseBoolValue(value, cfg.use_limit_orders, &parsed); cfg.use_limit_orders = parsed; continue; }
                if (normalizedKey == "limitorderwindow") { ParseIntegral(value, &cfg.limit_order_window); continue; }
                if (normalizedKey == "limitorderoffset") { ParseFloating(value, &cfg.limit_order_offset); continue; }
                if (normalizedKey == "thresholdchoice") { cfg.threshold_choice = ParseThresholdChoice(value); continue; }
                break;
            }
            case ParseSection::Stress: {
                result.hasStressConfig = true;
                if (normalizedKey == "enable") {
                    bool parsed;
                    ParseBoolValue(value, true, &parsed);
                    result.stress.enable = parsed;
                    continue;
                }
                if (normalizedKey == "bootstrapiterations") {
                    ParseIntegral(value, &result.stress.bootstrap_iterations);
                    continue;
                }
                if (normalizedKey == "mcptiterations") {
                    ParseIntegral(value, &result.stress.mcpt_iterations);
                    continue;
                }
                if (normalizedKey == "seed") {
                    std::uint64_t seed = result.stress.seed;
                    if (ParseIntegral(value, &seed)) {
                        result.stress.seed = seed;
                    }
                    continue;
                }
                break;
            }
            case ParseSection::FeatureSchedule:
                // handled earlier
                break;
            case ParseSection::None:
            case ParseSection::Target:
                break;
        }

        if ((section == ParseSection::None || section == ParseSection::Metadata) && key.empty() &&
            !value.empty()) {
            AppendFeatureCsv(result.features, value);
        }
    }

    flushSchedule();

    const bool parsedSomething =
        !result.features.empty() ||
        result.hasHyperparameters ||
        result.hasTradeConfig ||
        result.hasStressConfig ||
        result.hasWalkForward ||
        !result.target.empty();

    if (!parsedSomething) {
        if (error) {
            *error = "Clipboard text did not contain recognizable configuration data.";
        }
        return false;
    }

    *snapshot = result;
    return true;
}

bool RunConfigSerializer::LooksLikeSerializedConfig(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    if (text.find("[FEATURES]") != std::string::npos ||
        text.find("[TRADE]") != std::string::npos ||
        text.find("[STRESS_TEST]") != std::string::npos ||
        text.find("# Trade Simulation Parameters") != std::string::npos ||
        text.find("Train Size") != std::string::npos ||
        text.find("position_size") != std::string::npos) {
        return true;
    }
    return false;
}

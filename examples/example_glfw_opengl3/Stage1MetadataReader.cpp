#include "Stage1MetadataReader.h"

#include "Stage1RestClient.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>
#include <iostream>

namespace {

bool ParseJson(const std::string& json, rapidjson::Document* doc) {
    if (!doc) {
        return false;
    }
    if (json.empty()) {
        doc->SetObject();
        return true;
    }
    doc->Parse(json.c_str());
    return !doc->HasParseError();
}

double GetDoubleOr(const rapidjson::Value& obj, const char* key, double fallback = 0.0) {
    if (!obj.IsObject()) return fallback;
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsNumber()) return fallback;
    return it->value.GetDouble();
}

int GetIntOr(const rapidjson::Value& obj, const char* key, int fallback = 0) {
    if (!obj.IsObject()) return fallback;
    auto it = obj.FindMember(key);
    if (it == obj.MemberEnd() || !it->value.IsInt()) return fallback;
    return it->value.GetInt();
}

bool ParseTimestampBasic(const std::string& text, std::chrono::system_clock::time_point* tp) {
    if (!tp || text.empty()) {
        return false;
    }
    std::tm tm = {};
    std::istringstream ss(text);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return false;
    }
#if defined(_WIN32)
    time_t tt = _mkgmtime(&tm);
#else
    time_t tt = timegm(&tm);
#endif
    if (tt == static_cast<time_t>(-1)) {
        return false;
    }
    *tp = std::chrono::system_clock::from_time_t(tt);
    return true;
}

bool ParseTimestampFlexible(const std::string& text, std::chrono::system_clock::time_point* tp) {
    if (!tp || text.empty()) {
        return false;
    }

    std::string working = text;
    // Trim trailing Z/z (UTC designator)
    if (!working.empty() && (working.back() == 'Z' || working.back() == 'z')) {
        working.pop_back();
    }

    // Normalize separator
    auto tPos = working.find('T');
    if (tPos != std::string::npos) {
        working[tPos] = ' ';
    }

    // Extract timezone offset (e.g., +02:00 or -0530)
    int offsetMinutes = 0;
    auto LocateTzPos = [](const std::string& value) -> std::size_t {
        std::size_t searchStart = value.find(' ');
        if (searchStart == std::string::npos) {
            searchStart = value.find('T');
        }
        if (searchStart == std::string::npos) {
            searchStart = 0;
        }
        return value.find_first_of("+-", searchStart + 1);
    };

    const std::size_t tzPos = LocateTzPos(working);
    if (tzPos != std::string::npos) {
        const char sign = working[tzPos];
        std::string offset = working.substr(tzPos + 1);
        working = working.substr(0, tzPos);
        offset.erase(std::remove(offset.begin(), offset.end(), ':'), offset.end());
        if (!offset.empty()) {
            int hours = 0;
            int minutes = 0;
            try {
                if (offset.size() >= 2) {
                    hours = std::stoi(offset.substr(0, 2));
                }
                if (offset.size() >= 4) {
                    minutes = std::stoi(offset.substr(2, 2));
                }
                offsetMinutes = hours * 60 + minutes;
                if (sign == '-') {
                    offsetMinutes = -offsetMinutes;
                }
            } catch (...) {
                offsetMinutes = 0;
            }
        }
    }

    // Extract fractional seconds (keep millisecond precision)
    int64_t fractionalMillis = 0;
    auto dotPos = working.find('.');
    if (dotPos != std::string::npos) {
        std::string fraction = working.substr(dotPos + 1);
        working = working.substr(0, dotPos);
        while (fraction.size() < 3) {
            fraction.push_back('0');
        }
        if (fraction.size() > 3) {
            fraction.resize(3);
        }
        try {
            fractionalMillis = std::stoll(fraction);
        } catch (...) {
            fractionalMillis = 0;
        }
    }

    std::tm tm = {};
    std::istringstream ss(working);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return false;
    }
#if defined(_WIN32)
    time_t tt = _mkgmtime(&tm);
#else
    time_t tt = timegm(&tm);
#endif
    if (tt == static_cast<time_t>(-1)) {
        return false;
    }

    auto base = std::chrono::system_clock::from_time_t(tt);
    if (offsetMinutes != 0) {
        base -= std::chrono::minutes(offsetMinutes);
    }
    base += std::chrono::milliseconds(fractionalMillis);
    *tp = base;
    return true;
}

void ParseStringArray(const rapidjson::Value& value, std::vector<std::string>* out) {
    if (!out) return;
    out->clear();
    if (!value.IsArray()) return;
    for (const auto& item : value.GetArray()) {
        if (item.IsString()) {
            out->push_back(item.GetString());
        }
    }
}

} // namespace

bool Stage1MetadataReader::ListRunsForDataset(const std::string& dataset_id,
                                              std::vector<RunSummary>* runs,
                                              std::string* error) {
    if (!runs) {
        if (error) *error = "Run container is null.";
        return false;
    }
    runs->clear();
    if (dataset_id.empty()) {
        if (error) *error = "Dataset ID is required.";
        return false;
    }

    stage1::RestClient& api = stage1::RestClient::Instance();
    std::vector<stage1::RunSummary> remote;
    if (!api.FetchDatasetRuns(dataset_id, 200, 0, &remote, error)) {
        std::cerr << "[Stage1MetadataReader] Failed to fetch runs for dataset "
                  << dataset_id << ": " << (error ? *error : "unknown error") << std::endl;
        return false;
    }

    runs->reserve(remote.size());
    for (const auto& summary : remote) {
        RunSummary row;
        row.run_id = summary.run_id;
        row.measurement = summary.prediction_measurement;
        row.status = summary.status;
        row.started_at = summary.started_at;
        row.completed_at = summary.completed_at;
        runs->push_back(std::move(row));
    }
    std::cout << "[Stage1MetadataReader] Dataset " << dataset_id
              << " run summaries loaded: " << runs->size() << std::endl;
    return true;
}

bool Stage1MetadataReader::LoadRunPayload(const std::string& run_id,
                                          RunPayload* payload,
                                          std::string* error) {
    if (!payload) {
        if (error) *error = "Payload pointer is null.";
        return false;
    }
    payload->folds.clear();
    payload->feature_columns.clear();

    stage1::RestClient& api = stage1::RestClient::Instance();
    stage1::RunDetail detail;
    if (!api.FetchRunDetail(run_id, &detail, error)) {
        std::cerr << "[Stage1MetadataReader] FetchRunDetail failed for run "
                  << run_id << ": " << (error ? *error : "unknown error") << std::endl;
        return false;
    }

    payload->run_id = detail.run_id;
    payload->dataset_id = detail.dataset_id;
    payload->dataset_slug = detail.dataset_slug;
    payload->prediction_measurement = detail.prediction_measurement;
    payload->target_column = detail.target_column;
    payload->feature_columns = detail.feature_columns;
    std::cout << "[Stage1MetadataReader] Run " << run_id << " feature_columns="
              << payload->feature_columns.size() << std::endl;
    payload->summary_metrics_json = detail.summary_metrics_json;
    payload->status = detail.status.empty() ? "UNKNOWN" : detail.status;

    if (!ParseHyperparameters(detail.hyperparameters_json, &payload->hyperparameters)) {
        if (error) *error = "Failed to parse hyperparameters JSON.";
        return false;
    }

    if (!ParseWalkConfig(detail.walk_config_json, &payload->walk_config)) {
        if (error) *error = "Failed to parse walk-forward config JSON.";
        return false;
    }

    payload->started_at = std::chrono::system_clock::time_point{};
    payload->completed_at = payload->started_at;
    ParseTimestampFlexible(detail.started_at, &payload->started_at);
    ParseTimestampFlexible(detail.completed_at, &payload->completed_at);

    for (const auto& fold : detail.folds) {
        Stage1MetadataWriter::WalkforwardFoldRecord record;
        record.fold_number = fold.fold_number;
        record.train_start = fold.train_start;
        record.train_end = fold.train_end;
        record.test_start = fold.test_start;
        record.test_end = fold.test_end;
        record.samples_train = fold.samples_train;
        record.samples_test = fold.samples_test;
        record.hit_rate = static_cast<float>(fold.hit_rate);
        record.short_hit_rate = static_cast<float>(fold.short_hit_rate);
        record.profit_factor_test = static_cast<float>(fold.profit_factor_test);
        record.long_threshold_optimal = static_cast<float>(fold.long_threshold);
        record.short_threshold_optimal = static_cast<float>(fold.short_threshold);
        ParseFoldJson(&record, fold.thresholds_json, fold.metrics_json);
        payload->folds.push_back(std::move(record));
    }

    std::cout << "[Stage1MetadataReader] Loaded run " << payload->run_id
              << " (dataset " << payload->dataset_id << ") folds="
              << payload->folds.size() << " measurement="
              << payload->prediction_measurement << std::endl;
    return true;
}

bool Stage1MetadataReader::ParseWalkConfig(const std::string& json,
                                           simulation::WalkForwardConfig* cfg) {
    if (!cfg) return false;
    rapidjson::Document doc;
    if (!ParseJson(json, &doc) || !doc.IsObject()) {
        return false;
    }
    cfg->train_size = GetIntOr(doc, "train_size", cfg->train_size);
    cfg->test_size = GetIntOr(doc, "test_size", cfg->test_size);
    cfg->train_test_gap = GetIntOr(doc, "train_test_gap", cfg->train_test_gap);
    cfg->fold_step = GetIntOr(doc, "fold_step", cfg->fold_step);
    cfg->start_fold = GetIntOr(doc, "start_fold", cfg->start_fold);
    cfg->end_fold = GetIntOr(doc, "end_fold", cfg->end_fold);
    cfg->initial_offset = GetIntOr(doc, "initial_offset", cfg->initial_offset);
    return true;
}

bool Stage1MetadataReader::ParseHyperparameters(const std::string& json,
                                                simulation::XGBoostConfig* xgb) {
    if (!xgb) return false;
    rapidjson::Document doc;
    if (!ParseJson(json, &doc) || !doc.IsObject()) {
        return false;
    }
    auto getBool = [&](const char* key, bool fallback) {
        auto it = doc.FindMember(key);
        if (it == doc.MemberEnd() || !it->value.IsBool()) return fallback;
        return it->value.GetBool();
    };
    auto getDouble = [&](const char* key, double fallback) {
        auto it = doc.FindMember(key);
        if (it == doc.MemberEnd() || !it->value.IsNumber()) return fallback;
        return it->value.GetDouble();
    };
    auto getInt = [&](const char* key, int fallback) {
        auto it = doc.FindMember(key);
        if (it == doc.MemberEnd() || !it->value.IsInt()) return fallback;
        return it->value.GetInt();
    };
    auto getString = [&](const char* key, const std::string& fallback) {
        auto it = doc.FindMember(key);
        if (it == doc.MemberEnd() || !it->value.IsString()) return fallback;
        return std::string(it->value.GetString());
    };

    xgb->learning_rate = getDouble("learning_rate", xgb->learning_rate);
    xgb->max_depth = getInt("max_depth", xgb->max_depth);
    xgb->min_child_weight = getDouble("min_child_weight", xgb->min_child_weight);
    xgb->subsample = getDouble("subsample", xgb->subsample);
    xgb->colsample_bytree = getDouble("colsample_bytree", xgb->colsample_bytree);
    xgb->lambda = getDouble("lambda", xgb->lambda);
    xgb->num_boost_round = getInt("num_boost_round", xgb->num_boost_round);
    xgb->early_stopping_rounds = getInt("early_stopping_rounds", xgb->early_stopping_rounds);
    xgb->min_boost_rounds = getInt("min_boost_rounds", xgb->min_boost_rounds);
    xgb->force_minimum_training = getBool("force_minimum_training", xgb->force_minimum_training);
    xgb->objective = getString("objective", xgb->objective);
    xgb->quantile_alpha = getDouble("quantile_alpha", xgb->quantile_alpha);
    xgb->tree_method = getString("tree_method", xgb->tree_method);
    xgb->device = getString("device", xgb->device);
    xgb->random_seed = getInt("random_seed", xgb->random_seed);
    xgb->val_split_ratio = getDouble("val_split_ratio", xgb->val_split_ratio);
    xgb->use_tanh_transform = getBool("use_tanh_transform", xgb->use_tanh_transform);
    xgb->tanh_scaling_factor = getDouble("tanh_scaling_factor", xgb->tanh_scaling_factor);
    xgb->use_standardization = getBool("use_standardization", xgb->use_standardization);
    xgb->threshold_method = simulation::ThresholdMethod::Percentile95;
    if (auto method = doc.FindMember("threshold_method"); method != doc.MemberEnd() && method->value.IsString()) {
        std::string value = method->value.GetString();
        if (value == "OptimalROC") {
            xgb->threshold_method = simulation::ThresholdMethod::OptimalROC;
        } else {
            xgb->threshold_method = simulation::ThresholdMethod::Percentile95;
        }
    }
    return true;
}

void Stage1MetadataReader::ParseFoldJson(Stage1MetadataWriter::WalkforwardFoldRecord* record,
                                         const std::string& thresholds_json,
                                         const std::string& metrics_json) {
    if (!record) return;

    rapidjson::Document doc;
    if (ParseJson(thresholds_json, &doc) && doc.IsObject()) {
        record->long_threshold_optimal = GetDoubleOr(doc, "long_optimal", record->long_threshold_optimal);
        record->short_threshold_optimal = GetDoubleOr(doc, "short_optimal", record->short_threshold_optimal);
        record->prediction_threshold_scaled = GetDoubleOr(doc, "prediction_scaled", record->prediction_threshold_scaled);
        record->prediction_threshold_original = GetDoubleOr(doc, "prediction_original", record->prediction_threshold_original);
        record->dynamic_positive_threshold = GetDoubleOr(doc, "dynamic_positive", record->dynamic_positive_threshold);
        record->short_threshold_scaled = GetDoubleOr(doc, "short_scaled", record->short_threshold_scaled);
        record->short_threshold_original = GetDoubleOr(doc, "short_original", record->short_threshold_original);
        record->long_threshold_95th = GetDoubleOr(doc, "long_percentile", record->long_threshold_95th);
        record->short_threshold_5th = GetDoubleOr(doc, "short_percentile", record->short_threshold_5th);
    }

    if (ParseJson(metrics_json, &doc) && doc.IsObject()) {
        record->hit_rate = GetDoubleOr(doc, "hit_rate", record->hit_rate);
        record->short_hit_rate = GetDoubleOr(doc, "short_hit_rate", record->short_hit_rate);
        record->profit_factor_test = GetDoubleOr(doc, "profit_factor_test", record->profit_factor_test);
        record->profit_factor_train = GetDoubleOr(doc, "profit_factor_train", record->profit_factor_train);
        record->profit_factor_short_train = GetDoubleOr(doc, "profit_factor_short_train", record->profit_factor_short_train);
        record->profit_factor_short_test = GetDoubleOr(doc, "profit_factor_short_test", record->profit_factor_short_test);
        record->n_signals = GetIntOr(doc, "n_signals", record->n_signals);
        record->n_short_signals = GetIntOr(doc, "n_short_signals", record->n_short_signals);
        record->signal_sum = GetDoubleOr(doc, "signal_sum", record->signal_sum);
        record->short_signal_sum = GetDoubleOr(doc, "short_signal_sum", record->short_signal_sum);
        record->signal_rate = GetDoubleOr(doc, "signal_rate", record->signal_rate);
        record->short_signal_rate = GetDoubleOr(doc, "short_signal_rate", record->short_signal_rate);
        record->avg_return_on_signals = GetDoubleOr(doc, "avg_return_on_signals", record->avg_return_on_signals);
        record->median_return_on_signals = GetDoubleOr(doc, "median_return_on_signals", record->median_return_on_signals);
        record->std_return_on_signals = GetDoubleOr(doc, "std_return_on_signals", record->std_return_on_signals);
        record->avg_return_on_short_signals = GetDoubleOr(doc, "avg_return_on_short_signals", record->avg_return_on_short_signals);
        record->avg_predicted_return_on_signals = GetDoubleOr(doc, "avg_predicted_return_on_signals", record->avg_predicted_return_on_signals);
        record->running_sum = GetDoubleOr(doc, "running_sum", record->running_sum);
        record->running_sum_short = GetDoubleOr(doc, "running_sum_short", record->running_sum_short);
        record->running_sum_dual = GetDoubleOr(doc, "running_sum_dual", record->running_sum_dual);
        record->sum_wins = GetDoubleOr(doc, "sum_wins", record->sum_wins);
        record->sum_losses = GetDoubleOr(doc, "sum_losses", record->sum_losses);
        record->sum_short_wins = GetDoubleOr(doc, "sum_short_wins", record->sum_short_wins);
        record->sum_short_losses = GetDoubleOr(doc, "sum_short_losses", record->sum_short_losses);
        record->model_learned_nothing = GetIntOr(doc, "model_learned_nothing", record->model_learned_nothing ? 1 : 0) != 0;
        record->used_cached_model = GetIntOr(doc, "used_cached_model", record->used_cached_model ? 1 : 0) != 0;
    }
}

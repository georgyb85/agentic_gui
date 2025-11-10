#include "QuestDbExports.h"

#include <arrow/api.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "QuestDbDataFrameGateway.h"
#include "chronosflow.h"

namespace questdb {
namespace {

arrow::Status AppendMaybeNull(arrow::DoubleBuilder& builder, double value) {
    if (std::isfinite(value)) {
        return builder.Append(value);
    }
    return builder.AppendNull();
}

arrow::Result<chronosflow::AnalyticsDataFrame> BuildWalkforwardDataFrame(
    const simulation::SimulationRun& run,
    const Stage1MetadataWriter::WalkforwardRecord& record) {
    if (run.all_test_predictions.empty() || run.all_test_timestamps.empty()) {
        return arrow::Status::Invalid("Simulation run contains no predictions to export.");
    }

    const int totalPreds = static_cast<int>(run.all_test_predictions.size());

    arrow::Int64Builder timestamp_builder;
    arrow::Int64Builder bar_index_builder;
    arrow::Int32Builder fold_builder;
    arrow::DoubleBuilder prediction_builder;
    arrow::DoubleBuilder actual_builder;
    arrow::DoubleBuilder long_threshold_builder;
    arrow::DoubleBuilder short_threshold_builder;
    arrow::DoubleBuilder roc_threshold_builder;
    arrow::DoubleBuilder short_entry_threshold_builder;
    arrow::DoubleBuilder fold_score_builder;
    arrow::DoubleBuilder fold_profit_factor_builder;

    ARROW_RETURN_NOT_OK(timestamp_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(bar_index_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(fold_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(prediction_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(actual_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(long_threshold_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(short_threshold_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(roc_threshold_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(short_entry_threshold_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(fold_score_builder.Reserve(totalPreds));
    ARROW_RETURN_NOT_OK(fold_profit_factor_builder.Reserve(totalPreds));

    const auto offsets = run.fold_prediction_offsets;
    size_t appended_rows = 0;

    for (size_t foldIndex = 0; foldIndex < run.foldResults.size(); ++foldIndex) {
        if (foldIndex >= offsets.size()) {
            break;
        }
        const auto& fold = run.foldResults[foldIndex];

        const int start = offsets[foldIndex];
        const int end = (foldIndex + 1 < offsets.size()) ? offsets[foldIndex + 1] : totalPreds;

        for (int idx = start; idx < end && idx < totalPreds; ++idx) {
            if (idx >= static_cast<int>(run.all_test_timestamps.size())) {
                break;
            }

            const int64_t timestampMs = run.all_test_timestamps[idx];
            if (timestampMs <= 0) {
                continue;
            }

            const double prediction = run.all_test_predictions[idx];
            const double actual = (idx < static_cast<int>(run.all_test_actuals.size()))
                                      ? run.all_test_actuals[idx]
                                      : std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(prediction) || !std::isfinite(actual)) {
                continue;
            }

            ARROW_RETURN_NOT_OK(timestamp_builder.Append(timestampMs));
            ARROW_RETURN_NOT_OK(bar_index_builder.Append(static_cast<int64_t>(fold.test_start + (idx - start))));
            ARROW_RETURN_NOT_OK(fold_builder.Append(fold.fold_number));
            ARROW_RETURN_NOT_OK(prediction_builder.Append(prediction));
            ARROW_RETURN_NOT_OK(actual_builder.Append(actual));
            ARROW_RETURN_NOT_OK(AppendMaybeNull(long_threshold_builder, fold.long_threshold_optimal));
            ARROW_RETURN_NOT_OK(AppendMaybeNull(short_threshold_builder, fold.short_threshold_optimal));
            ARROW_RETURN_NOT_OK(AppendMaybeNull(roc_threshold_builder, fold.prediction_threshold_original));
            ARROW_RETURN_NOT_OK(AppendMaybeNull(short_entry_threshold_builder, fold.short_threshold_original));
            ARROW_RETURN_NOT_OK(AppendMaybeNull(fold_score_builder, fold.best_score));
            ARROW_RETURN_NOT_OK(AppendMaybeNull(fold_profit_factor_builder, fold.profit_factor_test));

            ++appended_rows;
        }
    }

    if (appended_rows == 0) {
        return arrow::Status::Invalid("No valid prediction rows were available for export.");
    }

    std::vector<std::shared_ptr<arrow::Field>> fields = {
        arrow::field("timestamp_unix", arrow::int64()),
        arrow::field("bar_index", arrow::int64()),
        arrow::field("fold_number", arrow::int32()),
        arrow::field("prediction", arrow::float64()),
        arrow::field("target_value", arrow::float64()),
        arrow::field("long_threshold", arrow::float64()),
        arrow::field("short_threshold", arrow::float64()),
        arrow::field("roc_threshold", arrow::float64()),
        arrow::field("short_entry_threshold", arrow::float64()),
        arrow::field("fold_score", arrow::float64()),
        arrow::field("fold_profit_factor", arrow::float64())
    };

    std::vector<std::shared_ptr<arrow::Array>> arrays;
    ARROW_RETURN_NOT_OK(timestamp_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(bar_index_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(fold_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(prediction_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(actual_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(long_threshold_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(short_threshold_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(roc_threshold_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(short_entry_threshold_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(fold_score_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(fold_profit_factor_builder.Finish(&arrays.emplace_back()));

    std::vector<std::shared_ptr<arrow::ChunkedArray>> columns;
    columns.reserve(arrays.size());
    for (const auto& array : arrays) {
        columns.push_back(std::make_shared<arrow::ChunkedArray>(array));
    }

    auto table = arrow::Table::Make(arrow::schema(fields), columns);
    chronosflow::AnalyticsDataFrame dataframe(table);
    return dataframe;
}

arrow::Status AppendTimestampOrNull(arrow::Int64Builder& builder, double value) {
    if (value > 0) {
        return builder.Append(static_cast<int64_t>(std::llround(value)));
    }
    return builder.AppendNull();
}

arrow::Result<chronosflow::AnalyticsDataFrame> BuildTradeDataFrame(
    const std::vector<ExecutedTrade>& trades) {
    if (trades.empty()) {
        return arrow::Status::Invalid("No trades to export.");
    }

    arrow::Int64Builder entry_ts_builder;
    arrow::Int64Builder exit_ts_builder;
    arrow::Int64Builder trade_index_builder;
    arrow::Int32Builder fold_index_builder;
    arrow::DoubleBuilder position_size_builder;
    arrow::DoubleBuilder entry_price_builder;
    arrow::DoubleBuilder exit_price_builder;
    arrow::DoubleBuilder pnl_builder;
    arrow::DoubleBuilder return_pct_builder;
    arrow::DoubleBuilder entry_signal_builder;
    arrow::DoubleBuilder exit_signal_builder;
    arrow::BooleanBuilder is_long_builder;
    arrow::StringBuilder side_label_builder;

    const size_t totalTrades = trades.size();
    ARROW_RETURN_NOT_OK(entry_ts_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(exit_ts_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(trade_index_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(fold_index_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(position_size_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(entry_price_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(exit_price_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(pnl_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(return_pct_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(entry_signal_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(exit_signal_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(is_long_builder.Reserve(totalTrades));
    ARROW_RETURN_NOT_OK(side_label_builder.Reserve(totalTrades));

    for (size_t i = 0; i < trades.size(); ++i) {
        const auto& trade = trades[i];
        ARROW_RETURN_NOT_OK(AppendTimestampOrNull(entry_ts_builder, trade.entry_timestamp));
        ARROW_RETURN_NOT_OK(AppendTimestampOrNull(exit_ts_builder, trade.exit_timestamp));
        ARROW_RETURN_NOT_OK(trade_index_builder.Append(static_cast<int64_t>(i)));
        ARROW_RETURN_NOT_OK(fold_index_builder.Append(trade.fold_index));
        ARROW_RETURN_NOT_OK(position_size_builder.Append(trade.quantity));
        ARROW_RETURN_NOT_OK(entry_price_builder.Append(trade.entry_price));
        ARROW_RETURN_NOT_OK(exit_price_builder.Append(trade.exit_price));
        ARROW_RETURN_NOT_OK(pnl_builder.Append(trade.pnl));
        ARROW_RETURN_NOT_OK(return_pct_builder.Append(trade.return_pct));
        ARROW_RETURN_NOT_OK(entry_signal_builder.Append(trade.entry_signal));
        ARROW_RETURN_NOT_OK(exit_signal_builder.Append(trade.exit_signal));
        ARROW_RETURN_NOT_OK(is_long_builder.Append(trade.is_long));
        ARROW_RETURN_NOT_OK(side_label_builder.Append(trade.is_long ? "long" : "short"));
    }

    std::vector<std::shared_ptr<arrow::Field>> fields = {
        arrow::field("timestamp_unix", arrow::int64()),
        arrow::field("exit_timestamp_unix", arrow::int64()),
        arrow::field("trade_index", arrow::int64()),
        arrow::field("fold_index", arrow::int32()),
        arrow::field("position_size", arrow::float64()),
        arrow::field("entry_price", arrow::float64()),
        arrow::field("exit_price", arrow::float64()),
        arrow::field("pnl", arrow::float64()),
        arrow::field("return_pct", arrow::float64()),
        arrow::field("entry_signal", arrow::float64()),
        arrow::field("exit_signal", arrow::float64()),
        arrow::field("is_long", arrow::boolean()),
        arrow::field("side_label", arrow::utf8())
    };

    std::vector<std::shared_ptr<arrow::Array>> arrays;
    ARROW_RETURN_NOT_OK(entry_ts_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(exit_ts_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(trade_index_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(fold_index_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(position_size_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(entry_price_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(exit_price_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(pnl_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(return_pct_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(entry_signal_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(exit_signal_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(is_long_builder.Finish(&arrays.emplace_back()));
    ARROW_RETURN_NOT_OK(side_label_builder.Finish(&arrays.emplace_back()));

    std::vector<std::shared_ptr<arrow::ChunkedArray>> columns;
    columns.reserve(arrays.size());
    for (const auto& array : arrays) {
        columns.push_back(std::make_shared<arrow::ChunkedArray>(array));
    }

    auto table = arrow::Table::Make(arrow::schema(fields), columns);
    chronosflow::AnalyticsDataFrame dataframe(table);
    return dataframe;
}

} // namespace

bool ExportWalkforwardPredictions(const simulation::SimulationRun& run,
                                  const Stage1MetadataWriter::WalkforwardRecord& record,
                                  const ExportOptions& options,
                                  std::string* error) {
    if (run.all_test_predictions.empty()) {
        return true;
    }

    auto dataframeResult = BuildWalkforwardDataFrame(run, record);
    if (!dataframeResult.ok()) {
        if (error) {
            *error = dataframeResult.status().ToString();
        }
        return false;
    }

    DataFrameGateway gateway;
    ExportSpec spec;
    spec.measurement = run.prediction_measurement.empty()
        ? (record.prediction_measurement.empty() ? "walkforward_predictions" : record.prediction_measurement)
        : run.prediction_measurement;

    ExportResult exportResult;
    if (!gateway.Export(dataframeResult.ValueOrDie(), spec, &exportResult, error)) {
        if (error && error->empty()) {
            *error = "QuestDB export failed for walkforward predictions.";
        }
        return false;
    }

    (void)options;
    return true;
}

bool ExportTradingSimulation(const Stage1MetadataWriter::SimulationRecord& record,
                             const std::vector<ExecutedTrade>& trades,
                             const ExportOptions& options,
                             std::string* error) {
    if (trades.empty()) {
        return true;
    }

    auto dataframeResult = BuildTradeDataFrame(trades);
    if (!dataframeResult.ok()) {
        if (error) {
            *error = dataframeResult.status().ToString();
        }
        return false;
    }

    DataFrameGateway gateway;
    ExportSpec spec;
    spec.measurement = record.questdb_namespace.empty() ? "trading_sim_traces" : record.questdb_namespace;
    spec.static_tags["dataset_id"] = record.dataset_id;
    spec.static_tags["run_id"] = record.run_id;
    spec.static_tags["simulation_id"] = record.simulation_id;
    if (!record.mode.empty()) {
        spec.static_tags["mode"] = record.mode;
    }
    spec.tag_columns.push_back("side_label");

    ExportResult exportResult;
    if (!gateway.Export(dataframeResult.ValueOrDie(), spec, &exportResult, error)) {
        if (error && error->empty()) {
            *error = "QuestDB export failed for trading simulations.";
        }
        return false;
    }

    (void)options;
    return true;
}

} // namespace questdb

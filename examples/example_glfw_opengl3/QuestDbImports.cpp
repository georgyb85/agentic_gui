#include "QuestDbImports.h"

#include "QuestDbDataFrameGateway.h"
#include "chronosflow.h"
#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/scalar.h>
#include <arrow/type.h>
#include <limits>
#include <initializer_list>
#include <cmath>

namespace questdb {
namespace {

int ResolveFieldIndex(const std::shared_ptr<arrow::Schema>& schema,
                      std::initializer_list<const char*> candidates) {
    if (!schema) {
        return -1;
    }
    for (const char* candidate : candidates) {
        if (!candidate) continue;
        int idx = schema->GetFieldIndex(candidate);
        if (idx >= 0) {
            return idx;
        }
    }
    return -1;
}

std::string JoinSchemaFields(const std::shared_ptr<arrow::Schema>& schema) {
    if (!schema) return {};
    std::string out;
    for (int i = 0; i < schema->num_fields(); ++i) {
        if (i > 0) out += ", ";
        out += schema->field(i)->name();
    }
    return out;
}

std::shared_ptr<arrow::ChunkedArray> ColumnOrNull(const std::shared_ptr<arrow::Table>& table,
                                                  int index) {
    if (!table || index < 0 || index >= table->num_columns()) {
        return nullptr;
    }
    return table->column(index);
}

double ScalarToDouble(const std::shared_ptr<arrow::Scalar>& scalar) {
    if (!scalar || !scalar->is_valid) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    switch (scalar->type->id()) {
        case arrow::Type::DOUBLE:
            return std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value;
        case arrow::Type::FLOAT:
            return static_cast<double>(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value);
        case arrow::Type::INT64:
            return static_cast<double>(std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value);
        case arrow::Type::INT32:
            return static_cast<double>(std::static_pointer_cast<arrow::Int32Scalar>(scalar)->value);
        default:
            break;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

int64_t ScalarToInt64(const std::shared_ptr<arrow::Scalar>& scalar) {
    if (!scalar || !scalar->is_valid) {
        return 0;
    }
    switch (scalar->type->id()) {
        case arrow::Type::INT64:
            return std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value;
        case arrow::Type::INT32:
            return static_cast<int64_t>(std::static_pointer_cast<arrow::Int32Scalar>(scalar)->value);
        case arrow::Type::DOUBLE:
            return static_cast<int64_t>(std::llround(std::static_pointer_cast<arrow::DoubleScalar>(scalar)->value));
        case arrow::Type::FLOAT:
            return static_cast<int64_t>(std::llround(std::static_pointer_cast<arrow::FloatScalar>(scalar)->value));
        case arrow::Type::TIMESTAMP: {
            auto tsScalar = std::static_pointer_cast<arrow::TimestampScalar>(scalar);
            return tsScalar->value;
        }
        default:
            break;
    }
    return 0;
}

int64_t NormalizeTimestampMs(int64_t raw) {
    if (raw <= 0) {
        return raw;
    }
    // Heuristics based on magnitude
    if (raw > 10'000'000'000'000LL) { // nanoseconds
        return raw / 1'000'000LL;
    }
    if (raw > 10'000'000'000LL) { // microseconds
        return raw / 1'000LL;
    }
    if (raw < 1'000'000'000LL) { // likely seconds
        return raw * 1000LL;
    }
    // assume already in milliseconds
    return raw;
}

int32_t ScalarToInt32(const std::shared_ptr<arrow::Scalar>& scalar) {
    if (!scalar || !scalar->is_valid) {
        return 0;
    }
    switch (scalar->type->id()) {
        case arrow::Type::INT32:
            return std::static_pointer_cast<arrow::Int32Scalar>(scalar)->value;
        case arrow::Type::INT64:
            return static_cast<int32_t>(std::static_pointer_cast<arrow::Int64Scalar>(scalar)->value);
        default:
            break;
    }
    return 0;
}

} // namespace

bool ImportWalkforwardPredictions(const std::string& measurement,
                                  WalkforwardPredictionSeries* series,
                                  std::string* error) {
    if (!series) {
        if (error) *error = "Prediction series pointer is null.";
        return false;
    }
    series->rows.clear();

    if (measurement.empty()) {
        if (error) *error = "Measurement name cannot be empty.";
        return false;
    }

    DataFrameGateway gateway;
    auto result = gateway.Import(measurement);
    if (!result.ok()) {
        if (error) *error = result.status().ToString();
        return false;
    }

    const auto table = result.ValueOrDie().get_cpu_table();
    if (!table || table->num_rows() == 0) {
        if (error) *error = "QuestDB measurement '" + measurement + "' is empty.";
        return false;
    }

    auto schema = table->schema();
    const int tsIndex = ResolveFieldIndex(schema, {"timestamp_unix", "timestamp", "ts"});
    const int barIndex = ResolveFieldIndex(schema, {"bar_index", "index"});
    const int foldIndex = ResolveFieldIndex(schema, {"fold_number", "fold"});
    const int predictionIndex = ResolveFieldIndex(schema, {"prediction", "prediction_value"});
    const int targetIndex = ResolveFieldIndex(schema, {"target_value", "target"});
    const int longThresholdIndex = ResolveFieldIndex(schema, {"long_threshold"});
    const int shortThresholdIndex = ResolveFieldIndex(schema, {"short_threshold"});
    const int rocThresholdIndex = ResolveFieldIndex(schema, {"roc_threshold", "prediction_threshold"});
    const int shortEntryThresholdIndex = ResolveFieldIndex(schema, {"short_entry_threshold"});
    const int foldScoreIndex = ResolveFieldIndex(schema, {"fold_score", "best_score"});
    const int foldPfIndex = ResolveFieldIndex(schema, {"fold_profit_factor"});

    if (tsIndex < 0 || predictionIndex < 0) {
        if (error) {
            *error = "QuestDB measurement '" + measurement
                + "' is missing required columns (needs at least timestamp + prediction). "
                + "Available columns: [" + JoinSchemaFields(schema) + "]";
        }
        return false;
    }

    auto tsColumn = ColumnOrNull(table, tsIndex);
    auto barColumn = ColumnOrNull(table, barIndex);
    auto foldColumn = ColumnOrNull(table, foldIndex);
    auto predictionColumn = ColumnOrNull(table, predictionIndex);
    auto targetColumn = ColumnOrNull(table, targetIndex);
    auto longColumn = ColumnOrNull(table, longThresholdIndex);
    auto shortColumn = ColumnOrNull(table, shortThresholdIndex);
    auto rocColumn = ColumnOrNull(table, rocThresholdIndex);
    auto shortEntryColumn = ColumnOrNull(table, shortEntryThresholdIndex);
    auto scoreColumn = ColumnOrNull(table, foldScoreIndex);
    auto pfColumn = ColumnOrNull(table, foldPfIndex);

    const int64_t totalRows = table->num_rows();
    series->rows.reserve(static_cast<size_t>(totalRows));

    for (int64_t row = 0; row < totalRows; ++row) {
        auto tsScalar = tsColumn ? tsColumn->GetScalar(row) : arrow::Result<std::shared_ptr<arrow::Scalar>>();
        auto predictionScalar = predictionColumn ? predictionColumn->GetScalar(row) : arrow::Result<std::shared_ptr<arrow::Scalar>>();
        if (!tsScalar.ok() || !predictionScalar.ok()) {
            continue;
        }

        WalkforwardPredictionSeries::Entry entry;
        entry.timestamp_ms = NormalizeTimestampMs(ScalarToInt64(tsScalar.ValueOrDie()));
        entry.bar_index = barColumn ? ScalarToInt64(barColumn->GetScalar(row).ValueOrDie()) : row;
        entry.fold_number = foldColumn ? ScalarToInt32(foldColumn->GetScalar(row).ValueOrDie()) : 0;
        entry.prediction = ScalarToDouble(predictionScalar.ValueOrDie());
        entry.target = targetColumn ? ScalarToDouble(targetColumn->GetScalar(row).ValueOrDie()) : std::numeric_limits<double>::quiet_NaN();
        entry.long_threshold = longColumn ? ScalarToDouble(longColumn->GetScalar(row).ValueOrDie()) : std::numeric_limits<double>::quiet_NaN();
        entry.short_threshold = shortColumn ? ScalarToDouble(shortColumn->GetScalar(row).ValueOrDie()) : std::numeric_limits<double>::quiet_NaN();
        entry.roc_threshold = rocColumn ? ScalarToDouble(rocColumn->GetScalar(row).ValueOrDie()) : std::numeric_limits<double>::quiet_NaN();
        entry.short_entry_threshold = shortEntryColumn ? ScalarToDouble(shortEntryColumn->GetScalar(row).ValueOrDie()) : std::numeric_limits<double>::quiet_NaN();
        entry.fold_score = scoreColumn ? ScalarToDouble(scoreColumn->GetScalar(row).ValueOrDie()) : std::numeric_limits<double>::quiet_NaN();
        entry.fold_profit_factor = pfColumn ? ScalarToDouble(pfColumn->GetScalar(row).ValueOrDie()) : std::numeric_limits<double>::quiet_NaN();

        series->rows.push_back(entry);
    }

    if (series->rows.empty()) {
        if (error) *error = "QuestDB measurement '" + measurement + "' contains no usable rows.";
        return false;
    }

    return true;
}

} // namespace questdb

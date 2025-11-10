#pragma once

#include <memory>
#include <cstddef>
#include <typeinfo>
#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/array/array_primitive.h>
#include <arrow/array/concatenate.h>
#include <arrow/type_traits.h>

#ifdef WITH_CUDA
#include <cudf/table/table.hpp>
#endif

namespace chronosflow {

enum class DeviceType {
    CPU,
    GPU
};

template<typename T>
class ColumnView {
public:
    ColumnView() = delete;
    ColumnView(const ColumnView&) = delete;
    ColumnView& operator=(const ColumnView&) = delete;

    ColumnView(ColumnView&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , device_type_(other.device_type_)
        , lifetime_sentinel_(std::move(other.lifetime_sentinel_)) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    ColumnView& operator=(ColumnView&& other) noexcept {
        if (this != &other) {
            data_ = other.data_;
            size_ = other.size_;
            device_type_ = other.device_type_;
            lifetime_sentinel_ = std::move(other.lifetime_sentinel_);
            
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    ~ColumnView() = default;

    const T* data() const { return data_; }
    std::size_t size() const { return size_; }
    DeviceType device_type() const { return device_type_; }

    static arrow::Result<ColumnView<T>> from_arrow_column(
        std::shared_ptr<arrow::Table> table, 
        const std::string& column_name);

#ifdef WITH_CUDA
    static arrow::Result<ColumnView<T>> from_cudf_column(
        std::shared_ptr<cudf::table> table,
        std::size_t column_index);
#endif

private:
    ColumnView(const T* data, std::size_t size, DeviceType device_type,
               std::shared_ptr<void> lifetime_sentinel)
        : data_(data)
        , size_(size)
        , device_type_(device_type)
        , lifetime_sentinel_(lifetime_sentinel) {}

    const T* data_;
    std::size_t size_;
    DeviceType device_type_;
    std::shared_ptr<void> lifetime_sentinel_;
};

template<typename T>
arrow::Result<ColumnView<T>> ColumnView<T>::from_arrow_column(
    std::shared_ptr<arrow::Table> table,
    const std::string& column_name) {
    
    if (!table) {
        return arrow::Status::Invalid("Input table is null");
    }

    auto column = table->GetColumnByName(column_name);
    if (!column) {
        return arrow::Status::Invalid("Column not found: ", column_name);
    }
    
    std::shared_ptr<arrow::Array> array;
    std::shared_ptr<void> lifetime_sentinel;

    // Handle multi-chunk columns by combining them into a single contiguous array.
    if (column->num_chunks() == 0) {
        return ColumnView<T>(nullptr, 0, DeviceType::CPU, table);
    } else if (column->num_chunks() == 1) {
        array = column->chunk(0);
        lifetime_sentinel = table; // Lifetime is tied to the original table
    } else {
        // arrow::ChunkedArray::chunks() returns the vector directly.
        auto combined_result = arrow::Concatenate(column->chunks());
        if (!combined_result.ok()) {
            return combined_result.status();
        }
        array = combined_result.ValueOrDie();
        // The lifetime sentinel must now hold the new combined array.
        lifetime_sentinel = array; 
    }
    
    // Use type traits for safer casting.
    using ArrowType = typename arrow::CTypeTraits<T>::ArrowType;
    using ArrayType = typename arrow::TypeTraits<ArrowType>::ArrayType;
    
    auto typed_array = std::dynamic_pointer_cast<ArrayType>(array);

    if (!typed_array) {
        return arrow::Status::Invalid("Type mismatch: Cannot cast column '", column_name, 
                                      "' to the requested type '", typeid(T).name(), "'.");
    }

    const T* data_ptr = typed_array->raw_values();
    std::size_t size = typed_array->length();

    return ColumnView<T>(data_ptr, size, DeviceType::CPU, lifetime_sentinel);
}

#ifdef WITH_CUDA
template<typename T>
arrow::Result<ColumnView<T>> ColumnView<T>::from_cudf_column(
    std::shared_ptr<cudf::table> table,
    std::size_t column_index) {
    
    if (column_index >= table->num_columns()) {
        return arrow::Status::Invalid("Column index out of range");
    }
    
    auto& column = table->get_column(column_index);
    const T* data_ptr = static_cast<const T*>(column.data<T>());
    std::size_t size = column.size();
    
    return ColumnView<T>(data_ptr, size, DeviceType::GPU, table);
}
#endif

} // namespace chronosflow
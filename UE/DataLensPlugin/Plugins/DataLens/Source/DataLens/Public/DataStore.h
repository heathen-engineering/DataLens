/******************************************************************************
 * DataStore.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * High-performance, column-oriented in-memory data table with dynamic
 * per-column stride. Supports both raw and safe access to table cells.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2025-11-14
 ******************************************************************************/

#pragma once

#include "DataLensSchema.h"

 /// <summary>
 /// DataStore provides a column-oriented, in-memory table of raw bytes with
 /// flexible per-column stride, supporting both unchecked raw access and safe
 /// bounds-checked operations. Useful as a low-level data container for
 /// structured, high-performance storage and serialization.
 /// </summary>
class DataStore
{
public:
    DataStore() = default;

    /// <summary>
    /// Initialize with column metadata and preallocate rows.
    /// </summary>
    /// <param name="columns"></param>
    /// <param name="preallocRows"></param>
    DataStore(const std::vector<ColumnSchema>& columns, size_t preallocRows);

    /// <summary>
    /// Initialize with column metadata and pre-load data from a byte array (row-major layout).
    /// </summary>
    /// <param name="columns"></param>
    /// <param name="data"></param>
    DataStore(const std::vector<ColumnSchema>& columns, const std::vector<uint8_t>& data);

    /// <summary>
    /// Initialize with column metadata, pre-load data, and optionally preallocate extra rows.
    /// </summary>
    /// <param name="columns"></param>
    /// <param name="data"></param>
    /// <param name="extraRows"></param>
    DataStore(const std::vector<ColumnSchema>& columns, const std::vector<uint8_t>& data, size_t extraRows);

    /// <summary>
    /// Get a typed value from a cell (unchecked).
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <returns></returns>
    template<typename T>
    T GetRaw(size_t row, size_t col) const
    {
        T value;
        std::memcpy(&value, GetRawCell(row, col), sizeof(T));
        return value;
    }

    /// <summary>
    /// Set a typed value to a cell (unchecked).
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <param name="value"></param>
    template<typename T>
    void SetRaw(size_t row, size_t col, const T& value)
    {
        std::memcpy(GetRawCellMutable(row, col), &value, sizeof(T));
    }

    /// <summary>
    /// Try to read a cell safely with stride-awareness.
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <param name="outValue"></param>
    /// <returns></returns>
    template<typename T>
    bool TryGet(size_t row, size_t col, T& outValue) const
    {
        if (!IsValid(row, col)) return false;
        outValue = T{};
        size_t copySize = std::min(sizeof(T), mColumns[col].GetStride());
        std::memcpy(&outValue, GetRawCell(row, col), copySize);
        return true;
    }

    /// <summary>
    /// Try to write a cell safely with stride-awareness.
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <param name="value"></param>
    /// <returns></returns>
    template<typename T>
    bool TrySet(size_t row, size_t col, const T& value)
    {
        if (!IsValid(row, col)) return false;
        size_t copySize = std::min(sizeof(T), mColumns[col].GetStride());
        std::memcpy(GetRawCellMutable(row, col), &value, copySize);
        return true;
    }

    /// <summary>
    /// Load raw row-major data into the datastore.
    /// </summary>
    /// <param name="src"></param>
    void LoadRaw(const std::vector<uint8_t>& src);

    /// <summary>
    /// Dump the current table data in row-major layout.
    /// </summary>
    /// <returns></returns>
    std::vector<uint8_t> Dump() const;

    size_t GetRowCount() const;
    size_t GetColumnCount() const;
    size_t GetColumnStride(size_t col) const;

    /// <summary>
    /// Total bytes per row (sum of all column strides).
    /// </summary>
    /// <returns></returns>
    size_t GetRowStride() const;
    void ConvertToSchema(const StoreSchema& newSchema);

private:
    std::vector<ColumnSchema> mColumns;
    std::vector<std::vector<uint8_t>> mColumnsData; // column-major storage
    size_t mRowCount{ 0 };

    /// <summary>
    /// Get a pointer to a raw cell (unchecked).
    /// </summary>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <returns></returns>
    uint8_t* GetRawCellMutable(size_t row, size_t col);

    /// <summary>
    /// Get a pointer to a raw cell (unchecked).
    /// </summary>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <returns></returns>
    const uint8_t* GetRawCell(size_t row, size_t col) const;

    void InitializeColumns(size_t rows);

    void LoadDataFromRowMajor(const std::vector<uint8_t>& src, size_t rows);

    bool IsValid(size_t row, size_t col) const;
};

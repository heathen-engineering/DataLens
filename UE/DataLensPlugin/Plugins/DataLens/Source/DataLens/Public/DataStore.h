/******************************************************************************
 * DataStore.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * High-performance, column-oriented in-memory data table with dynamic
 * per-column stride. Supports both raw and safe access to table cells.
 *
 * Author: James McGhee
 * Date:   2025-11-04
 ******************************************************************************/

#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

 /// <summary>
 /// DataStore provides a column-oriented, in-memory table of raw bytes with
 /// flexible per-column stride, supporting both unchecked raw access and safe
 /// bounds-checked operations. Useful as a low-level data container for
 /// structured, high-performance storage and serialization.
 /// </summary>
class DataStore
{
public:
    /// <summary>
    /// Metadata per column
    /// </summary>
    struct ColumnMeta
    {
        size_t stride; // number of bytes this column occupies
        // Potential future flags: endian, type hints, etc.
    };

    DataStore() = default;

    /// <summary>
    /// Initialize with column metadata and preallocate rows.
    /// </summary>
    /// <param name="columns"></param>
    /// <param name="preallocRows"></param>
    DataStore(const std::vector<ColumnMeta>& columns, size_t preallocRows)
        : mColumns(columns), mRowCount(preallocRows)
    {
        mColumnsData.resize(columns.size());
        for (size_t c = 0; c < columns.size(); ++c)
            mColumnsData[c].resize(columns[c].stride * preallocRows);
    }

    /// <summary>
    /// Initialize with column metadata and pre-load data from a byte array (row-major layout).
    /// </summary>
    /// <param name="columns"></param>
    /// <param name="data"></param>
    DataStore(const std::vector<ColumnMeta>& columns, const std::vector<uint8_t>& data)
        : mColumns(columns)
    {
        size_t rowStride = GetRowStride();
        if (data.size() % rowStride != 0)
            throw std::runtime_error("Data size is not a multiple of row stride");

        size_t rows = data.size() / rowStride;
        InitializeColumns(rows);
        LoadDataFromRowMajor(data, rows);
    }

    /// <summary>
    /// Initialize with column metadata, pre-load data, and optionally preallocate extra rows.
    /// </summary>
    /// <param name="columns"></param>
    /// <param name="data"></param>
    /// <param name="extraRows"></param>
    DataStore(const std::vector<ColumnMeta>& columns, const std::vector<uint8_t>& data, size_t extraRows)
        : mColumns(columns)
    {
        size_t rowStride = GetRowStride();
        if (data.size() % rowStride != 0)
            throw std::runtime_error("Data size is not a multiple of row stride");

        size_t loadedRows = data.size() / rowStride;
        InitializeColumns(loadedRows + extraRows);
        LoadDataFromRowMajor(data, loadedRows);
    }

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
        std::memcpy(GetRawCell(row, col), &value, sizeof(T));
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
        size_t copySize = std::min(sizeof(T), mColumns[col].stride);
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
        size_t copySize = std::min(sizeof(T), mColumns[col].stride);
        std::memcpy(GetRawCell(row, col), &value, copySize);
        return true;
    }

    /// <summary>
    /// Load raw row-major data into the datastore.
    /// </summary>
    /// <param name="src"></param>
    void LoadRaw(const std::vector<uint8_t>& src)
    {
        size_t rows = src.size() / GetRowStride();
        InitializeColumns(rows);
        LoadDataFromRowMajor(src, rows);
    }

    /// <summary>
    /// Dump the current table data in row-major layout.
    /// </summary>
    /// <returns></returns>
    std::vector<uint8_t> Dump() const
    {
        size_t rowStride = GetRowStride();
        std::vector<uint8_t> out(rowStride * mRowCount);
        for (size_t r = 0; r < mRowCount; ++r)
        {
            size_t offset = r * rowStride;
            for (size_t c = 0; c < mColumns.size(); ++c)
            {
                std::memcpy(out.data() + offset, GetRawCell(r, c), mColumns[c].stride);
                offset += mColumns[c].stride;
            }
        }
        return out;
    }

    size_t GetRowCount() const { return mRowCount; }
    size_t GetColumnCount() const { return mColumns.size(); }
    size_t GetColumnStride(size_t col) const { return mColumns[col].stride; }

    /// <summary>
    /// Total bytes per row (sum of all column strides).
    /// </summary>
    /// <returns></returns>
    size_t GetRowStride() const
    {
        size_t stride = 0;
        for (auto& col : mColumns) stride += col.stride;
        return stride;
    }

private:
    std::vector<ColumnMeta> mColumns;
    std::vector<std::vector<uint8_t>> mColumnsData; // column-major storage
    size_t mRowCount{ 0 };

    /// <summary>
    /// Get a pointer to a raw cell (unchecked).
    /// </summary>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <returns></returns>
    uint8_t* GetRawCellMutable(size_t row, size_t col)
    {
        return mColumnsData[col].data() + row * mColumns[col].stride;
    }

    /// <summary>
    /// Get a pointer to a raw cell (unchecked).
    /// </summary>
    /// <param name="row"></param>
    /// <param name="col"></param>
    /// <returns></returns>
    const uint8_t* GetRawCell(size_t row, size_t col) const
    {
        return mColumnsData[col].data() + row * mColumns[col].stride;
    }

    void InitializeColumns(size_t rows)
    {
        mRowCount = rows;
        mColumnsData.resize(mColumns.size());
        for (size_t c = 0; c < mColumns.size(); ++c)
            mColumnsData[c].resize(mColumns[c].stride * rows);
    }

    void LoadDataFromRowMajor(const std::vector<uint8_t>& src, size_t rows)
    {
        size_t rowStride = GetRowStride();
        for (size_t r = 0; r < rows; ++r)
        {
            size_t srcOffset = r * rowStride;
            size_t dstOffset = 0;
            for (size_t c = 0; c < mColumns.size(); ++c)
            {
                std::memcpy(mColumnsData[c].data() + r * mColumns[c].stride,
                    src.data() + srcOffset + dstOffset,
                    mColumns[c].stride);
                dstOffset += mColumns[c].stride;
            }
        }
    }

    bool IsValid(size_t row, size_t col) const
    {
        return row < mRowCount && col < mColumns.size();
    }
};

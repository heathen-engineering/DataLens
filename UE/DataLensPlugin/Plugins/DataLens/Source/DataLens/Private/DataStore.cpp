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

#include "DataStore.h"

/// <summary>
/// Initialize with column metadata and preallocate rows.
/// </summary>
/// <param name="columns"></param>
/// <param name="preallocRows"></param>
DataStore::DataStore(const std::vector<ColumnSchema>& columns, size_t preallocRows)
    : mColumns(columns), mRowCount(preallocRows)
{
    mColumnsData.resize(columns.size());
    for (size_t c = 0; c < columns.size(); ++c)
        mColumnsData[c].resize(columns[c].GetStride() * preallocRows);
}

/// <summary>
/// Initialize with column metadata and pre-load data from a byte array (row-major layout).
/// </summary>
/// <param name="columns"></param>
/// <param name="data"></param>
DataStore::DataStore(const std::vector<ColumnSchema>& columns, const std::vector<uint8_t>& data)
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
DataStore::DataStore(const std::vector<ColumnSchema>& columns, const std::vector<uint8_t>& data, size_t extraRows)
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
/// Load raw row-major data into the datastore.
/// </summary>
/// <param name="src"></param>
void DataStore::LoadRaw(const std::vector<uint8_t>& src)
{
    size_t rowStride = GetRowStride();
    if (rowStride == 0)
        throw std::runtime_error("Cannot load data: row stride is zero (no columns defined)");

    size_t rows = src.size() / rowStride;
    InitializeColumns(rows);
    LoadDataFromRowMajor(src, rows);
}

/// <summary>
/// Dump the current table data in row-major layout.
/// </summary>
/// <returns></returns>
std::vector<uint8_t> DataStore::Dump() const
{
    size_t rowStride = GetRowStride();
    std::vector<uint8_t> out(rowStride * mRowCount);
    for (size_t r = 0; r < mRowCount; ++r)
    {
        size_t offset = r * rowStride;
        for (size_t c = 0; c < mColumns.size(); ++c)
        {
            std::memcpy(out.data() + offset, GetRawCell(r, c), mColumns[c].GetStride());
            offset += mColumns[c].GetStride();
        }
    }
    return out;
}

size_t DataStore::GetRowCount() const { return mRowCount; }

size_t DataStore::GetColumnCount() const { return mColumns.size(); }

size_t DataStore::GetColumnStride(size_t col) const { return mColumns[col].GetStride(); }

/// <summary>
/// Total bytes per row (sum of all column strides).
/// </summary>
/// <returns></returns>
size_t DataStore::GetRowStride() const
{
    size_t stride = 0;
    for (auto& col : mColumns) stride += col.GetStride();
    return stride;
}

void DataStore::ConvertToSchema(const StoreSchema& newSchema)
{
    std::vector<std::vector<uint8_t>> newColumnsData;
    newColumnsData.reserve(newSchema.Columns.size());

    size_t rowCount = mRowCount;

    for (const ColumnSchema& newCol : newSchema.Columns)
    {
        size_t newStride = newCol.GetStride();
        std::vector<uint8_t> newColData(rowCount * newStride, 0); // zero-initialize

        auto it = std::find_if(mColumns.begin(), mColumns.end(),
            [&newCol](const ColumnSchema& oldCol) { return oldCol.Name == newCol.Name; });

        if (it != mColumns.end())
        {
            size_t oldStride = it->GetStride();
            size_t colIndex = std::distance(mColumns.begin(), it);
            const std::vector<uint8_t>& oldData = mColumnsData[colIndex];

            for (size_t row = 0; row < rowCount; ++row)
            {
                const uint8_t* src = oldData.data() + row * oldStride;
                uint8_t* dst = newColData.data() + row * newStride;

                ColumnSchema::ConvertCell(src, it->DataType, dst, newCol.DataType);
            }
        }
        else if (!newCol.DefaultValue.empty())
        {
            size_t copySize = std::min(newStride, newCol.DefaultValue.size());
            for (size_t row = 0; row < rowCount; ++row)
            {
                std::memcpy(newColData.data() + row * newStride, newCol.DefaultValue.data(), copySize);
                if (copySize < newStride)
                {
                    std::memset(newColData.data() + row * newStride + copySize, 0, newStride - copySize);
                }
            }
        }
        // else: column missing and no default, already zeroed above

        newColumnsData.push_back(std::move(newColData));
    }

    mColumns = newSchema.Columns;
    mColumnsData = std::move(newColumnsData);
}

/// <summary>
/// Get a pointer to a raw cell (unchecked).
/// </summary>
/// <param name="row"></param>
/// <param name="col"></param>
/// <returns></returns>
uint8_t* DataStore::GetRawCellMutable(size_t row, size_t col)
{
    return mColumnsData[col].data() + row * mColumns[col].GetStride();
}

/// <summary>
/// Get a pointer to a raw cell (unchecked).
/// </summary>
/// <param name="row"></param>
/// <param name="col"></param>
/// <returns></returns>
const uint8_t* DataStore::GetRawCell(size_t row, size_t col) const
{
    return mColumnsData[col].data() + row * mColumns[col].GetStride();
}

void DataStore::InitializeColumns(size_t rows)
{
    mRowCount = rows;
    mColumnsData.resize(mColumns.size());
    for (size_t c = 0; c < mColumns.size(); ++c)
        mColumnsData[c].resize(mColumns[c].GetStride() * rows);
}

void DataStore::LoadDataFromRowMajor(const std::vector<uint8_t>& src, size_t rows)
{
    size_t rowStride = GetRowStride();
    for (size_t r = 0; r < rows; ++r)
    {
        size_t srcOffset = r * rowStride;
        size_t dstOffset = 0;
        for (size_t c = 0; c < mColumns.size(); ++c)
        {
            std::memcpy(mColumnsData[c].data() + r * mColumns[c].GetStride(),
                src.data() + srcOffset + dstOffset,
                mColumns[c].GetStride());
            dstOffset += mColumns[c].GetStride();
        }
    }
}

bool DataStore::IsValid(size_t row, size_t col) const
{
    return row < mRowCount && col < mColumns.size();
}

/******************************************************************************
 * c_api.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Thin C ABI shim over DataStore. No engine types cross the boundary.
 ******************************************************************************/

#include "datalens/c_api.h"

#include "datalens/DataStore.h"
#include "datalens/DataLensSchema.h"

#include <string>
#include <vector>

namespace
{
    DataStore* AsStore(dl_store* s) { return reinterpret_cast<DataStore*>(s); }
    const DataStore* AsStore(const dl_store* s) { return reinterpret_cast<const DataStore*>(s); }
}

extern "C" {

int32_t dl_abi_version(void)
{
    return 1;
}

int32_t dl_smallest_uint_type(uint64_t maxValue)
{
    return static_cast<int32_t>(DataLensValueTypeUtils::SmallestUnsignedForMax(maxValue));
}

int32_t dl_smallest_int_type(int64_t minValue, int64_t maxValue)
{
    return static_cast<int32_t>(DataLensValueTypeUtils::SmallestSignedForRange(minValue, maxValue));
}

dl_store* dl_store_create(const char* const* colNames,
                          const int32_t* colTypes,
                          int32_t colCount,
                          uint64_t preallocRows)
{
    if (colCount <= 0 || colNames == nullptr || colTypes == nullptr)
        return nullptr;

    std::vector<DataStoreColumnSchema> cols;
    cols.reserve(static_cast<size_t>(colCount));
    for (int32_t i = 0; i < colCount; ++i)
    {
        DataStoreColumnSchema c;
        c.Name = colNames[i] ? colNames[i] : "";
        c.Type = static_cast<DataLensValueType>(colTypes[i]);
        cols.push_back(std::move(c));
    }

    return reinterpret_cast<dl_store*>(new DataStore(cols, static_cast<size_t>(preallocRows)));
}

void dl_store_destroy(dl_store* store)
{
    delete AsStore(store);
}

uint64_t dl_store_row_count(const dl_store* store)
{
    return store ? AsStore(store)->GetRowCount() : 0;
}

uint64_t dl_store_column_count(const dl_store* store)
{
    return store ? AsStore(store)->GetColumnCount() : 0;
}

uint64_t dl_store_row_stride(const dl_store* store)
{
    return store ? AsStore(store)->GetRowStride() : 0;
}

int32_t dl_store_set_f32(dl_store* store, uint64_t row, uint64_t col, float value)
{
    if (!store) return 0;
    return AsStore(store)->TrySet<float>(static_cast<size_t>(row), static_cast<size_t>(col), value) ? 1 : 0;
}

int32_t dl_store_get_f32(const dl_store* store, uint64_t row, uint64_t col, float* out)
{
    if (!store || !out) return 0;
    return AsStore(store)->TryGet<float>(static_cast<size_t>(row), static_cast<size_t>(col), *out) ? 1 : 0;
}

int32_t dl_store_set_i32(dl_store* store, uint64_t row, uint64_t col, int32_t value)
{
    if (!store) return 0;
    return AsStore(store)->TrySet<int32_t>(static_cast<size_t>(row), static_cast<size_t>(col), value) ? 1 : 0;
}

int32_t dl_store_get_i32(const dl_store* store, uint64_t row, uint64_t col, int32_t* out)
{
    if (!store || !out) return 0;
    return AsStore(store)->TryGet<int32_t>(static_cast<size_t>(row), static_cast<size_t>(col), *out) ? 1 : 0;
}

int32_t dl_store_set_f64(dl_store* store, uint64_t row, uint64_t col, double value)
{
    if (!store) return 0;
    return AsStore(store)->TrySet<double>(static_cast<size_t>(row), static_cast<size_t>(col), value) ? 1 : 0;
}

int32_t dl_store_get_f64(const dl_store* store, uint64_t row, uint64_t col, double* out)
{
    if (!store || !out) return 0;
    return AsStore(store)->TryGet<double>(static_cast<size_t>(row), static_cast<size_t>(col), *out) ? 1 : 0;
}

void dl_store_set_valid(dl_store* store, uint64_t row, int32_t valid)
{
    if (store) AsStore(store)->SetValid(static_cast<size_t>(row), valid != 0);
}

int32_t dl_store_is_valid(const dl_store* store, uint64_t row)
{
    return (store && AsStore(store)->IsValidRow(static_cast<size_t>(row))) ? 1 : 0;
}

uint64_t dl_store_alloc_row(dl_store* store)
{
    if (!store) return UINT64_MAX;
    size_t row = AsStore(store)->AllocRow();
    return (row == SIZE_MAX) ? UINT64_MAX : static_cast<uint64_t>(row);
}

void dl_store_free_row(dl_store* store, uint64_t row)
{
    if (store) AsStore(store)->FreeRow(static_cast<size_t>(row));
}

uint64_t dl_store_live_count(const dl_store* store)
{
    return store ? AsStore(store)->GetLiveCount() : 0;
}

uint64_t dl_store_run_f32(dl_store* store, uint64_t targetCol, int32_t op, float operand,
                          int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold)
{
    if (!store) return 0;
    return AsStore(store)->RunColumnSystem<float>(
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_store_run_i32(dl_store* store, uint64_t targetCol, int32_t op, int32_t operand,
                          int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold)
{
    if (!store) return 0;
    return AsStore(store)->RunColumnSystem<int32_t>(
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

} // extern "C"

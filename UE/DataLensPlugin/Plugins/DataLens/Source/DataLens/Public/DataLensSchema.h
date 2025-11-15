/******************************************************************************
 * DataLensSchema.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2025-11-15
 ******************************************************************************/

#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

enum class DataType : uint8_t
{
    Bool,       // 1 byte
    Int8,       // signed 8-bit
    UInt8,      // unsigned 8-bit
    Int16,      // signed 16-bit
    UInt16,     // unsigned 16-bit
    Int32,      // signed 32-bit
    UInt32,     // unsigned 32-bit
    Int64,      // signed 64-bit
    UInt64,     // unsigned 64-bit
    Float,      // 32-bit IEEE 754
    Double,     // 64-bit IEEE 754
    GUID        // 128-bit universally unique identifier
};

enum class QueryOperator : uint8_t
{
    Equals,
    NotEquals,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
    BitmaskHas,
    BitmaskNot,
    Range,
    Not
};

struct ColumnSchema
{
    std::string Name;
    DataType Type;
        
    static size_t GetStrideForType(DataType type)
    {
        switch (type)
        {
        case DataType::Bool:   return 1;
        case DataType::Int8:   return 1;
        case DataType::UInt8:  return 1;
        case DataType::Int16:  return 2;
        case DataType::UInt16: return 2;
        case DataType::Int32:  return 4;
        case DataType::UInt32: return 4;
        case DataType::Int64:  return 8;
        case DataType::UInt64: return 8;
        case DataType::Float:  return 4;
        case DataType::Double: return 8;
        case DataType::GUID:   return 16;
        }
        return 0; // should never happen
    }
    size_t GetStride() const
    {
        return GetStrideForType(Type);
    }

    static void ConvertCell(const void* src, DataType fromType, void* dst, DataType toType);

    bool Optional = false;
    std::vector<uint8_t> DefaultValue; // optional
};

struct StoreSchema
{
public:
    std::string Name;
    std::vector<ColumnSchema> Columns;
    uint64_t DefaultCapacity;
    uint32_t Version = 1;
};

class Schema
{
public:
    void AddStore(const StoreSchema& store);

    const StoreSchema* GetStore(const std::string& name) const;

    bool HasStore(const std::string& name) const;

    // Indexed access
    const StoreSchema& operator[](size_t index) const { return mStores.at(index); }
    StoreSchema& operator[](size_t index) { return mStores.at(index); }

    // Number of stores
    size_t Count() const;

private:
    std::vector<StoreSchema> mStores;
};

struct QueryJoin
{
    size_t LeftStoreIndex;
    size_t LeftColumnIndex;
    size_t RightStoreIndex;
    size_t RightColumnIndex;
};

struct QueryColumn
{
    size_t StoreIndex;
    size_t ColumnIndex;
};

struct QueryResultRow
{
    std::vector<size_t> RowIndicesPerStore; // row index in each DataStore
};

struct SortColumn
{
    size_t StoreIndex;
    size_t ColumnIndex;
    bool Descending;
};

struct QueryPredicate
{
    size_t StoreIndex;          // which DataStore this predicate targets
    size_t ColumnIndex;         // index into the DataStore schema
    DataType ColumnType;        // primitive type for conversions
    QueryOperator Op;

    union {
        int64_t IntValue;
        uint64_t UIntValue;
        double DoubleValue;
        float FloatValue;
        uint64_t BitmaskValue;    // for tag masks
    };

    union {
        int64_t IntValueHigh;      // for range queries
        uint64_t UIntValueHigh;
        double DoubleValueHigh;
        float FloatValueHigh;
    };

    size_t SubPredicateStartIndex; // index into flat predicate array
    size_t SubPredicateCount;      // number of nested predicates
};

struct QueryObject
{
    std::vector<size_t> DataStoreIndices;      // stores involved
    std::vector<QueryPredicate> Predicates;    // predicates (with StoreIndex)
    std::vector<QueryJoin> Joins;              // join conditions
    bool SelectAll = false;                    // if true, ignore SelectColumns and select all columns
    std::vector<QueryColumn> SelectColumns;    // which columns to return, in order
    std::vector<SortColumn> SortColumns;       // sort info
    size_t Limit = SIZE_MAX;
    size_t Offset = 0;
};

namespace DataLensEndian
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    constexpr bool NeedSwap = true;
#elif defined(_WIN32) || defined(__LITTLE_ENDIAN__) || defined(__i386__) || defined(__x86_64__)
    constexpr bool NeedSwap = false;
#else
#error "Unknown platform endianness; define NeedSwap manually"
#endif

    inline uint32_t ToLittle(uint32_t val)
    {
        if (!NeedSwap) return val;
        return (val >> 24) |
            ((val >> 8) & 0x0000FF00) |
            ((val << 8) & 0x00FF0000) |
            (val << 24);
    }

    inline uint32_t FromLittle(uint32_t val) { return ToLittle(val); }

    inline uint64_t ToLittle(uint64_t val)
    {
        if (!NeedSwap) return val;
        return (val >> 56) |
            ((val >> 40) & 0x000000000000FF00ULL) |
            ((val >> 24) & 0x0000000000FF0000ULL) |
            ((val >> 8) & 0x00000000FF000000ULL) |
            ((val << 8) & 0x000000FF00000000ULL) |
            ((val << 24) & 0x0000FF0000000000ULL) |
            ((val << 40) & 0x00FF000000000000ULL) |
            (val << 56);
    }

    inline uint64_t FromLittle(uint64_t val) { return ToLittle(val); }
}
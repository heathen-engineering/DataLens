/******************************************************************************
 * DataLensSchema.cpp
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2025-11-15
 ******************************************************************************/

#include "DataLensSchema.h"

void Schema::AddStore(const StoreSchema& store) { mStores.push_back(store); }

const StoreSchema* Schema::GetStore(const std::string& name) const
{
    for (const auto& s : mStores)
        if (s.Name == name)
            return &s;
    return nullptr;
}

bool Schema::HasStore(const std::string& name) const
{
    return GetStore(name) != nullptr;
}

// Number of stores
size_t Schema::Count() const 
{
    return mStores.size(); 
}

void ColumnSchema::ConvertCell(const void* src, DataType fromType, void* dst, DataType toType)
{
    // Convert everything to a 64-bit intermediate for integers
    if (fromType == DataType::Bool || fromType == DataType::Int8 || fromType == DataType::UInt8 ||
        fromType == DataType::Int16 || fromType == DataType::UInt16 || fromType == DataType::Int32 ||
        fromType == DataType::UInt32 || fromType == DataType::Int64 || fromType == DataType::UInt64)
    {
        int64_t i64 = 0;
        switch (fromType)
        {
        case DataType::Bool:   i64 = *reinterpret_cast<const bool*>(src); break;
        case DataType::Int8:   i64 = *reinterpret_cast<const int8_t*>(src); break;
        case DataType::UInt8:  i64 = *reinterpret_cast<const uint8_t*>(src); break;
        case DataType::Int16:  i64 = *reinterpret_cast<const int16_t*>(src); break;
        case DataType::UInt16: i64 = *reinterpret_cast<const uint16_t*>(src); break;
        case DataType::Int32:  i64 = *reinterpret_cast<const int32_t*>(src); break;
        case DataType::UInt32: i64 = *reinterpret_cast<const uint32_t*>(src); break;
        case DataType::Int64:  i64 = *reinterpret_cast<const int64_t*>(src); break;
        case DataType::UInt64: i64 = static_cast<int64_t>(*reinterpret_cast<const uint64_t*>(src)); break;
        default: break;
        }

        // Convert to target integer type
        switch (toType)
        {
        case DataType::Bool:   *reinterpret_cast<bool*>(dst) = (i64 != 0); break;
        case DataType::Int8:   *reinterpret_cast<int8_t*>(dst) = static_cast<int8_t>(i64); break;
        case DataType::UInt8:  *reinterpret_cast<uint8_t*>(dst) = static_cast<uint8_t>(i64); break;
        case DataType::Int16:  *reinterpret_cast<int16_t*>(dst) = static_cast<int16_t>(i64); break;
        case DataType::UInt16: *reinterpret_cast<uint16_t*>(dst) = static_cast<uint16_t>(i64); break;
        case DataType::Int32:  *reinterpret_cast<int32_t*>(dst) = static_cast<int32_t>(i64); break;
        case DataType::UInt32: *reinterpret_cast<uint32_t*>(dst) = static_cast<uint32_t>(i64); break;
        case DataType::Int64:  *reinterpret_cast<int64_t*>(dst) = i64; break;
        case DataType::UInt64: *reinterpret_cast<uint64_t*>(dst) = static_cast<uint64_t>(i64); break;
        default: break;
        }

        return;
    }

    // Float/double conversion
    if (fromType == DataType::Float || fromType == DataType::Double)
    {
        double d = (fromType == DataType::Float) ? *reinterpret_cast<const float*>(src) : *reinterpret_cast<const double*>(src);
        switch (toType)
        {
        case DataType::Float:  *reinterpret_cast<float*>(dst) = static_cast<float>(d); break;
        case DataType::Double: *reinterpret_cast<double*>(dst) = d; break;
        case DataType::Bool:   *reinterpret_cast<bool*>(dst) = (d != 0.0); break;
        case DataType::Int8:   *reinterpret_cast<int8_t*>(dst) = static_cast<int8_t>(d); break;
        case DataType::UInt8:  *reinterpret_cast<uint8_t*>(dst) = static_cast<uint8_t>(d); break;
        case DataType::Int16:  *reinterpret_cast<int16_t*>(dst) = static_cast<int16_t>(d); break;
        case DataType::UInt16: *reinterpret_cast<uint16_t*>(dst) = static_cast<uint16_t>(d); break;
        case DataType::Int32:  *reinterpret_cast<int32_t*>(dst) = static_cast<int32_t>(d); break;
        case DataType::UInt32: *reinterpret_cast<uint32_t*>(dst) = static_cast<uint32_t>(d); break;
        case DataType::Int64:  *reinterpret_cast<int64_t*>(dst) = static_cast<int64_t>(d); break;
        case DataType::UInt64: *reinterpret_cast<uint64_t*>(dst) = static_cast<uint64_t>(d); break;
        default: break;
        }
        return;
    }

    // GUID conversion (must be same type)
    if (fromType == DataType::GUID && toType == DataType::GUID)
    {
        std::memcpy(dst, src, 16);
        return;
    }

    // If we reach here, unsupported conversion
    throw std::runtime_error("Unsupported ConvertCell conversion");
}

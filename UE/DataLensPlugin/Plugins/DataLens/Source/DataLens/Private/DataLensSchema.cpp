/******************************************************************************
 * DataLensSchema.cpp
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2025-11-15
 ******************************************************************************/

#include "DataLensSchema.h"

void DataLensSchema::AddStore(const DataStoreSchema& store) { mStores.push_back(store); }

const DataStoreSchema* DataLensSchema::GetStore(const std::string& name) const
{
    for (const auto& s : mStores)
        if (s.Name == name)
            return &s;
    return nullptr;
}

bool DataLensSchema::HasStore(const std::string& name) const
{
    return GetStore(name) != nullptr;
}

// Number of stores
size_t DataLensSchema::Count() const 
{
    return mStores.size(); 
}

void DataLensValueTypeUtils::ConvertData(const void* src, DataLensValueType fromType, void* dst, DataLensValueType toType)
{
    // Convert everything to a 64-bit intermediate for integers
    if (fromType == DataLensValueType::Bool || fromType == DataLensValueType::Int8 || fromType == DataLensValueType::UInt8 ||
        fromType == DataLensValueType::Int16 || fromType == DataLensValueType::UInt16 || fromType == DataLensValueType::Int32 ||
        fromType == DataLensValueType::UInt32 || fromType == DataLensValueType::Int64 || fromType == DataLensValueType::UInt64)
    {
        int64_t i64 = 0;
        switch (fromType)
        {
        case DataLensValueType::Bool:   i64 = *reinterpret_cast<const bool*>(src); break;
        case DataLensValueType::Int8:   i64 = *reinterpret_cast<const int8_t*>(src); break;
        case DataLensValueType::UInt8:  i64 = *reinterpret_cast<const uint8_t*>(src); break;
        case DataLensValueType::Int16:  i64 = *reinterpret_cast<const int16_t*>(src); break;
        case DataLensValueType::UInt16: i64 = *reinterpret_cast<const uint16_t*>(src); break;
        case DataLensValueType::Int32:  i64 = *reinterpret_cast<const int32_t*>(src); break;
        case DataLensValueType::UInt32: i64 = *reinterpret_cast<const uint32_t*>(src); break;
        case DataLensValueType::Int64:  i64 = *reinterpret_cast<const int64_t*>(src); break;
        case DataLensValueType::UInt64: i64 = static_cast<int64_t>(*reinterpret_cast<const uint64_t*>(src)); break;
        default: break;
        }

        // Convert to target integer type
        switch (toType)
        {
        case DataLensValueType::Bool:   *reinterpret_cast<bool*>(dst) = (i64 != 0); break;
        case DataLensValueType::Int8:   *reinterpret_cast<int8_t*>(dst) = static_cast<int8_t>(i64); break;
        case DataLensValueType::UInt8:  *reinterpret_cast<uint8_t*>(dst) = static_cast<uint8_t>(i64); break;
        case DataLensValueType::Int16:  *reinterpret_cast<int16_t*>(dst) = static_cast<int16_t>(i64); break;
        case DataLensValueType::UInt16: *reinterpret_cast<uint16_t*>(dst) = static_cast<uint16_t>(i64); break;
        case DataLensValueType::Int32:  *reinterpret_cast<int32_t*>(dst) = static_cast<int32_t>(i64); break;
        case DataLensValueType::UInt32: *reinterpret_cast<uint32_t*>(dst) = static_cast<uint32_t>(i64); break;
        case DataLensValueType::Int64:  *reinterpret_cast<int64_t*>(dst) = i64; break;
        case DataLensValueType::UInt64: *reinterpret_cast<uint64_t*>(dst) = static_cast<uint64_t>(i64); break;
        default: break;
        }

        return;
    }

    // Float/double conversion
    if (fromType == DataLensValueType::Float || fromType == DataLensValueType::Double)
    {
        double d = (fromType == DataLensValueType::Float) ? *reinterpret_cast<const float*>(src) : *reinterpret_cast<const double*>(src);
        switch (toType)
        {
        case DataLensValueType::Float:  *reinterpret_cast<float*>(dst) = static_cast<float>(d); break;
        case DataLensValueType::Double: *reinterpret_cast<double*>(dst) = d; break;
        case DataLensValueType::Bool:   *reinterpret_cast<bool*>(dst) = (d != 0.0); break;
        case DataLensValueType::Int8:   *reinterpret_cast<int8_t*>(dst) = static_cast<int8_t>(d); break;
        case DataLensValueType::UInt8:  *reinterpret_cast<uint8_t*>(dst) = static_cast<uint8_t>(d); break;
        case DataLensValueType::Int16:  *reinterpret_cast<int16_t*>(dst) = static_cast<int16_t>(d); break;
        case DataLensValueType::UInt16: *reinterpret_cast<uint16_t*>(dst) = static_cast<uint16_t>(d); break;
        case DataLensValueType::Int32:  *reinterpret_cast<int32_t*>(dst) = static_cast<int32_t>(d); break;
        case DataLensValueType::UInt32: *reinterpret_cast<uint32_t*>(dst) = static_cast<uint32_t>(d); break;
        case DataLensValueType::Int64:  *reinterpret_cast<int64_t*>(dst) = static_cast<int64_t>(d); break;
        case DataLensValueType::UInt64: *reinterpret_cast<uint64_t*>(dst) = static_cast<uint64_t>(d); break;
        default: break;
        }
        return;
    }

    // GUID conversion (must be same type)
    if (fromType == DataLensValueType::GUID && toType == DataLensValueType::GUID)
    {
        std::memcpy(dst, src, 16);
        return;
    }

    // If we reach here, unsupported conversion
    throw std::runtime_error("Unsupported ConvertCell conversion");
}

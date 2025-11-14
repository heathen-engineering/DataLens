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

void ColumnSchema::ConvertCell(const void* src, Type fromType, void* dst, Type toType)
{
    // Convert everything to a 64-bit intermediate for integers
    if (fromType == Type::Bool || fromType == Type::Int8 || fromType == Type::UInt8 ||
        fromType == Type::Int16 || fromType == Type::UInt16 || fromType == Type::Int32 ||
        fromType == Type::UInt32 || fromType == Type::Int64 || fromType == Type::UInt64)
    {
        int64_t i64 = 0;
        switch (fromType)
        {
        case Type::Bool:   i64 = *reinterpret_cast<const bool*>(src); break;
        case Type::Int8:   i64 = *reinterpret_cast<const int8_t*>(src); break;
        case Type::UInt8:  i64 = *reinterpret_cast<const uint8_t*>(src); break;
        case Type::Int16:  i64 = *reinterpret_cast<const int16_t*>(src); break;
        case Type::UInt16: i64 = *reinterpret_cast<const uint16_t*>(src); break;
        case Type::Int32:  i64 = *reinterpret_cast<const int32_t*>(src); break;
        case Type::UInt32: i64 = *reinterpret_cast<const uint32_t*>(src); break;
        case Type::Int64:  i64 = *reinterpret_cast<const int64_t*>(src); break;
        case Type::UInt64: i64 = static_cast<int64_t>(*reinterpret_cast<const uint64_t*>(src)); break;
        default: break;
        }

        // Convert to target integer type
        switch (toType)
        {
        case Type::Bool:   *reinterpret_cast<bool*>(dst) = (i64 != 0); break;
        case Type::Int8:   *reinterpret_cast<int8_t*>(dst) = static_cast<int8_t>(i64); break;
        case Type::UInt8:  *reinterpret_cast<uint8_t*>(dst) = static_cast<uint8_t>(i64); break;
        case Type::Int16:  *reinterpret_cast<int16_t*>(dst) = static_cast<int16_t>(i64); break;
        case Type::UInt16: *reinterpret_cast<uint16_t*>(dst) = static_cast<uint16_t>(i64); break;
        case Type::Int32:  *reinterpret_cast<int32_t*>(dst) = static_cast<int32_t>(i64); break;
        case Type::UInt32: *reinterpret_cast<uint32_t*>(dst) = static_cast<uint32_t>(i64); break;
        case Type::Int64:  *reinterpret_cast<int64_t*>(dst) = i64; break;
        case Type::UInt64: *reinterpret_cast<uint64_t*>(dst) = static_cast<uint64_t>(i64); break;
        default: break;
        }

        return;
    }

    // Float/double conversion
    if (fromType == Type::Float || fromType == Type::Double)
    {
        double d = (fromType == Type::Float) ? *reinterpret_cast<const float*>(src) : *reinterpret_cast<const double*>(src);
        switch (toType)
        {
        case Type::Float:  *reinterpret_cast<float*>(dst) = static_cast<float>(d); break;
        case Type::Double: *reinterpret_cast<double*>(dst) = d; break;
        case Type::Bool:   *reinterpret_cast<bool*>(dst) = (d != 0.0); break;
        case Type::Int8:   *reinterpret_cast<int8_t*>(dst) = static_cast<int8_t>(d); break;
        case Type::UInt8:  *reinterpret_cast<uint8_t*>(dst) = static_cast<uint8_t>(d); break;
        case Type::Int16:  *reinterpret_cast<int16_t*>(dst) = static_cast<int16_t>(d); break;
        case Type::UInt16: *reinterpret_cast<uint16_t*>(dst) = static_cast<uint16_t>(d); break;
        case Type::Int32:  *reinterpret_cast<int32_t*>(dst) = static_cast<int32_t>(d); break;
        case Type::UInt32: *reinterpret_cast<uint32_t*>(dst) = static_cast<uint32_t>(d); break;
        case Type::Int64:  *reinterpret_cast<int64_t*>(dst) = static_cast<int64_t>(d); break;
        case Type::UInt64: *reinterpret_cast<uint64_t*>(dst) = static_cast<uint64_t>(d); break;
        default: break;
        }
        return;
    }

    // GUID conversion (must be same type)
    if (fromType == Type::GUID && toType == Type::GUID)
    {
        std::memcpy(dst, src, 16);
        return;
    }

    // If we reach here, unsupported conversion
    throw std::runtime_error("Unsupported ConvertCell conversion");
}

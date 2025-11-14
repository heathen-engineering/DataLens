#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

struct ColumnSchema
{
    std::string Name;
    enum class Type : uint8_t
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
    } DataType;
        
    static size_t GetStrideForType(ColumnSchema::Type type)
    {
        switch (type)
        {
        case ColumnSchema::Type::Bool:   return 1;
        case ColumnSchema::Type::Int8:   return 1;
        case ColumnSchema::Type::UInt8:  return 1;
        case ColumnSchema::Type::Int16:  return 2;
        case ColumnSchema::Type::UInt16: return 2;
        case ColumnSchema::Type::Int32:  return 4;
        case ColumnSchema::Type::UInt32: return 4;
        case ColumnSchema::Type::Int64:  return 8;
        case ColumnSchema::Type::UInt64: return 8;
        case ColumnSchema::Type::Float:  return 4;
        case ColumnSchema::Type::Double: return 8;
        case ColumnSchema::Type::GUID:   return 16;
        }
        return 0; // should never happen
    }
    size_t GetStride() const
    {
        return GetStrideForType(DataType);
    }

    static void ConvertCell(const void* src, Type fromType, void* dst, Type toType);

    bool Optional = false;
    std::vector<uint8_t> DefaultValue; // optional
};

struct StoreSchema
{
    std::string Name;
    std::vector<ColumnSchema> Columns;
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
/******************************************************************************
 * DataLens.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * High-performance, column-oriented in-memory data table with dynamic
 * per-column stride. Supports both raw and safe access to table cells.
 *
 * Author: James McGhee
 * Date:   2025-11-14
 ******************************************************************************/

#include "DataLens.h"
#include <cstring>

 // Helper for writing a string into a buffer with length prefix
void DataLens::WriteString(std::vector<uint8_t>& out, const std::string& str)
{
    uint32_t len = static_cast<uint32_t>(str.size());
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&len), reinterpret_cast<uint8_t*>(&len) + sizeof(len));
    out.insert(out.end(), str.begin(), str.end());
}

// Helper for reading a string from a buffer with length prefix
std::string DataLens::ReadString(const uint8_t* data, size_t& offset, size_t dataSize)
{
    if (offset + sizeof(uint32_t) > dataSize) throw std::runtime_error("Buffer overflow reading string length");
    uint32_t len;
    std::memcpy(&len, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    if (offset + len > dataSize) throw std::runtime_error("Buffer overflow reading string data");
    std::string result(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return result;
}

std::vector<uint8_t> DataLens::Serialize() const
{
    std::vector<uint8_t> out;

    // 1. Write schema: number of stores
    uint32_t storeCount = DataLensEndian::ToLittle(static_cast<uint32_t>(mSchema.Count()));
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&storeCount), reinterpret_cast<uint8_t*>(&storeCount) + sizeof(storeCount));

    // For each store: name, version, columns
    for (size_t i = 0; i < mSchema.Count(); ++i)
    {
        const StoreSchema& store = mSchema[i];
        WriteString(out, store.Name);

        uint32_t versionLE = DataLensEndian::ToLittle(store.Version);
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&versionLE), reinterpret_cast<const uint8_t*>(&versionLE) + sizeof(versionLE));

        uint32_t columnCountLE = DataLensEndian::ToLittle(static_cast<uint32_t>(store.Columns.size()));
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&columnCountLE), reinterpret_cast<uint8_t*>(&columnCountLE) + sizeof(columnCountLE));

        for (const ColumnSchema& col : store.Columns)
        {
            WriteString(out, col.Name);
            out.push_back(static_cast<uint8_t>(col.DataType));

            uint32_t strideLE = DataLensEndian::ToLittle(static_cast<uint32_t>(col.GetStride()));
            out.insert(out.end(), reinterpret_cast<uint8_t*>(&strideLE), reinterpret_cast<uint8_t*>(&strideLE) + sizeof(strideLE));

            uint8_t hasDefault = col.DefaultValue.empty() ? 0 : 1;
            out.push_back(hasDefault);
            if (hasDefault)
            {
                uint32_t defaultSizeLE = DataLensEndian::ToLittle(static_cast<uint32_t>(col.DefaultValue.size()));
                out.insert(out.end(), reinterpret_cast<uint8_t*>(&defaultSizeLE), reinterpret_cast<uint8_t*>(&defaultSizeLE) + sizeof(defaultSizeLE));
                out.insert(out.end(), col.DefaultValue.begin(), col.DefaultValue.end());
            }
        }
    }

    // 2. Write DataStores: iterate mStores
    for (const DataStore& store : mStores)
    {
        std::vector<uint8_t> dump = store.Dump();
        uint32_t dumpSizeLE = DataLensEndian::ToLittle(static_cast<uint32_t>(dump.size()));
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&dumpSizeLE), reinterpret_cast<uint8_t*>(&dumpSizeLE) + sizeof(dumpSizeLE));
        out.insert(out.end(), dump.begin(), dump.end());
    }

    return out;
}

void DataLens::Deserialize(const std::vector<uint8_t>& data)
{
    size_t offset = 0;
    const uint8_t* ptr = data.data();
    size_t dataSize = data.size();

    // Read store count
    if (offset + sizeof(uint32_t) > dataSize) throw std::runtime_error("Invalid payload");
    uint32_t storeCountLE;
    std::memcpy(&storeCountLE, ptr + offset, sizeof(storeCountLE));
    uint32_t storeCount = DataLensEndian::FromLittle(storeCountLE);
    offset += sizeof(storeCountLE);

    Schema loadedSchema;
    for (uint32_t i = 0; i < storeCount; ++i)
    {
        StoreSchema store;
        store.Name = ReadString(ptr, offset, dataSize);

        if (offset + sizeof(uint32_t) > dataSize) throw std::runtime_error("Invalid payload");
        uint32_t versionLE;
        std::memcpy(&versionLE, ptr + offset, sizeof(versionLE));
        store.Version = DataLensEndian::FromLittle(versionLE);
        offset += sizeof(versionLE);

        if (offset + sizeof(uint32_t) > dataSize) throw std::runtime_error("Invalid payload");
        uint32_t columnCountLE;
        std::memcpy(&columnCountLE, ptr + offset, sizeof(columnCountLE));
        uint32_t columnCount = DataLensEndian::FromLittle(columnCountLE);
        offset += sizeof(columnCountLE);

        for (uint32_t c = 0; c < columnCount; ++c)
        {
            ColumnSchema col;
            col.Name = ReadString(ptr, offset, dataSize);

            if (offset + 1 > dataSize) throw std::runtime_error("Invalid payload");
            col.DataType = static_cast<ColumnSchema::Type>(ptr[offset++]);

            uint8_t hasDefault = ptr[offset++];
            if (hasDefault)
            {
                if (offset + sizeof(uint32_t) > dataSize) throw std::runtime_error("Invalid payload");
                uint32_t defaultSizeLE;
                std::memcpy(&defaultSizeLE, ptr + offset, sizeof(defaultSizeLE));
                uint32_t defaultSize = DataLensEndian::FromLittle(defaultSizeLE);
                offset += sizeof(defaultSizeLE);

                if (offset + defaultSize > dataSize) throw std::runtime_error("Invalid payload");
                col.DefaultValue.assign(ptr + offset, ptr + offset + defaultSize);
                offset += defaultSize;
            }

            store.Columns.push_back(col);
        }

        loadedSchema.AddStore(store);
    }

    // Save schema before populating stores
    mSchema = std::move(loadedSchema);

    // Read DataStores
    mStores.clear();
    for (uint32_t i = 0; i < storeCount; ++i)
    {
        if (offset + sizeof(uint32_t) > dataSize) throw std::runtime_error("Invalid payload");
        uint32_t dumpSizeLE;
        std::memcpy(&dumpSizeLE, ptr + offset, sizeof(dumpSizeLE));
        uint32_t dumpSize = DataLensEndian::FromLittle(dumpSizeLE);
        offset += sizeof(dumpSizeLE);

        if (offset + dumpSize > dataSize) throw std::runtime_error("Invalid payload");
        std::vector<uint8_t> dump(ptr + offset, ptr + offset + dumpSize);
        offset += dumpSize;

        // Convert the dump to match in-memory schema (type conversions, defaults, missing columns)
        const StoreSchema& schema = mSchema[i];
        DataStore store(schema.Columns, dump);
        store.ConvertToSchema(schema);
        mStores.push_back(std::move(store));
    }
}


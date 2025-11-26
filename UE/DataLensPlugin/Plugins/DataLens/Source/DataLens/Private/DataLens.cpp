/******************************************************************************
 * DataLens.cpp
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-14 - 2025-11-24
 ******************************************************************************/

#pragma once

#include "DataLens.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <DataLensSchema.h>
#include <DataStore.h>
#include <exception>
#include <iterator>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

inline DataLens::DataLens(const DataLensSchema& schema)
    : mSchema(schema)
{
    // Pre-create a DataStore for each store in the schema
    mStores.reserve(mSchema.Count());

    for (size_t i = 0; i < mSchema.Count(); ++i)
    {
        const DataStoreSchema& store = mSchema[i];

        size_t prealloc = store.DefaultCapacity;

        mStores.emplace_back(store.Columns, prealloc);
    }
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
        const DataStoreSchema& store = mSchema[i];
        WriteString(out, store.Name);

        uint32_t versionLE = DataLensEndian::ToLittle(store.Version);
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(&versionLE), reinterpret_cast<const uint8_t*>(&versionLE) + sizeof(versionLE));

        uint32_t columnCountLE = DataLensEndian::ToLittle(static_cast<uint32_t>(store.Columns.size()));
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&columnCountLE), reinterpret_cast<uint8_t*>(&columnCountLE) + sizeof(columnCountLE));

        for (const DataStoreColumnSchema& col : store.Columns)
        {
            WriteString(out, col.Name);
            out.push_back(static_cast<uint8_t>(col.Type));

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

    DataLensSchema loadedSchema;
    for (uint32_t i = 0; i < storeCount; ++i)
    {
        DataStoreSchema store;
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
            DataStoreColumnSchema col;
            col.Name = ReadString(ptr, offset, dataSize);

            if (offset + 1 > dataSize) throw std::runtime_error("Invalid payload");
            col.Type = static_cast<DataLensValueType>(ptr[offset++]);

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
        const DataStoreSchema& schema = mSchema[i];
        DataStore store(schema.Columns, dump);
        store.ConvertToSchema(schema);
        mStores.push_back(std::move(store));
    }
}

size_t DataLens::FindStore(const std::string& name) const
{
    for (size_t i = 0; i < mSchema.Count(); ++i)
    {
        if (mSchema[i].Name == name)
            return i;
    }

    return SIZE_MAX; // not found
}

bool DataLens::HasView(const std::string& name)
{
    return mViewNameToID.find(name) != mViewNameToID.end();
}

bool DataLens::IsViewValid(size_t id) const
{
    return id < mViews.size();
}

size_t DataLens::FindOrCreateView(const std::string& name)
{
    auto it = mViewNameToID.find(name);
    if (it != mViewNameToID.end())
        return it->second;

    // Not found, create a new view
    DataViewRegistry newView;
    newView.DataLensSchema.Name = name;

    size_t newID = mViews.size();
    mViews.push_back(std::move(newView));
    mViewNameToID[name] = newID;

    return newID;
}

void DataLens::RemoveView(size_t id)
{
    if (!IsViewValid(id))
        return;

    const std::string& name = mViews[id].DataLensSchema.Name;
    mViewNameToID.erase(name);

    // Mark as invalid by clearing the name
    mViews[id].DataLensSchema.Name.clear();
    // Optionally reset other parts of the registry if needed
    mViews[id].Cache = DataViewRegistry::CacheMetadata{};
}

bool DataLens::SetViewSelect(size_t id, const std::string sql)
{
    if (!IsViewValid(id))
        return false;

    auto [query, columns] = GetQuery(sql);
    mViews[id].DataLensSchema.Query = query;

    if (!columns.empty())
    {
        mViews[id].DataLensSchema.Columns = columns;
        return true;
    }
    else
        return false;
}

bool DataLens::SetViewInsert(size_t id, const std::string sql)
{
    if (!IsViewValid(id))
        return false;

    auto insertObject = GetUpdate(sql);
    if (insertObject.Type == DataUpdateType::Insert)
    {
        mViews[id].DataLensSchema.Insert = insertObject;
        return true;
    }
    else
        return false;
}

bool DataLens::SetViewUpdate(size_t id, const std::string sql)
{
    if (!IsViewValid(id))
        return false;

    auto updateObject = GetUpdate(sql);
    if (updateObject.Type == DataUpdateType::Update)
    {
        mViews[id].DataLensSchema.Update = updateObject;
        return true;
    }
    else
        return false;
}

bool DataLens::SetViewDelete(size_t id, const std::string sql)
{
    if (!IsViewValid(id))
        return false;

    auto deleteObject = GetUpdate(sql);
    if (deleteObject.Type == DataUpdateType::Delete)
    {
        mViews[id].DataLensSchema.Delete = deleteObject;
        return true;
    }
    else
        return false;
}

bool DataLens::SetViewFrequency(size_t id, uint8_t frequency)
{
    if (!IsViewValid(id))
        return false;

    mViews[id].Frequency = frequency;
    return true;
}

bool DataLens::SetViewCanInsert(size_t id, bool canInsert)
{
    if (!IsViewValid(id))
        return false;

    // Use the legacy convenience flag on the Insert object
    mViews[id].DataLensSchema.Insert.InsertIfNotExists = canInsert;
    return true;
}

bool DataLens::SetViewCanUpdate(size_t id, bool canUpdate)
{
    if (!IsViewValid(id))
        return false;

    // Use the legacy convenience flag on the Update object
    mViews[id].DataLensSchema.Update.UpdateIfExists = canUpdate;
    return true;
}

std::vector<DataViewColumnSchema> DataLens::GetViewSchema(size_t id) const
{
    if (!IsViewValid(id))
        return {};

    return mViews[id].DataLensSchema.Columns;
}

bool DataLens::GetViewRow(size_t view, size_t row, uint8_t* buffer) const
{
    if (!IsViewValid(view))
        return false;

    const auto& cache = mViews[view].Cache;
    if (row >= cache.RowStartPointers.size() || !buffer)
        return false;

    size_t stride = cache.RowStride;
    std::memcpy(buffer, cache.RowStartPointers[row], stride);
    return true;
}

bool DataLens::SetViewRow(size_t view, size_t row, const uint8_t* data)
{
    if (!IsViewValid(view))
        return false;

    auto& cache = mViews[view].Cache;
    if (row >= cache.RowStartPointers.size() || !data)
        return false;

    size_t stride = cache.RowStride;
    std::memcpy(cache.RowStartPointers[row], data, stride);
    mViews[view].Cache.DirtyRows[row][0] = DataViewRegistry::RowState::Modified;
    return true;
}

size_t DataLens::AddViewRow(size_t view, const uint8_t* data)
{
    if (!IsViewValid(view) || !data)
        return SIZE_MAX;

    auto& v = mViews[view];
    size_t newRowIndex = v.Cache.RowStartPointers.size();
    size_t stride = v.Cache.RowStride;

    // Expand data
    size_t oldSize = v.Cache.Data.size();
    v.Cache.Data.resize(oldSize + stride);
    uint8_t* newRowPtr = v.Cache.Data.data() + oldSize;
    std::memcpy(newRowPtr, data, stride);

    v.Cache.RowStartPointers.push_back(newRowPtr);
    v.Cache.DirtyRows.push_back(std::vector<DataViewRegistry::RowState>(v.DataLensSchema.Columns.size() + 1, DataViewRegistry::RowState::New));

    return newRowIndex;
}

bool DataLens::RemoveViewRow(size_t view, size_t row)
{
    if (!IsViewValid(view))
        return false;

    auto& cache = mViews[view].Cache;
    if (row >= cache.RowStartPointers.size())
        return false;

    cache.DirtyRows[row][0] = DataViewRegistry::RowState::Removed;
    return true;
}

bool DataLens::RefreshView(size_t id)
{
    // Validate view ID
    if (id >= mViews.size())
        return false;

    auto& viewRegistry = mViews[id];
    auto& view = viewRegistry.DataLensSchema;

    // Ensure the view has a query defined
    if (view.Query.DataStoreIndices.empty() && view.Query.SelectColumns.empty())
        return false;

    // Gather candidate rows from all involved stores
    auto candidateRows = GatherCandidateRows(view.Query);

    // Apply predicates to filter rows
    candidateRows = FilterRowsByPredicates(candidateRows, view.Query);

    // Convert candidate row indices to actual row pointers
    std::vector<std::vector<uint8_t*>> rowPtrs;
    for (const auto& rowSet : candidateRows)
    {
        std::vector<uint8_t*> ptrSet;
        for (size_t storeIdx = 0; storeIdx < rowSet.size(); ++storeIdx)
        {
            size_t storeID = view.Query.DataStoreIndices[storeIdx];
            size_t rowIndex = rowSet[storeIdx];
            ptrSet.push_back(GetRowPointer(storeID, rowIndex));
        }
        rowPtrs.push_back(ptrSet);
    }

    // Evaluate calculated expressions for all rows
    std::vector<std::vector<uint8_t>> evaluatedRows;
    EvaluateExpressions(view.Query.Expressions, rowPtrs, evaluatedRows);

    // Apply grouping and aggregates if any
    if (!view.Query.GroupByColumns.empty() || !view.Query.Aggregates.empty())
    {
        // Group rows by the specified columns
        auto groupedRowsMap = GroupRows(evaluatedRows, view.Query.GroupByColumns);

        // Apply aggregates directly on row indices
        ApplyAggregates(view.Query.Aggregates, groupedRowsMap);

        // Flatten grouped row indices back into evaluatedRows for caching
        std::vector<std::vector<uint8_t>> flattenedRows;
        for (auto& kv : groupedRowsMap)
        {
            for (size_t rowIndex : kv.second)
            {
                flattenedRows.push_back(evaluatedRows[rowIndex]);
            }
        }
        evaluatedRows = std::move(flattenedRows);
    }

    // Build the flat row-major cache for this view
    BuildRowCache(view, evaluatedRows, viewRegistry.Cache);

    // Apply sorting and limit/offset
    SortRowCache(viewRegistry.Cache, view.Query.SortColumns);
    ApplyLimitOffsetToCache(viewRegistry.Cache, view.Query);

    return true;
}

std::pair<DataQueryObject, std::vector<DataViewColumnSchema>> DataLens::GetQuery(const std::string& sql) const
{
    DataQueryObject query;
    std::vector<DataViewColumnSchema> columns;

    // 1. Normalize whitespace
    std::string str = sql;
    str.erase(std::remove(str.begin(), str.end(), '\n'), str.end());
    str.erase(std::remove(str.begin(), str.end(), '\t'), str.end());
    while (str.find("  ") != std::string::npos)
        str = std::regex_replace(str, std::regex("  "), " ");
    if (!str.empty() && str.back() == ';')
        str.pop_back();

    // 2. Basic SELECT ... FROM ... WHERE parsing
    std::regex selectRegex(R"(select\s+(.*?)\s+from\s+([^\s]+)(?:\s+where\s+(.*))?)", std::regex::icase);
    std::smatch m;
    if (!std::regex_match(str, m, selectRegex))
        throw std::runtime_error("Invalid query syntax");

    std::string selectPart = m[1].str();
    std::string fromPart = m[2].str();
    std::string wherePart = m.size() > 3 ? m[3].str() : "";

    // 3. Resolve FROM stores
    std::vector<std::string> storeNames;
    std::stringstream ss(fromPart);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
        storeNames.push_back(token);
    }
    for (auto& s : storeNames) {
        size_t storeIdx = FindStore(s);
        if (storeIdx == SIZE_MAX)
            throw std::runtime_error("Store not found: " + s);
        query.DataStoreIndices.push_back(storeIdx);
    }

    // 4. Add synthetic IsValid column
    DataQueryColumn isValidCol{};
    isValidCol.StoreIndex = SIZE_MAX;
    isValidCol.ColumnIndex = SIZE_MAX;
    isValidCol.ResultType = DataLensValueType::Bool;
    isValidCol.IsCalculated = false;
    query.SelectColumns.push_back(isValidCol);

    columns.push_back({ "IsValid", DataLensValueType::Bool });

    // 5. Parse SELECT columns
    if (selectPart == "*" || selectPart == " * ") {
        query.SelectAll = true;
        for (size_t storeIdx : query.DataStoreIndices) {
            const auto& store = mSchema[storeIdx];
            for (const auto& col : store.Columns) {
                columns.push_back({ col.Name, col.Type });
            }
        }
    }
    else {
        std::stringstream colStream(selectPart);
        std::string colText;
        while (std::getline(colStream, colText, ',')) {
            colText.erase(std::remove_if(colText.begin(), colText.end(), ::isspace), colText.end());

            bool isAggregate = false;
            DataQueryAggregateType aggType = DataQueryAggregateType::None;
            std::string inner;

            if (colText.find("SUM(") == 0) { aggType = DataQueryAggregateType::Sum; inner = colText.substr(4, colText.size() - 5); isAggregate = true; }
            else if (colText.find("COUNT(") == 0) { aggType = DataQueryAggregateType::Count; inner = colText.substr(6, colText.size() - 7); isAggregate = true; }
            else inner = colText;

            size_t dotPos = inner.find('.');
            size_t storeIdx = query.DataStoreIndices[0];
            std::string colName = inner;
            if (dotPos != std::string::npos) {
                std::string storeName = inner.substr(0, dotPos);
                colName = inner.substr(dotPos + 1);
                storeIdx = FindStore(storeName);
                if (storeIdx == SIZE_MAX) throw std::runtime_error("Store not found: " + storeName);
            }

            auto it = std::find_if(mSchema[storeIdx].Columns.begin(), mSchema[storeIdx].Columns.end(),
                [&](const DataStoreColumnSchema& c) { return c.Name == colName; });
            if (it == mSchema[storeIdx].Columns.end()) throw std::runtime_error("Column not found: " + colName);

            size_t colIdx = std::distance(mSchema[storeIdx].Columns.begin(), it);
            DataQueryColumn col{};
            col.IsCalculated = false;
            col.StoreIndex = storeIdx;
            col.ColumnIndex = colIdx;
            col.ResultType = it->Type;
            query.SelectColumns.push_back(col);

            columns.push_back({ it->Name, it->Type });

            if (isAggregate) {
                query.Aggregates.push_back({ aggType, query.Expressions.size(), query.SelectColumns.size() - 1 });
            }
        }
    }

    // 6. Parse WHERE predicates (simplified)
    bool hasIsValidFilter = false;
    if (!wherePart.empty()) {
        std::stringstream predStream(wherePart);
        std::string predText;
        while (std::getline(predStream, predText, 'A')) { // crude split on AND
            if (predText.substr(0, 2) == "ND") predText = predText.substr(2);
            predText.erase(std::remove_if(predText.begin(), predText.end(), ::isspace), predText.end());
            if (predText.empty()) continue;

            if (predText.find("IsValid") != std::string::npos) {
                hasIsValidFilter = true;
            }
        }
    }

    // 7. Implicit IsValid = true predicate if not filtered
    if (!hasIsValidFilter) {
        DataQueryPredicate validPred{};
        validPred.StoreIndex = SIZE_MAX;
        validPred.ColumnIndex = SIZE_MAX;
        validPred.ColumnType = DataLensValueType::Bool;
        validPred.Op = DataQueryOperator::Equals;
        validPred.BoolValue = true;
        query.Predicates.push_back(validPred);
    }

    return { query, columns };
}


DataUpdateObject DataLens::GetUpdate(const std::string sql)
{
    DataUpdateObject update;
    update.Limit = SIZE_MAX;
    update.Offset = 0;

    // begin parsing
    std::string statement = Trim(sql);
    if (statement.empty()) return update;

    std::istringstream iss(statement);
    std::string keyword;
    iss >> keyword;
    keyword = ToUpper(keyword);

    if (keyword == "INSERT")
    {
        update.Type = DataUpdateType::Insert;
        update.InsertIfNotExists = true;

        // Expect: INSERT INTO <Table> (col,...) FROM CACHE
        std::string intoToken, tableName;
        iss >> intoToken >> tableName;
        if (ToUpper(intoToken) != "INTO") throw std::runtime_error("Invalid INSERT, expected INTO");

        tableName = Trim(tableName);
        const DataStoreSchema* storeSchemaPtr = mSchema.GetStore(tableName);
        if (!storeSchemaPtr) throw std::runtime_error("INSERT target store not found: " + tableName);

        size_t storeIndex = SIZE_MAX;
        for (size_t i = 0; i < mSchema.Count(); ++i) if (&mSchema[i] == storeSchemaPtr) { storeIndex = i; break; }
        update.TargetStores.push_back(storeIndex);

        // read rest
        std::string rest;
        std::getline(iss, rest);
        rest = Trim(rest);

        // parse column list (optional)
        std::vector<std::string> columns;
        if (!rest.empty() && rest[0] == '(')
        {
            size_t endp = rest.find(')');
            if (endp == std::string::npos) throw std::runtime_error("Malformed column list in INSERT");
            std::string colsText = rest.substr(1, endp - 1);
            columns = SplitCSV(colsText);
            rest = Trim(rest.substr(endp + 1));
        }

        // handle two forms: VALUES (...) or FROM CACHE
        std::string restUpper = ToUpper(rest);
        if (restUpper.find("VALUES") == 0)
        {
            // VALUES (...) [, (...), ...]
            size_t pos = 6; // after VALUES
            std::string valsText = Trim(rest.substr(pos));
            // split on ")"
            size_t idx = 0;
            while (idx < valsText.size()) {
                size_t start = valsText.find('(', idx);
                if (start == std::string::npos) break;
                size_t end = valsText.find(')', start);
                if (end == std::string::npos) throw std::runtime_error("Malformed VALUES list");
                std::string group = valsText.substr(start + 1, end - start - 1);
                std::vector<std::string> vals = SplitCSV(group);
                if (!columns.empty() && columns.size() != vals.size()) throw std::runtime_error("Column/value count mismatch in VALUES");
                // If columns empty, assume target store columns order (excluding IsValid?) — but you must supply columns for unambiguous mapping.
                if (columns.empty()) throw std::runtime_error("INSERT without column list and VALUES not supported");
                // create update columns as constants for this INSERT: we will append one set of constant columns; multi-row INSERT will be represented as multiple DataUpdateObjects in future; for now we support single-row or user can supply multiple statements.
                for (size_t ci = 0; ci < columns.size(); ++ci) {
                    const auto& cols = storeSchemaPtr->Columns;
                    auto it = std::find_if(cols.begin(), cols.end(), [&](const DataStoreColumnSchema& c) { return c.Name == columns[ci]; });
                    if (it == cols.end()) throw std::runtime_error("Column not found in INSERT: " + columns[ci]);
                    DataUpdateColumn duc{};
                    duc.TargetStoreIndex = storeIndex;
                    duc.TargetColumnIndex = it - cols.begin();
                    duc.IsConstant = true;
                    duc.ResultType = it->Type;
                    duc.ConstantValue = LiteralToBytes(vals[ci], it->Type);
                    update.Columns.push_back(duc);
                }
                idx = end + 1;
            }
        }
        else
        {
            // Expect FROM <source>
            // Typical: FROM CACHE
            std::string fromTok;
            std::istringstream rs(rest);
            rs >> fromTok;
            if (ToUpper(fromTok) != "FROM") throw std::runtime_error("INSERT must specify FROM or VALUES");
            std::string sourceName;
            rs >> sourceName;
            if (ToUpper(sourceName) != "CACHE")
            {
                // allow FROM <Store> as a copy-from-store
                const DataStoreSchema* srcPtr = mSchema.GetStore(sourceName);
                if (!srcPtr) throw std::runtime_error("Unsupported INSERT FROM source: " + sourceName);
                // INSERT ... FROM OtherStore : treat as source store. We'll set target mapping to read from source store columns.
                size_t srcIndex = SIZE_MAX;
                for (size_t i = 0; i < mSchema.Count(); ++i) if (&mSchema[i] == srcPtr) { srcIndex = i; break; }
                // If columns list provided we map source columns in same order; else assume identical layout.
                if (columns.empty())
                {
                    // map all columns (skip IsValid column index 0?) — we assume target store column list provided
                    for (size_t ci = 0; ci < srcPtr->Columns.size(); ++ci) {
                        // create mapping from source store column to target store column by name
                        const auto& name = srcPtr->Columns[ci].Name;
                        auto it = std::find_if(storeSchemaPtr->Columns.begin(), storeSchemaPtr->Columns.end(),
                            [&](const DataStoreColumnSchema& c) { return c.Name == name; });
                        if (it == storeSchemaPtr->Columns.end()) continue; // skip if name not present
                        DataUpdateColumn duc{};
                        duc.TargetStoreIndex = storeIndex;
                        duc.TargetColumnIndex = it - storeSchemaPtr->Columns.begin();
                        duc.SourceColumnIndex = ci; // indicates reading from source store column index; RunUpdate must interpret storeIndex != SIZE_MAX vs cache
                        duc.ResultType = it->Type;
                        update.Columns.push_back(duc);
                    }
                }
                else
                {
                    // columns specified: map columns by name to source columns of same name
                    for (auto& cName : columns) {
                        // find target column
                        auto itT = std::find_if(storeSchemaPtr->Columns.begin(), storeSchemaPtr->Columns.end(),
                            [&](const DataStoreColumnSchema& c) { return c.Name == cName; });
                        if (itT == storeSchemaPtr->Columns.end()) throw std::runtime_error("Target column not found: " + cName);
                        // find source column by name
                        auto itS = std::find_if(srcPtr->Columns.begin(), srcPtr->Columns.end(),
                            [&](const DataStoreColumnSchema& c) { return c.Name == cName; });
                        if (itS == srcPtr->Columns.end()) throw std::runtime_error("Source column not found: " + cName);
                        DataUpdateColumn duc{};
                        duc.TargetStoreIndex = storeIndex;
                        duc.TargetColumnIndex = itT - storeSchemaPtr->Columns.begin();
                        duc.SourceColumnIndex = itS - srcPtr->Columns.begin();
                        duc.ResultType = itT->Type;
                        update.Columns.push_back(duc);
                    }
                }
            }
            else
            {
                // FROM CACHE
                // columns must be provided to map cache columns to target store columns
                if (columns.empty()) throw std::runtime_error("INSERT FROM CACHE requires column list");

                for (size_t ci = 0; ci < columns.size(); ++ci) {
                    // find target column in store
                    const auto& cols = storeSchemaPtr->Columns;
                    auto it = std::find_if(cols.begin(), cols.end(), [&](const DataStoreColumnSchema& c) { return c.Name == columns[ci]; });
                    if (it == cols.end()) throw std::runtime_error("Column not found in INSERT: " + columns[ci]);
                    DataUpdateColumn duc{};
                    duc.TargetStoreIndex = storeIndex;
                    duc.TargetColumnIndex = it - cols.begin();
                    // source column is cache column: we must encode the cache column index; but we don't have view columns here.
                    // We will store SourceColumnIndex as SIZE_MAX and set ExprRootIndex = SIZE_MAX to indicate run-time binding needed.
                    // However more usable is to expect the column token to be numeric index or the caller attaches mapping later.
                    // For now, set SourceColumnIndex = SIZE_MAX to indicate "read from cache column named <columns[ci]>" and RunUpdate must match by name.
                    duc.SourceColumnIndex = SIZE_MAX;
                    duc.ResultType = it->Type;
                    update.Columns.push_back(duc);
                }
            }
        }
    }
    else if (keyword == "UPDATE")
    {
        update.Type = DataUpdateType::Update;
        update.UpdateIfExists = true;

        std::string tableName;
        iss >> tableName;
        tableName = Trim(tableName);
        const DataStoreSchema* storeSchemaPtr = mSchema.GetStore(tableName);
        if (!storeSchemaPtr) throw std::runtime_error("UPDATE target store not found: " + tableName);

        size_t storeIndex = SIZE_MAX;
        for (size_t i = 0; i < mSchema.Count(); ++i) if (&mSchema[i] == storeSchemaPtr) { storeIndex = i; break; }
        update.TargetStores.push_back(storeIndex);

        // read remaining text
        std::string rest;
        std::getline(iss, rest);
        rest = Trim(rest);

        // locate SET and WHERE
        std::string restUpper = ToUpper(rest);
        size_t posSet = restUpper.find("SET");
        if (posSet == std::string::npos) throw std::runtime_error("UPDATE missing SET clause");
        std::string setPart = Trim(rest.substr(posSet + 3));

        std::string wherePart;
        size_t posWhere = ToUpper(setPart).find("WHERE");
        if (posWhere != std::string::npos) {
            wherePart = Trim(setPart.substr(posWhere + 5));
            setPart = Trim(setPart.substr(0, posWhere));
        }

        // parse set assignments
        std::vector<std::string> assigns = SplitCSV(setPart);
        for (auto& a : assigns)
        {
            size_t eq = a.find('=');
            if (eq == std::string::npos) throw std::runtime_error("Malformed SET assignment: " + a);

            std::string left = Trim(a.substr(0, eq));
            std::string right = Trim(a.substr(eq + 1));

            // locate target column
            auto it = std::find_if(storeSchemaPtr->Columns.begin(), storeSchemaPtr->Columns.end(),
                [&](const DataStoreColumnSchema& c) { return c.Name == left; });
            if (it == storeSchemaPtr->Columns.end()) throw std::runtime_error("Column not found in UPDATE: " + left);

            DataUpdateColumn duc{};
            duc.TargetStoreIndex = storeIndex;
            duc.TargetColumnIndex = it - storeSchemaPtr->Columns.begin();
            duc.ResultType = it->Type;

            // handle right-hand expression
            size_t opPos = std::string::npos;
            std::string opTok;
            for (char c : {'+', '-', '*', '/', '%'}) {
                size_t p = right.find(c);
                if (p != std::string::npos) { opPos = p; opTok = std::string(1, c); break; }
            }

            if (opPos == std::string::npos) {
                // single operand (literal or column)
                duc.ExprRootIndex = ParseOperandToExprIndex(right, update);
            }
            else {
                // binary expression
                std::string leftOp = Trim(right.substr(0, opPos));
                std::string rightOp = Trim(right.substr(opPos + 1));
                size_t leftIdx = ParseOperandToExprIndex(leftOp, update);
                size_t rightIdx = ParseOperandToExprIndex(rightOp, update);

                DataUpdateExpr opExpr{};
                opExpr.Type = DataQueryExprType::BinaryOp;
                opExpr.BinaryOperator = OpStringToOperator(opTok);
                opExpr.LeftOperandIndex = leftIdx;
                opExpr.RightOperandIndex = rightIdx;

                // conservative result type
                DataLensValueType lt = update.Expressions[leftIdx].ResultType;
                DataLensValueType rt = update.Expressions[rightIdx].ResultType;
                opExpr.ResultType = (lt == DataLensValueType::Double || rt == DataLensValueType::Double)
                    ? DataLensValueType::Double
                    : DataLensValueType::Int64;

                update.Expressions.push_back(opExpr);
                duc.ExprRootIndex = update.Expressions.size() - 1;
            }

            update.Columns.push_back(duc);
        }

        // parse WHERE clause
        if (!wherePart.empty()) {
            DataQueryPredicate p = ParseWhereToPredicate(wherePart);
            update.Predicates.push_back(p);
        }
    }
    else if (keyword == "DELETE")
    {
        update.Type = DataUpdateType::Delete;
        update.DeleteIfRemoved = true;

        std::string fromTok, tableName;
        iss >> fromTok >> tableName;
        if (ToUpper(fromTok) != "FROM") throw std::runtime_error("Invalid DELETE syntax, expected FROM");

        tableName = Trim(tableName);
        const DataStoreSchema* storeSchemaPtr = mSchema.GetStore(tableName);
        if (!storeSchemaPtr) throw std::runtime_error("DELETE target store not found: " + tableName);

        size_t storeIndex = SIZE_MAX;
        for (size_t i = 0; i < mSchema.Count(); ++i) if (&mSchema[i] == storeSchemaPtr) { storeIndex = i; break; }
        update.TargetStores.push_back(storeIndex);

        std::string rest;
        std::getline(iss, rest);
        rest = Trim(rest);
        size_t posWhere = ToUpper(rest).find("WHERE");
        if (posWhere != std::string::npos) {
            std::string whereText = Trim(rest.substr(posWhere + 5));
            // parse single predicate
            DataQueryPredicate pred = ParseWhereToPredicate(whereText);
            update.Predicates.push_back(pred);
        }
    }
    else 
    {
        throw std::runtime_error("Unsupported SQL statement: " + sql);
    }

    return update;
}

QueryResultCache DataLens::RunQuery(const DataQueryObject& query)
{
    QueryResultCache cacheResult;
    const size_t numStores = query.DataStoreIndices.size();
    if (numStores == 0) return cacheResult;

    std::vector<std::vector<size_t>> matchingRowCombos;

    // Step 0: Check for explicit IsValid filter
    bool hasExplicitIsValidFilter = false;
    for (const auto& pred : query.Predicates)
    {
        if (pred.StoreIndex == SIZE_MAX && pred.ColumnIndex == 0)
        {
            hasExplicitIsValidFilter = true;
            break;
        }
    }

    size_t firstStoreIdx = query.DataStoreIndices[0];
    size_t rowCount = mStores[firstStoreIdx].GetRowCount();

    // Step 1: Generate candidate row combinations
    for (size_t row = 0; row < rowCount; ++row)
    {
        std::vector<size_t> rowCombo(numStores, SIZE_MAX);
        rowCombo[0] = row;

        // 1a: Evaluate joins
        bool passesJoins = true;
        for (const auto& join : query.Joins)
        {
            size_t leftRow = rowCombo[join.LeftStoreIndex];
            bool matchFound = false;
            for (size_t rightRow = 0; rightRow < mStores[join.RightStoreIndex].GetRowCount(); ++rightRow)
            {
                if (mStores[join.LeftStoreIndex].CompareCells(
                    leftRow, join.LeftColumnIndex,
                    mStores[join.RightStoreIndex], rightRow, join.RightColumnIndex))
                {
                    rowCombo[join.RightStoreIndex] = rightRow;
                    matchFound = true;
                    break;
                }
            }
            if (!matchFound) { passesJoins = false; break; }
        }
        if (!passesJoins) continue;

        // 1b: Evaluate predicates
        bool passesPredicates = true;
        for (const auto& pred : query.Predicates)
        {
            size_t storeIdx = pred.StoreIndex;
            size_t rowIdx = rowCombo[storeIdx];
            const DataStore& store = mStores[storeIdx];

            if (!store.MatchesPredicate(rowIdx, pred))
            {
                passesPredicates = false;
                break;
            }
        }

        // 1c: Implicit IsValid filter
        if (!hasExplicitIsValidFilter)
        {
            if (!mStores[firstStoreIdx].GetRaw<bool>(row, 0)) // column 0 = IsValid
                passesPredicates = false;
        }

        if (passesPredicates)
            matchingRowCombos.push_back(rowCombo);
    }

    // Step 2: Allocate flat result buffer
    size_t rowStride = GetRowStride(query);
    cacheResult.RowStride = rowStride;
    cacheResult.Data.resize(matchingRowCombos.size() * rowStride);

    // Step 2a: Compute ColumnOffsets
    cacheResult.ColumnOffsets.clear();
    size_t offset = 0;
    cacheResult.ColumnOffsets.push_back(offset); // IsValid first
    offset += sizeof(bool);

    if (query.SelectAll)
    {
        for (size_t storeIdx : query.DataStoreIndices)
        {
            const auto& cols = mSchema[storeIdx].Columns;
            for (size_t colIdx = 1; colIdx < cols.size(); ++colIdx)
            {
                cacheResult.ColumnOffsets.push_back(offset);
                offset += cols[colIdx].GetStride();
            }
        }
    }
    else
    {
        for (const auto& selCol : query.SelectColumns)
        {
            if (selCol.StoreIndex == SIZE_MAX && selCol.ColumnIndex == 0) continue; // IsValid
            cacheResult.ColumnOffsets.push_back(offset);
            offset += mSchema[selCol.StoreIndex].Columns[selCol.ColumnIndex].GetStride();
        }
    }

    // Step 2b: Fill RowStartPointers and write data
    cacheResult.RowStartPointers.resize(matchingRowCombos.size());
    for (size_t i = 0; i < matchingRowCombos.size(); ++i)
    {
        offset = i * rowStride;
        cacheResult.RowStartPointers[i] = &cacheResult.Data[offset];

        // Write IsValid
        const size_t firstRow = matchingRowCombos[i][0];
        bool isValid = mStores[firstStoreIdx].GetRaw<bool>(firstRow, 0);
        std::memcpy(&cacheResult.Data[offset], &isValid, sizeof(bool));
        offset += sizeof(bool);

        // Write remaining columns
        if (query.SelectAll)
        {
            for (size_t storeIdx : query.DataStoreIndices)
            {
                const DataStore& store = mStores[storeIdx];
                size_t rowIdx = matchingRowCombos[i][storeIdx];
                for (size_t colIdx = 1; colIdx < mSchema[storeIdx].Columns.size(); ++colIdx)
                {
                    store.CopyCellToFlatRow(rowIdx, colIdx, &cacheResult.Data[offset]);
                    offset += mSchema[storeIdx].Columns[colIdx].GetStride();
                }
            }
        }
        else
        {
            for (const auto& selCol : query.SelectColumns)
            {
                if (selCol.StoreIndex == SIZE_MAX && selCol.ColumnIndex == 0) continue; // IsValid already
                const DataStore& store = mStores[selCol.StoreIndex];
                size_t rowIdx = matchingRowCombos[i][selCol.StoreIndex];
                store.CopyCellToFlatRow(rowIdx, selCol.ColumnIndex, &cacheResult.Data[offset]);
                offset += mSchema[selCol.StoreIndex].Columns[selCol.ColumnIndex].GetStride();
            }
        }
    }

    // Step 3: Sort
    ApplySort(cacheResult.Data, query);

    // Step 4: Limit/Offset
    ApplyLimitOffset(cacheResult.Data, query);

    return cacheResult;
}

std::vector<DataCommandValue> DataLens::RunUpdate(const DataViewRegistry& viewRegistry)
{
    return std::vector<DataCommandValue>();
}

void DataLens::ApplySort(std::vector<uint8_t>& results, const DataQueryObject& query)
{
    if (query.SortColumns.empty())
        return;

    const size_t rowStride = GetRowStride(query);
    const size_t rowCount = results.size() / rowStride;

    auto rowPtr = [&](size_t rowIndex) -> uint8_t* {
        return results.data() + rowIndex * rowStride;
        };

    // Prepare a vector of row indices for sorting
    std::vector<size_t> indices(rowCount);
    for (size_t i = 0; i < rowCount; ++i) indices[i] = i;

    auto compareCells = [&](size_t rowAIdx, size_t rowBIdx) -> bool {
        uint8_t* rowA = rowPtr(rowAIdx);
        uint8_t* rowB = rowPtr(rowBIdx);

        size_t offsetA = 0;
        size_t offsetB = 0;

        for (const auto& sortCol : query.SortColumns)
        {
            const DataStoreColumnSchema& colSchema = mSchema[sortCol.StoreIndex].Columns[sortCol.ColumnIndex];
            size_t stride = colSchema.GetStride();
            const void* valA = rowA + offsetA;
            const void* valB = rowB + offsetB;

            int cmp = 0;
            switch (colSchema.Type)
            {
            case DataLensValueType::Bool:
            case DataLensValueType::UInt8:  cmp = *(uint8_t*)valA - *(uint8_t*)valB; break;
            case DataLensValueType::Int8:   cmp = *(int8_t*)valA - *(int8_t*)valB; break;
            case DataLensValueType::UInt16: cmp = *(uint16_t*)valA - *(uint16_t*)valB; break;
            case DataLensValueType::Int16:  cmp = *(int16_t*)valA - *(int16_t*)valB; break;
            case DataLensValueType::UInt32: cmp = *(uint32_t*)valA > *(uint32_t*)valB ? 1 : (*(uint32_t*)valA < *(uint32_t*)valB ? -1 : 0); break;
            case DataLensValueType::Int32:  cmp = *(int32_t*)valA > *(int32_t*)valB ? 1 : (*(int32_t*)valA < *(int32_t*)valB ? -1 : 0); break;
            case DataLensValueType::UInt64: cmp = *(uint64_t*)valA > *(uint64_t*)valB ? 1 : (*(uint64_t*)valA < *(uint64_t*)valB ? -1 : 0); break;
            case DataLensValueType::Int64:  cmp = *(int64_t*)valA > *(int64_t*)valB ? 1 : (*(int64_t*)valA < *(int64_t*)valB ? -1 : 0); break;
            case DataLensValueType::Float:  cmp = *(float*)valA > *(float*)valB ? 1 : (*(float*)valA < *(float*)valB ? -1 : 0); break;
            case DataLensValueType::Double: cmp = *(double*)valA > *(double*)valB ? 1 : (*(double*)valA < *(double*)valB ? -1 : 0); break;
            case DataLensValueType::GUID:   cmp = std::memcmp(valA, valB, 16); break;
            default: throw std::runtime_error("Unsupported column type in sort");
            }

            if (cmp != 0)
                return sortCol.Descending ? cmp > 0 : cmp < 0;

            offsetA += stride;
            offsetB += stride;
        }

        return false; // equal across all sort columns
        };

    std::sort(indices.begin(), indices.end(), compareCells);

    // Rebuild results in sorted order
    std::vector<uint8_t> sortedResults;
    sortedResults.reserve(results.size());
    for (size_t idx : indices)
        sortedResults.insert(sortedResults.end(), rowPtr(idx), rowPtr(idx) + rowStride);

    results.swap(sortedResults);
}

void DataLens::ApplyLimitOffset(std::vector<uint8_t>& results, const DataQueryObject& query)
{
    size_t rowStride = GetRowStride(query);
    size_t totalRows = results.size() / rowStride;

    size_t start = std::min(query.Offset, totalRows);
    size_t end = std::min(start + query.Limit, totalRows);

    if (start == 0 && end == totalRows)
        return; // nothing to do

    std::vector<uint8_t> trimmed;
    trimmed.reserve((end - start) * rowStride);

    trimmed.insert(trimmed.end(), results.begin() + start * rowStride, results.begin() + end * rowStride);
    results.swap(trimmed);
}

size_t DataLens::GetRowStride(const DataQueryObject& query)
{
    size_t stride = 0;

    if (query.SelectAll)
    {
        for (size_t storeIdx : query.DataStoreIndices)
        {
            const auto& storeSchema = mSchema[storeIdx];
            for (const auto& col : storeSchema.Columns)
                stride += col.GetStride();
        }
    }
    else
    {
        for (const auto& selCol : query.SelectColumns)
        {
            const auto& storeSchema = mSchema[selCol.StoreIndex];
            const auto& col = storeSchema.Columns[selCol.ColumnIndex];
            stride += col.GetStride();
        }
    }

    return stride;
}

void DataLens::WriteString(std::vector<uint8_t>& out, const std::string& str)
{
    uint32_t len = static_cast<uint32_t>(str.size());
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&len), reinterpret_cast<uint8_t*>(&len) + sizeof(len));
    out.insert(out.end(), str.begin(), str.end());
}

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

std::string DataLens::ToUpper(const std::string& s) const
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::toupper);
    return out;
};

std::string DataLens::Trim(const std::string& s) const
{
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
};

std::vector<std::string> DataLens::SplitCSV(const std::string& s) const
{
    std::vector<std::string> out;
    std::string cur;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == ',') {
            out.push_back(Trim(cur));
            cur.clear();
            ++i;
            continue;
        }
        cur.push_back(s[i++]);
    }
    if (!cur.empty()) out.push_back(Trim(cur));
    return out;
};

std::vector<uint8_t> DataLens::LiteralToBytes(const std::string& lit, DataLensValueType type) const
{
    size_t stride = DataLensValueTypeUtils::GetStride(type);
    std::vector<uint8_t> out(stride, 0);

    try {
        // Parse literal as its "natural" type
        union {
            int64_t i;
            uint64_t u;
            double d;
            float f;
            bool b;
        } temp{};

        // Deduce type to parse into (prefer largest integer or floating point)
        if (lit == "TRUE" || lit == "true" || lit == "1" || lit == "FALSE" || lit == "false" || lit == "0") {
            temp.b = (ToUpper(lit) == "TRUE" || lit == "1");
        }
        else if (lit.find('.') != std::string::npos) {
            temp.d = std::stod(lit);
        }
        else {
            temp.i = std::stoll(lit);
        }

        // Convert parsed value into target type using ConvertData
        DataLensValueTypeUtils::ConvertData(
            &temp,
            (lit.find('.') != std::string::npos) ? DataLensValueType::Double : ((lit == "TRUE" || lit == "FALSE" || lit == "1" || lit == "0") ? DataLensValueType::Bool : DataLensValueType::Int64),
            out.data(),
            type
        );
    }
    catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Literal conversion failed: ") + ex.what());
    }

    return out;
}

size_t DataLens::ParseOperandToExprIndex(const std::string& token, DataUpdateObject& update)
{
    std::string t = Trim(token);

    // If it contains a dot => store.column
    size_t dot = t.find('.');
    if (dot != std::string::npos) {
        std::string storeName = Trim(t.substr(0, dot));
        std::string colName = Trim(t.substr(dot + 1));
        size_t storeIndex = SIZE_MAX;

        if (ToUpper(storeName) == "CACHE") {
            throw std::runtime_error("CACHE column references in expressions must be resolved when binding to a view");
        }
        else {
            const DataStoreSchema* sPtr = mSchema.GetStore(storeName);
            if (!sPtr) throw std::runtime_error("Unknown store in expression: " + storeName);

            for (size_t i = 0; i < mSchema.Count(); ++i) {
                if (&mSchema[i] == sPtr) { storeIndex = i; break; }
            }
            if (storeIndex == SIZE_MAX) throw std::runtime_error("Internal error resolving store index");
        }

        size_t colIndex = SIZE_MAX;
        const auto& cols = mSchema[storeIndex].Columns;
        for (size_t ci = 0; ci < cols.size(); ++ci) {
            if (cols[ci].Name == colName) { colIndex = ci; break; }
        }
        if (colIndex == SIZE_MAX) throw std::runtime_error("Column not found in expression: " + colName);

        DataUpdateExpr expr{};
        expr.Type = DataQueryExprType::ColumnRef;
        expr.StoreIndex = storeIndex;
        expr.ColumnIndex = colIndex;
        expr.ResultType = cols[colIndex].Type;
        update.Expressions.push_back(expr);
        return update.Expressions.size() - 1;
    }

    // Otherwise treat as literal
    DataUpdateExpr expr{};
    expr.Type = DataQueryExprType::Constant;

    // Use LiteralToBytes to parse literal and assign to union
    // Default to Int64 if type deduction fails
    try {
        DataLensValueType deducedType = DataLensValueType::Int64;
        if (t == "TRUE" || t == "true" || t == "1" || t == "FALSE" || t == "false" || t == "0")
            deducedType = DataLensValueType::Bool;
        else if (t.find('.') != std::string::npos)
            deducedType = DataLensValueType::Double;

        expr.ResultType = deducedType;
        std::vector<uint8_t> bytes = LiteralToBytes(t, deducedType);

        switch (deducedType) {
        case DataLensValueType::Bool:   std::memcpy(&expr.BoolValue, bytes.data(), sizeof(bool)); break;
        case DataLensValueType::Int64:  std::memcpy(&expr.IntValue, bytes.data(), sizeof(int64_t)); break;
        case DataLensValueType::Double: std::memcpy(&expr.DoubleValue, bytes.data(), sizeof(double)); break;
        default: throw std::runtime_error("Unsupported literal type in expression");
        }
    }
    catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Failed parsing literal in expression: ") + ex.what());
    }

    update.Expressions.push_back(expr);
    return update.Expressions.size() - 1;
}

DataQueryOperator DataLens::OpStringToOperator(const std::string& op) const
{
    if (op == "+") return DataQueryOperator::Add;
    if (op == "-") return DataQueryOperator::Subtract;
    if (op == "*") return DataQueryOperator::Multiply;
    if (op == "/") return DataQueryOperator::Divide;
    if (op == "%") return DataQueryOperator::Modulo;
    throw std::runtime_error("Unsupported arithmetic operator: " + op);
}

DataQueryPredicate DataLens::ParseWhereToPredicate(const std::string& whereText) const
{
    std::string s = Trim(whereText);

    // find operator token
    static const std::vector<std::string> ops = { ">=", "<=", "!=", "=", ">", "<" };
    size_t opPos = std::string::npos;
    std::string foundOp;
    for (const auto& o : ops) {
        size_t p = s.find(o);
        if (p != std::string::npos) { opPos = p; foundOp = o; break; }
    }
    if (opPos == std::string::npos)
        throw std::runtime_error("Unsupported WHERE: " + whereText);

    std::string left = Trim(s.substr(0, opPos));
    std::string right = Trim(s.substr(opPos + foundOp.size()));

    // Resolve left to store/column
    size_t dot = left.find('.');
    size_t storeIndex = SIZE_MAX;
    std::string colName = left;
    if (dot != std::string::npos)
    {
        std::string storeName = Trim(left.substr(0, dot));
        colName = Trim(left.substr(dot + 1));
        if (ToUpper(storeName) != "CACHE") {
            const DataStoreSchema* st = mSchema.GetStore(storeName);
            if (!st) throw std::runtime_error("Unknown store in WHERE: " + storeName);
            for (size_t i = 0; i < mSchema.Count(); ++i)
                if (&mSchema[i] == st) { storeIndex = i; break; }
        }
        // else storeIndex stays SIZE_MAX for CACHE, will bind at runtime
    }
    else
    {
        throw std::runtime_error("WHERE column must be qualified with store or CACHE: " + left);
    }

    DataQueryPredicate pred{};
    pred.StoreIndex = storeIndex;

    if (storeIndex != SIZE_MAX)
    {
        const auto& cols = mSchema[storeIndex].Columns;
        size_t colIdx = SIZE_MAX;
        for (size_t ci = 0; ci < cols.size(); ++ci) if (cols[ci].Name == colName) { colIdx = ci; break; }
        if (colIdx == SIZE_MAX) throw std::runtime_error("Column not found in WHERE: " + colName);
        pred.ColumnIndex = colIdx;
        pred.ColumnType = cols[colIdx].Type;
    }
    else
    {
        // cache column: placeholder, will bind when view is attached
        pred.ColumnIndex = SIZE_MAX;
        pred.ColumnType = DataLensValueType::Int64;
    }

    // operator map
    if (foundOp == "=")  pred.Op = DataQueryOperator::Equals;
    else if (foundOp == "!=") pred.Op = DataQueryOperator::NotEquals;
    else if (foundOp == "<")  pred.Op = DataQueryOperator::Less;
    else if (foundOp == "<=") pred.Op = DataQueryOperator::LessOrEqual;
    else if (foundOp == ">")  pred.Op = DataQueryOperator::Greater;
    else if (foundOp == ">=") pred.Op = DataQueryOperator::GreaterOrEqual;
    else throw std::runtime_error("Unsupported operator in WHERE: " + foundOp);

    // Use LiteralToBytes to parse right-hand side literal
    try {
        std::vector<uint8_t> bytes = LiteralToBytes(right, pred.ColumnType);

        switch (pred.ColumnType) {
        case DataLensValueType::Bool:   std::memcpy(&pred.BoolValue, bytes.data(), sizeof(bool)); break;
        case DataLensValueType::Int8:   std::memcpy(&pred.IntValue, bytes.data(), sizeof(int8_t)); break;
        case DataLensValueType::UInt8:  std::memcpy(&pred.UIntValue, bytes.data(), sizeof(uint8_t)); break;
        case DataLensValueType::Int16:  std::memcpy(&pred.IntValue, bytes.data(), sizeof(int16_t)); break;
        case DataLensValueType::UInt16: std::memcpy(&pred.UIntValue, bytes.data(), sizeof(uint16_t)); break;
        case DataLensValueType::Int32:  std::memcpy(&pred.IntValue, bytes.data(), sizeof(int32_t)); break;
        case DataLensValueType::UInt32: std::memcpy(&pred.UIntValue, bytes.data(), sizeof(uint32_t)); break;
        case DataLensValueType::Int64:  std::memcpy(&pred.IntValue, bytes.data(), sizeof(int64_t)); break;
        case DataLensValueType::UInt64: std::memcpy(&pred.UIntValue, bytes.data(), sizeof(uint64_t)); break;
        case DataLensValueType::Float:  std::memcpy(&pred.FloatValue, bytes.data(), sizeof(float)); break;
        case DataLensValueType::Double: std::memcpy(&pred.DoubleValue, bytes.data(), sizeof(double)); break;
        default:
            throw std::runtime_error("Unsupported column type for WHERE literal");
        }
    }
    catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Failed parsing literal in WHERE: ") + ex.what());
    }

    return pred;
}

std::vector<std::vector<size_t>> DataLens::GatherCandidateRows(const DataQueryObject& query) const
{
    std::vector<std::vector<size_t>> result;
    result.reserve(query.DataStoreIndices.size());

    for (size_t storeIndex : query.DataStoreIndices)
    {
        std::vector<size_t> storeRows;
        if (storeIndex >= mStores.size())
        {
            // Invalid store index, push empty vector
            result.push_back(storeRows);
            continue;
        }

        size_t rowCount = mStores[storeIndex].GetRowCount();
        storeRows.reserve(rowCount);

        for (size_t i = 0; i < rowCount; ++i)
        {
            storeRows.push_back(i);
        }

        result.push_back(std::move(storeRows));
    }

    return result;
}

std::vector<std::vector<size_t>> DataLens::PerformJoins(const std::vector<size_t>& storeIndices, const std::vector<DataQueryJoin>& joins) const
{
    // Start with a Cartesian product of all rows per store
    std::vector<std::vector<size_t>> result;
    if (storeIndices.empty()) return result;

    // Initialize result with single-store row indices
    for (size_t storeIndex : storeIndices)
    {
        std::vector<size_t> rows;
        if (storeIndex >= mStores.size()) continue;
        size_t rowCount = mStores[storeIndex].GetRowCount();
        for (size_t i = 0; i < rowCount; ++i) rows.push_back(i);

        if (result.empty())
        {
            for (size_t row : rows) result.push_back({ row });
        }
        else
        {
            // Build cross product with existing combinations
            std::vector<std::vector<size_t>> temp;
            for (auto& existing : result)
            {
                for (size_t row : rows)
                {
                    std::vector<size_t> combination = existing;
                    combination.push_back(row);
                    temp.push_back(std::move(combination));
                }
            }
            result = std::move(temp);
        }
    }

    // Apply equality-based joins
    if (!joins.empty())
    {
        std::vector<std::vector<size_t>> filtered;
        for (auto& combination : result)
        {
            bool valid = true;
            for (auto& join : joins)
            {
                uint8_t* leftPtr = GetRowPointer(join.LeftStoreIndex, combination[join.LeftStoreIndex]);
                uint8_t* rightPtr = GetRowPointer(join.RightStoreIndex, combination[join.RightStoreIndex]);
                // Compare as raw memory based on column stride
                size_t leftStride = mStores[join.LeftStoreIndex].GetColumnStride(join.LeftColumnIndex);
                size_t rightStride = mStores[join.RightStoreIndex].GetColumnStride(join.RightColumnIndex);
                if (leftStride != rightStride || memcmp(leftPtr, rightPtr, leftStride) != 0)
                {
                    valid = false;
                    break;
                }
            }
            if (valid) filtered.push_back(std::move(combination));
        }
        result = std::move(filtered);
    }

    return result;
}

bool DataLens::EvaluatePredicate(const DataQueryPredicate& pred, const std::vector<uint8_t*>& rowPtrs, const std::vector<DataStore>& stores) const
{
    if (pred.StoreIndex >= rowPtrs.size() || pred.ColumnIndex >= mSchema[pred.StoreIndex].Columns.size())
        return false;

    const uint8_t* row = rowPtrs[pred.StoreIndex];

    // Compute byte offset of the column in the row
    size_t offset = 0;
    for (size_t i = 0; i < pred.ColumnIndex; ++i)
        offset += mSchema[pred.StoreIndex].Columns[i].GetStride();

    const uint8_t* cell = row + offset;
    DataLensValueType colType = mSchema[pred.StoreIndex].Columns[pred.ColumnIndex].Type;

    // Allocate a temporary buffer for converted value
    uint8_t converted[16] = {}; // large enough for GUID
    DataLensValueTypeUtils::ConvertData(cell, colType, converted, pred.ColumnType);

    switch (pred.ColumnType)
    {
    case DataLensValueType::Bool:
    {
        bool val;
        std::memcpy(&val, converted, sizeof(bool));
        switch (pred.Op)
        {
        case DataQueryOperator::Equals: return val == pred.BoolValue;
        case DataQueryOperator::NotEquals: return val != pred.BoolValue;
        case DataQueryOperator::Not: return !val;
        default: return false;
        }
    }

    case DataLensValueType::Int8:
    case DataLensValueType::Int16:
    case DataLensValueType::Int32:
    case DataLensValueType::Int64:
    {
        int64_t val = 0;
        std::memcpy(&val, converted, DataLensValueTypeUtils::GetStride(pred.ColumnType));
        switch (pred.Op)
        {
        case DataQueryOperator::Equals: return val == pred.IntValue;
        case DataQueryOperator::NotEquals: return val != pred.IntValue;
        case DataQueryOperator::Less: return val < pred.IntValue;
        case DataQueryOperator::LessOrEqual: return val <= pred.IntValue;
        case DataQueryOperator::Greater: return val > pred.IntValue;
        case DataQueryOperator::GreaterOrEqual: return val >= pred.IntValue;
        case DataQueryOperator::Range: return val >= pred.IntValue && val <= pred.IntValueHigh;
        default: return false;
        }
    }

    case DataLensValueType::UInt8:
    case DataLensValueType::UInt16:
    case DataLensValueType::UInt32:
    case DataLensValueType::UInt64:
    {
        uint64_t val = 0;
        std::memcpy(&val, converted, DataLensValueTypeUtils::GetStride(pred.ColumnType));
        switch (pred.Op)
        {
        case DataQueryOperator::Equals: return val == pred.UIntValue;
        case DataQueryOperator::NotEquals: return val != pred.UIntValue;
        case DataQueryOperator::Less: return val < pred.UIntValue;
        case DataQueryOperator::LessOrEqual: return val <= pred.UIntValue;
        case DataQueryOperator::Greater: return val > pred.UIntValue;
        case DataQueryOperator::GreaterOrEqual: return val >= pred.UIntValue;
        case DataQueryOperator::BitmaskHas: return (val & pred.UIntValue) == pred.UIntValue;
        case DataQueryOperator::BitmaskNot: return (val & pred.UIntValue) == 0;
        case DataQueryOperator::Range: return val >= pred.UIntValue && val <= pred.UIntValueHigh;
        default: return false;
        }
    }

    case DataLensValueType::Float:
    {
        float val;
        std::memcpy(&val, converted, sizeof(float));
        switch (pred.Op)
        {
        case DataQueryOperator::Equals: return val == pred.FloatValue;
        case DataQueryOperator::NotEquals: return val != pred.FloatValue;
        case DataQueryOperator::Less: return val < pred.FloatValue;
        case DataQueryOperator::LessOrEqual: return val <= pred.FloatValue;
        case DataQueryOperator::Greater: return val > pred.FloatValue;
        case DataQueryOperator::GreaterOrEqual: return val >= pred.FloatValue;
        case DataQueryOperator::Range: return val >= pred.FloatValue && val <= pred.FloatValueHigh;
        default: return false;
        }
    }

    case DataLensValueType::Double:
    {
        double val;
        std::memcpy(&val, converted, sizeof(double));
        switch (pred.Op)
        {
        case DataQueryOperator::Equals: return val == pred.DoubleValue;
        case DataQueryOperator::NotEquals: return val != pred.DoubleValue;
        case DataQueryOperator::Less: return val < pred.DoubleValue;
        case DataQueryOperator::LessOrEqual: return val <= pred.DoubleValue;
        case DataQueryOperator::Greater: return val > pred.DoubleValue;
        case DataQueryOperator::GreaterOrEqual: return val >= pred.DoubleValue;
        case DataQueryOperator::Range: return val >= pred.DoubleValue && val <= pred.DoubleValueHigh;
        default: return false;
        }
    }

    case DataLensValueType::GUID:
        return std::memcmp(converted, &pred.UIntValue, 16) == 0;

    default:
        return false;
    }
}

std::vector<std::vector<size_t>> DataLens::FilterRowsByPredicates(const std::vector<std::vector<size_t>>& candidateRows, const DataQueryObject& query) const
{
    if (query.Predicates.empty()) return candidateRows;

    std::vector<std::vector<size_t>> filteredRows;

    // Flatten candidateRows per combination of stores
    // Each candidate row combination corresponds to one "row set" for multi-store queries
    for (const auto& rowCombination : candidateRows)
    {
        bool passAll = true;

        // Prepare row pointers for each store in this combination
        std::vector<uint8_t*> rowPtrs;
        for (size_t storeIdx = 0; storeIdx < rowCombination.size(); ++storeIdx)
        {
            rowPtrs.push_back(GetRowPointer(storeIdx, rowCombination[storeIdx]));
        }

        // Evaluate all predicates
        for (const auto& pred : query.Predicates)
        {
            if (!EvaluatePredicate(pred, rowPtrs, mStores))
            {
                passAll = false;
                break;
            }
        }

        if (passAll)
            filteredRows.push_back(rowCombination);
    }

    return filteredRows;
}

void DataLens::EvaluateExpressionTree(size_t exprIndex, const std::vector<DataQueryExpr>& expressions, const std::vector<uint8_t*>& rowPtrs, std::vector<uint8_t>& outRow) const
{
    if (exprIndex >= expressions.size())
        return; // invalid index

    const DataQueryExpr& expr = expressions[exprIndex];

    switch (expr.Type)
    {
    case DataQueryExprType::Constant:
    {
        // Copy constant value into outRow
        size_t stride = DataLensValueTypeUtils::GetStride(expr.ResultType);
        switch (expr.ResultType)
        {
        case DataLensValueType::Bool:      std::memcpy(outRow.data(), &expr.BoolValue, stride); break;
        case DataLensValueType::Int8:      std::memcpy(outRow.data(), &expr.IntValue, stride); break;
        case DataLensValueType::Int16:     std::memcpy(outRow.data(), &expr.IntValue, stride); break;
        case DataLensValueType::Int32:     std::memcpy(outRow.data(), &expr.IntValue, stride); break;
        case DataLensValueType::Int64:     std::memcpy(outRow.data(), &expr.IntValue, stride); break;
        case DataLensValueType::UInt8:     std::memcpy(outRow.data(), &expr.UIntValue, stride); break;
        case DataLensValueType::UInt16:    std::memcpy(outRow.data(), &expr.UIntValue, stride); break;
        case DataLensValueType::UInt32:    std::memcpy(outRow.data(), &expr.UIntValue, stride); break;
        case DataLensValueType::UInt64:    std::memcpy(outRow.data(), &expr.UIntValue, stride); break;
        case DataLensValueType::Float:     std::memcpy(outRow.data(), &expr.FloatValue, stride); break;
        case DataLensValueType::Double:    std::memcpy(outRow.data(), &expr.DoubleValue, stride); break;
        case DataLensValueType::GUID:      std::memcpy(outRow.data(), &expr.UIntValue, stride); break;
        }
        break;
    }

    case DataQueryExprType::ColumnRef:
    {
        // Copy value from store row
        const uint8_t* row = rowPtrs[expr.StoreIndex];
        size_t offset = 0;
        const DataStoreSchema& storeSchema = mSchema[expr.StoreIndex];
        for (size_t i = 0; i < expr.ColumnIndex; ++i)
            offset += storeSchema.Columns[i].GetStride();

        const uint8_t* src = row + offset;
        CopyValue(src, storeSchema.Columns[expr.ColumnIndex].Type, outRow.data(), expr.ResultType);
        break;
    }

    case DataQueryExprType::UnaryOp:
    {
        // Evaluate operand recursively
        std::vector<uint8_t> operandBuf(DataLensValueTypeUtils::GetStride(expr.ResultType));
        EvaluateExpressionTree(expr.OperandIndex, expressions, rowPtrs, operandBuf);
        ApplyUnaryOp(expr.UnaryOperator, operandBuf.data(), expr.ResultType, outRow.data(), expr.ResultType);
        break;
    }

    case DataQueryExprType::BinaryOp:
    {
        // Evaluate left and right operands recursively
        std::vector<uint8_t> leftBuf(DataLensValueTypeUtils::GetStride(expr.ResultType));
        std::vector<uint8_t> rightBuf(DataLensValueTypeUtils::GetStride(expr.ResultType));

        EvaluateExpressionTree(expr.LeftOperandIndex, expressions, rowPtrs, leftBuf);
        EvaluateExpressionTree(expr.RightOperandIndex, expressions, rowPtrs, rightBuf);

        ApplyBinaryOp(expr.BinaryOperator, leftBuf.data(), expr.ResultType, rightBuf.data(), expr.ResultType, outRow.data(), expr.ResultType);
        break;
    }

    case DataQueryExprType::Aggregate:
    {
        // Aggregates are evaluated at group level elsewhere
        // Here, you could fetch a precomputed aggregate value if available
        // For now, leave as zeroed
        std::fill(outRow.begin(), outRow.end(), 0);
        break;
    }

    default:
        // Unknown type
        std::fill(outRow.begin(), outRow.end(), 0);
        break;
    }
}

void DataLens::ApplyBinaryOp(DataQueryOperator op, const void* left, DataLensValueType leftType, const void* right, DataLensValueType rightType, void* out, DataLensValueType outType) const
{
    // Temporary buffers for operands converted to the output type
    uint8_t leftBuf[16] = { 0 };   // max size for GUID (16 bytes)
    uint8_t rightBuf[16] = { 0 };

    DataLensValueTypeUtils::ConvertData(left, leftType, leftBuf, outType);
    DataLensValueTypeUtils::ConvertData(right, rightType, rightBuf, outType);

    switch (outType)
    {
    case DataLensValueType::Bool:
    {
        bool l = *reinterpret_cast<bool*>(leftBuf);
        bool r = *reinterpret_cast<bool*>(rightBuf);
        bool result = false;
        switch (op)
        {
        case DataQueryOperator::And: result = l && r; break;
        case DataQueryOperator::Or:  result = l || r; break;
        case DataQueryOperator::Equals: result = l == r; break;
        case DataQueryOperator::NotEquals: result = l != r; break;
        default: break;
        }
        *reinterpret_cast<bool*>(out) = result;
        break;
    }
    case DataLensValueType::Int8:
    case DataLensValueType::Int16:
    case DataLensValueType::Int32:
    case DataLensValueType::Int64:
    {
        int64_t l = 0, r = 0, result = 0;
        std::memcpy(&l, leftBuf, DataLensValueTypeUtils::GetStride(outType));
        std::memcpy(&r, rightBuf, DataLensValueTypeUtils::GetStride(outType));

        switch (op)
        {
        case DataQueryOperator::Add: result = l + r; break;
        case DataQueryOperator::Subtract: result = l - r; break;
        case DataQueryOperator::Multiply: result = l * r; break;
        case DataQueryOperator::Divide: result = r != 0 ? l / r : 0; break;
        case DataQueryOperator::Modulo: result = r != 0 ? l % r : 0; break;
        case DataQueryOperator::Equals: result = l == r; break;
        case DataQueryOperator::NotEquals: result = l != r; break;
        case DataQueryOperator::Less: result = l < r; break;
        case DataQueryOperator::LessOrEqual: result = l <= r; break;
        case DataQueryOperator::Greater: result = l > r; break;
        case DataQueryOperator::GreaterOrEqual: result = l >= r; break;
        case DataQueryOperator::BitmaskHas: result = (l & r) != 0; break;
        case DataQueryOperator::BitmaskNot: result = (l & r) == 0; break;
        case DataQueryOperator::And: result = l & r; break;
        case DataQueryOperator::Or: result = l | r; break;
        case DataQueryOperator::Xor: result = l ^ r; break;
        default: break;
        }
        std::memcpy(out, &result, DataLensValueTypeUtils::GetStride(outType));
        break;
    }
    case DataLensValueType::UInt8:
    case DataLensValueType::UInt16:
    case DataLensValueType::UInt32:
    case DataLensValueType::UInt64:
    {
        uint64_t l = 0, r = 0, result = 0;
        std::memcpy(&l, leftBuf, DataLensValueTypeUtils::GetStride(outType));
        std::memcpy(&r, rightBuf, DataLensValueTypeUtils::GetStride(outType));

        switch (op)
        {
        case DataQueryOperator::Add: result = l + r; break;
        case DataQueryOperator::Subtract: result = l - r; break;
        case DataQueryOperator::Multiply: result = l * r; break;
        case DataQueryOperator::Divide: result = r != 0 ? l / r : 0; break;
        case DataQueryOperator::Modulo: result = r != 0 ? l % r : 0; break;
        case DataQueryOperator::Equals: result = l == r; break;
        case DataQueryOperator::NotEquals: result = l != r; break;
        case DataQueryOperator::Less: result = l < r; break;
        case DataQueryOperator::LessOrEqual: result = l <= r; break;
        case DataQueryOperator::Greater: result = l > r; break;
        case DataQueryOperator::GreaterOrEqual: result = l >= r; break;
        case DataQueryOperator::BitmaskHas: result = (l & r) != 0; break;
        case DataQueryOperator::BitmaskNot: result = (l & r) == 0; break;
        case DataQueryOperator::And: result = l & r; break;
        case DataQueryOperator::Or: result = l | r; break;
        case DataQueryOperator::Xor: result = l ^ r; break;
        default: break;
        }
        std::memcpy(out, &result, DataLensValueTypeUtils::GetStride(outType));
        break;
    }
    case DataLensValueType::Float:
    case DataLensValueType::Double:
    {
        double l = 0.0, r = 0.0, result = 0.0;
        if (outType == DataLensValueType::Float)
        {
            l = *reinterpret_cast<float*>(leftBuf);
            r = *reinterpret_cast<float*>(rightBuf);
        }
        else
        {
            l = *reinterpret_cast<double*>(leftBuf);
            r = *reinterpret_cast<double*>(rightBuf);
        }

        switch (op)
        {
        case DataQueryOperator::Add: result = l + r; break;
        case DataQueryOperator::Subtract: result = l - r; break;
        case DataQueryOperator::Multiply: result = l * r; break;
        case DataQueryOperator::Divide: result = r != 0.0 ? l / r : 0.0; break;
        case DataQueryOperator::Equals: result = l == r; break;
        case DataQueryOperator::NotEquals: result = l != r; break;
        case DataQueryOperator::Less: result = l < r; break;
        case DataQueryOperator::LessOrEqual: result = l <= r; break;
        case DataQueryOperator::Greater: result = l > r; break;
        case DataQueryOperator::GreaterOrEqual: result = l >= r; break;
        default: break;
        }

        if (outType == DataLensValueType::Float)
        {
            float f = static_cast<float>(result);
            std::memcpy(out, &f, sizeof(float));
        }
        else
        {
            std::memcpy(out, &result, sizeof(double));
        }
        break;
    }
    case DataLensValueType::GUID:
    default:
        // Not supported for arithmetic; fallback to zero
        std::memset(out, 0, DataLensValueTypeUtils::GetStride(outType));
        break;
    }
}

void DataLens::ApplyUnaryOp(DataQueryOperator op, const void* operand, DataLensValueType operandType, void* out, DataLensValueType outType) const
{
    // Temporary buffer for converted operand
    uint8_t buf[16] = { 0 }; // max size for GUID
    DataLensValueTypeUtils::ConvertData(operand, operandType, buf, outType);

    switch (outType)
    {
    case DataLensValueType::Bool:
    {
        bool val = *reinterpret_cast<bool*>(buf);
        bool result = false;

        switch (op)
        {
        case DataQueryOperator::LogicalNot: result = !val; break;
        default: result = val; break;
        }

        *reinterpret_cast<bool*>(out) = result;
        break;
    }

    case DataLensValueType::Int8:
    case DataLensValueType::Int16:
    case DataLensValueType::Int32:
    case DataLensValueType::Int64:
    {
        int64_t val = 0;
        std::memcpy(&val, buf, DataLensValueTypeUtils::GetStride(outType));
        int64_t result = 0;

        switch (op)
        {
        case DataQueryOperator::Negate: result = -val; break;
        case DataQueryOperator::LogicalNot: result = !val; break;
        default: result = val; break;
        }

        std::memcpy(out, &result, DataLensValueTypeUtils::GetStride(outType));
        break;
    }

    case DataLensValueType::UInt8:
    case DataLensValueType::UInt16:
    case DataLensValueType::UInt32:
    case DataLensValueType::UInt64:
    {
        uint64_t val = 0;
        std::memcpy(&val, buf, DataLensValueTypeUtils::GetStride(outType));
        uint64_t result = 0;

        switch (op)
        {
        case DataQueryOperator::Negate: result = static_cast<uint64_t>(-static_cast<int64_t>(val)); break;
        case DataQueryOperator::LogicalNot: result = !val; break;
        default: result = val; break;
        }

        std::memcpy(out, &result, DataLensValueTypeUtils::GetStride(outType));
        break;
    }

    case DataLensValueType::Float:
    case DataLensValueType::Double:
    {
        double val = (outType == DataLensValueType::Float) ?
            static_cast<double>(*reinterpret_cast<float*>(buf)) :
            *reinterpret_cast<double*>(buf);
        double result = 0.0;

        switch (op)
        {
        case DataQueryOperator::Negate: result = -val; break;
        case DataQueryOperator::LogicalNot: result = (val == 0.0 ? 1.0 : 0.0); break;
        default: result = val; break;
        }

        if (outType == DataLensValueType::Float)
        {
            float f = static_cast<float>(result);
            std::memcpy(out, &f, sizeof(float));
        }
        else
        {
            std::memcpy(out, &result, sizeof(double));
        }
        break;
    }

    case DataLensValueType::GUID:
    default:
        // Unary ops not supported on GUID, zero out
        std::memset(out, 0, DataLensValueTypeUtils::GetStride(outType));
        break;
    }
}

void DataLens::EvaluateExpression(const DataQueryExpr& expr, const std::vector<uint8_t*>& rowPtrs, std::vector<uint8_t>& outRow) const
{
    switch (expr.Type)
    {
    case DataQueryExprType::Constant:
    {
        // Copy constant value into outRow
        switch (expr.ResultType)
        {
        case DataLensValueType::Bool:
            *reinterpret_cast<bool*>(outRow.data()) = expr.BoolValue;
            break;
        case DataLensValueType::Int8:
            *reinterpret_cast<int8_t*>(outRow.data()) = static_cast<int8_t>(expr.IntValue);
            break;
        case DataLensValueType::UInt8:
            *reinterpret_cast<uint8_t*>(outRow.data()) = static_cast<uint8_t>(expr.UIntValue);
            break;
        case DataLensValueType::Int16:
            *reinterpret_cast<int16_t*>(outRow.data()) = static_cast<int16_t>(expr.IntValue);
            break;
        case DataLensValueType::UInt16:
            *reinterpret_cast<uint16_t*>(outRow.data()) = static_cast<uint16_t>(expr.UIntValue);
            break;
        case DataLensValueType::Int32:
            *reinterpret_cast<int32_t*>(outRow.data()) = static_cast<int32_t>(expr.IntValue);
            break;
        case DataLensValueType::UInt32:
            *reinterpret_cast<uint32_t*>(outRow.data()) = static_cast<uint32_t>(expr.UIntValue);
            break;
        case DataLensValueType::Int64:
            *reinterpret_cast<int64_t*>(outRow.data()) = expr.IntValue;
            break;
        case DataLensValueType::UInt64:
            *reinterpret_cast<uint64_t*>(outRow.data()) = expr.UIntValue;
            break;
        case DataLensValueType::Float:
            *reinterpret_cast<float*>(outRow.data()) = expr.FloatValue;
            break;
        case DataLensValueType::Double:
            *reinterpret_cast<double*>(outRow.data()) = expr.DoubleValue;
            break;
        case DataLensValueType::GUID:
            std::memcpy(outRow.data(), &expr.UIntValue, 16);
            break;
        }
        break;
    }

    case DataQueryExprType::ColumnRef:
    {
        const DataStoreSchema& store = mSchema[expr.StoreIndex];
        size_t offset = 0;
        for (size_t i = 0; i < expr.ColumnIndex; ++i)
            offset += store.Columns[i].GetStride();

        const uint8_t* src = rowPtrs[expr.StoreIndex] + offset;
        DataLensValueTypeUtils::ConvertData(src, store.Columns[expr.ColumnIndex].Type, outRow.data(), expr.ResultType);
        break;
    }

    case DataQueryExprType::UnaryOp:
    {
        std::vector<uint8_t> operandBuf(DataLensValueTypeUtils::GetStride(expr.ResultType));

        // Recursively evaluate the operand expression
        const DataQueryExpr& operandExpr = *reinterpret_cast<const DataQueryExpr*>(rowPtrs[expr.OperandIndex]);
        EvaluateExpression(operandExpr, rowPtrs, operandBuf);

        ApplyUnaryOp(expr.UnaryOperator, operandBuf.data(), expr.ResultType, outRow.data(), expr.ResultType);
        break;
    }

    case DataQueryExprType::BinaryOp:
    {
        std::vector<uint8_t> leftBuf(DataLensValueTypeUtils::GetStride(expr.ResultType));
        std::vector<uint8_t> rightBuf(DataLensValueTypeUtils::GetStride(expr.ResultType));

        // Recursively evaluate left and right expressions
        const DataQueryExpr& leftExpr = *reinterpret_cast<const DataQueryExpr*>(rowPtrs[expr.LeftOperandIndex]);
        const DataQueryExpr& rightExpr = *reinterpret_cast<const DataQueryExpr*>(rowPtrs[expr.RightOperandIndex]);

        EvaluateExpression(leftExpr, rowPtrs, leftBuf);
        EvaluateExpression(rightExpr, rowPtrs, rightBuf);

        ApplyBinaryOp(expr.BinaryOperator, leftBuf.data(), expr.ResultType,
            rightBuf.data(), expr.ResultType, outRow.data(), expr.ResultType);
        break;
    }

    case DataQueryExprType::Aggregate:
        // Aggregates are evaluated at group level elsewhere
        break;
    }
}
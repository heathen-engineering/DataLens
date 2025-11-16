/******************************************************************************
 * DataLens.cpp
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-14 - 2025-11-15
 ******************************************************************************/

#include "DataLens.h"
#include <cstring>
#include <regex>
#include <sstream>
#include <algorithm>

inline DataLens::DataLens(const DataLensSchema& schema)
    : mSchema(schema)
{
    // Pre-create a DataStore for each store in the schema
    mStores.reserve(mSchema.Count());

    for (size_t i = 0; i < mSchema.Count(); ++i)
    {
        const DataStoreSchema& store = mSchema[i];

        size_t prealloc = store.DefaultCapacity;
        if (prealloc == 0)
            prealloc = 0; // optional: allow zero-capacity stores; change if you want a minimum

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

DataQueryObject DataLens::GetQuery(const std::string& sql) const
{
    DataQueryObject query;

    // 1. Normalize whitespace and lowercase for keywords
    std::string str = sql;
    std::replace(str.begin(), str.end(), '\n', ' ');
    std::replace(str.begin(), str.end(), '\t', ' ');
    while (str.find("  ") != std::string::npos)
        str = std::regex_replace(str, std::regex("  "), " ");

    // Remove trailing semicolon
    if (!str.empty() && str.back() == ';')
        str.pop_back();

    std::string strLower = str;
    std::transform(strLower.begin(), strLower.end(), strLower.begin(), ::tolower);

    // 2. Extract SELECT, FROM, WHERE parts
    std::smatch m;
    std::regex selectRegex(R"(select\s+(.*?)\s+from\s+(\S+)(?:\s+where\s+(.*))?)", std::regex::icase);
    if (!std::regex_match(str, m, selectRegex))
        throw std::runtime_error("Invalid query syntax");

    std::string selectPart = m[1].str();
    std::string fromPart = m[2].str();
    std::string wherePart = m.size() > 3 ? m[3].str() : "";

    // 3. Resolve store
    size_t storeIdx = FindStore(fromPart);
    if (storeIdx == SIZE_MAX)
        throw std::runtime_error("Store not found: " + fromPart);
    query.DataStoreIndices.push_back(storeIdx);

    const DataStoreSchema& storeSchema = mSchema[storeIdx];

    // 4. Parse SELECT columns
    if (selectPart == "*" || selectPart == " * ")
    {
        query.SelectAll = true;
    }
    else
    {
        std::stringstream ss(selectPart);
        std::string colName;
        while (std::getline(ss, colName, ','))
        {
            colName.erase(std::remove_if(colName.begin(), colName.end(), ::isspace), colName.end());

            // Remove brackets if present
            if (!colName.empty() && colName.front() == '[' && colName.back() == ']')
                colName = colName.substr(1, colName.size() - 2);

            // Split by dot if present: store.column
            size_t dotPos = colName.find('.');
            size_t colStoreIdx = storeIdx;
            std::string columnOnly = colName;
            if (dotPos != std::string::npos)
            {
                std::string storeName = colName.substr(0, dotPos);
                columnOnly = colName.substr(dotPos + 1);
                colStoreIdx = FindStore(storeName);
                if (colStoreIdx == SIZE_MAX)
                    throw std::runtime_error("Store not found: " + storeName);
            }

            // Find column
            auto it = std::find_if(mSchema[colStoreIdx].Columns.begin(), mSchema[colStoreIdx].Columns.end(),
                [&](const DataStoreColumnSchema& col) { return col.Name == columnOnly; });

            if (it == mSchema[colStoreIdx].Columns.end())
                throw std::runtime_error("Column not found: " + columnOnly);

            size_t colIdx = std::distance(mSchema[colStoreIdx].Columns.begin(), it);
            query.SelectColumns.push_back({ colStoreIdx, colIdx });
        }
    }

    // 5. Parse WHERE predicates (simple ANDs)
    if (!wherePart.empty())
    {
        std::stringstream ss(wherePart);
        std::string predText;

        while (std::getline(ss, predText, 'A')) // crude split on AND
        {
            if (predText.substr(0, 2) == "ND") // skip the "ND" remainder
                predText = predText.substr(2);

            // Remove whitespace
            predText.erase(std::remove_if(predText.begin(), predText.end(), ::isspace), predText.end());

            if (predText.empty()) continue;

            // Support =, >, <, >=, <=
            static const std::vector<std::string> ops = { ">=", "<=", "!=", "=", ">", "<" };
            std::string op;
            size_t opPos = std::string::npos;
            for (auto& candidate : ops)
            {
                opPos = predText.find(candidate);
                if (opPos != std::string::npos)
                {
                    op = candidate;
                    break;
                }
            }

            if (op.empty())
                throw std::runtime_error("Unsupported operator in predicate: " + predText);

            std::string left = predText.substr(0, opPos);
            std::string right = predText.substr(opPos + op.size());

            // Resolve column
            size_t dotPos = left.find('.');
            size_t colStoreIdx = storeIdx;
            std::string columnOnly = left;
            if (dotPos != std::string::npos)
            {
                std::string storeName = left.substr(0, dotPos);
                columnOnly = left.substr(dotPos + 1);
                colStoreIdx = FindStore(storeName);
                if (colStoreIdx == SIZE_MAX)
                    throw std::runtime_error("Store not found: " + storeName);
            }

            auto it = std::find_if(mSchema[colStoreIdx].Columns.begin(), mSchema[colStoreIdx].Columns.end(),
                [&](const DataStoreColumnSchema& col) { return col.Name == columnOnly; });
            if (it == mSchema[colStoreIdx].Columns.end())
                throw std::runtime_error("Column not found: " + columnOnly);

            size_t colIdx = std::distance(mSchema[colStoreIdx].Columns.begin(), it);
            const DataStoreColumnSchema& colSchema = mSchema[colStoreIdx].Columns[colIdx];

            // Construct predicate
            DataQueryPredicate pred{};
            pred.StoreIndex = colStoreIdx;
            pred.ColumnIndex = colIdx;
            pred.ColumnType = colSchema.Type;

            // Convert right-hand side to appropriate value
            switch (colSchema.Type)
            {
            case DataLensValueType::Bool:
                pred.IntValue = (right == "true" || right == "1") ? 1 : 0;
                pred.Op = (op == "=") ? DataQueryOperator::Equals : DataQueryOperator::NotEquals;
                break;
            case DataLensValueType::Int8:
            case DataLensValueType::Int16:
            case DataLensValueType::Int32:
            case DataLensValueType::Int64:
                pred.IntValue = std::stoll(right);
                pred.Op = (op == "=") ? DataQueryOperator::Equals : DataQueryOperator::Greater;
                if (op == "!=") pred.Op = DataQueryOperator::Not;
                else if (op == ">") pred.Op = DataQueryOperator::Greater;
                else if (op == "<") pred.Op = DataQueryOperator::Less;
                else if (op == ">=") pred.Op = DataQueryOperator::GreaterOrEqual;
                else if (op == "<=") pred.Op = DataQueryOperator::LessOrEqual;
                break;
            case DataLensValueType::UInt8:
            case DataLensValueType::UInt16:
            case DataLensValueType::UInt32:
            case DataLensValueType::UInt64:
                pred.UIntValue = std::stoull(right);
                pred.Op = (op == "=") ? DataQueryOperator::Equals : DataQueryOperator::Greater;
                if (op == "!=") pred.Op = DataQueryOperator::Not;
                else if (op == ">") pred.Op = DataQueryOperator::Greater;
                else if (op == "<") pred.Op = DataQueryOperator::Less;
                else if (op == ">=") pred.Op = DataQueryOperator::GreaterOrEqual;
                else if (op == "<=") pred.Op = DataQueryOperator::LessOrEqual;
                break;
            case DataLensValueType::Float:
                pred.FloatValue = std::stof(right);
                if (op == "=") pred.Op = DataQueryOperator::Equals;
                else if (op == "!=") pred.Op = DataQueryOperator::Not;
                else if (op == ">") pred.Op = DataQueryOperator::Greater;
                else if (op == "<") pred.Op = DataQueryOperator::Less;
                else if (op == ">=") pred.Op = DataQueryOperator::GreaterOrEqual;
                else if (op == "<=") pred.Op = DataQueryOperator::LessOrEqual;
                break;
            case DataLensValueType::Double:
                pred.DoubleValue = std::stod(right);
                if (op == "=") pred.Op = DataQueryOperator::Equals;
                else if (op == "!=") pred.Op = DataQueryOperator::Not;
                else if (op == ">") pred.Op = DataQueryOperator::Greater;
                else if (op == "<") pred.Op = DataQueryOperator::Less;
                else if (op == ">=") pred.Op = DataQueryOperator::GreaterOrEqual;
                else if (op == "<=") pred.Op = DataQueryOperator::LessOrEqual;
                break;
            default:
                throw std::runtime_error("Unsupported data type in predicate parsing");
            }

            query.Predicates.push_back(pred);
        }
    }

    return query;
}

std::vector<uint8_t> DataLens::RunQuery(const DataQueryObject& query)
{
    std::vector<DataQueryResultRow> matchingRows;

    // Step 1: Generate candidate rows
    for (size_t rowIndex = 0; rowIndex < mStores[query.DataStoreIndices[0]].GetRowCount(); ++rowIndex)
    {
        DataQueryResultRow rowCombo;
        rowCombo.RowIndicesPerStore.resize(query.DataStoreIndices.size());
        rowCombo.RowIndicesPerStore[0] = rowIndex;

        bool passesJoins = true;

        // Evaluate joins for this rowCombo
        for (const auto& join : query.Joins)
        {
            size_t leftRow = rowCombo.RowIndicesPerStore[join.LeftStoreIndex];
            bool matchFound = false;

            for (size_t rightRow = 0; rightRow < mStores[join.RightStoreIndex].GetRowCount(); ++rightRow)
            {
                if (mStores[join.LeftStoreIndex].CompareCells(leftRow, join.LeftColumnIndex,
                    mStores[join.RightStoreIndex], rightRow, join.RightColumnIndex))
                {
                    rowCombo.RowIndicesPerStore[join.RightStoreIndex] = rightRow;
                    matchFound = true;
                    break;
                }
            }

            if (!matchFound)
            {
                passesJoins = false;
                break;
            }
        }

        if (!passesJoins) continue;

        // Step 2: Evaluate predicates using DataStore::MatchesPredicate
        bool passesPredicates = true;
        for (const auto& pred : query.Predicates)
        {
            const DataStore& store = mStores[pred.StoreIndex];
            size_t rowIdx = rowCombo.RowIndicesPerStore[pred.StoreIndex];

            if (!store.MatchesPredicate(rowIdx, pred))
            {
                passesPredicates = false;
                break;
            }
        }

        if (passesPredicates)
            matchingRows.push_back(rowCombo);
    }

    // Step 3: Build flat byte array of results
    size_t rowStride = GetRowStride(query);
    std::vector<uint8_t> results(matchingRows.size() * rowStride);

    for (size_t i = 0; i < matchingRows.size(); ++i)
    {
        size_t offset = i * rowStride;

        if (query.SelectAll)
        {
            for (size_t storeIdx : query.DataStoreIndices)
            {
                const DataStore& store = mStores[storeIdx];
                for (size_t colIdx = 0; colIdx < mSchema[storeIdx].Columns.size(); ++colIdx)
                {
                    size_t rowIdx = matchingRows[i].RowIndicesPerStore[storeIdx];
                    store.CopyCellToFlatRow(rowIdx, colIdx, &results[offset]);
                    offset += mSchema[storeIdx].Columns[colIdx].GetStride();
                }
            }
        }
        else
        {
            for (const auto& selCol : query.SelectColumns)
            {
                const DataStore& store = mStores[selCol.StoreIndex];
                size_t rowIdx = matchingRows[i].RowIndicesPerStore[selCol.StoreIndex];
                store.CopyCellToFlatRow(rowIdx, selCol.ColumnIndex, &results[offset]);
                offset += mSchema[selCol.StoreIndex].Columns[selCol.ColumnIndex].GetStride();
            }
        }
    }

    // Step 4: Apply sort
    ApplySort(results, query);

    // Step 5: Apply limit/offset
    ApplyLimitOffset(results, query);

    return results;
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
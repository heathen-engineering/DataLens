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

std::pair<DataQueryObject, DataViewSchema> DataLens::GetQuery(const std::string& sql) const
{
    DataQueryObject query;
    DataViewSchema view;

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

    // 4. Add synthetic IsValid column as first column
    DataQueryColumn isValidCol{};
    isValidCol.StoreIndex = SIZE_MAX;   // synthetic / cache managed
    isValidCol.ColumnIndex = SIZE_MAX;
    isValidCol.ResultType = DataLensValueType::Bool;
    isValidCol.IsCalculated = false;
    query.SelectColumns.push_back(isValidCol);

    DataViewColumnSchema isValidSchema{};
    isValidSchema.Name = "IsValid";
    isValidSchema.Type = DataLensValueType::Bool;
    view.Columns.push_back(isValidSchema);

    // 5. Parse SELECT columns
    if (selectPart == "*" || selectPart == " * ") {
        query.SelectAll = true;
        // Add remaining columns to view schema
        for (size_t storeIdx : query.DataStoreIndices) {
            const auto& store = mSchema[storeIdx];
            for (size_t colIdx = 0; colIdx < store.Columns.size(); ++colIdx) {
                DataViewColumnSchema colSchema{};
                colSchema.Name = store.Columns[colIdx].Name;
                colSchema.Type = store.Columns[colIdx].Type;
                view.Columns.push_back(colSchema);
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
            size_t storeIdx = query.DataStoreIndices[0]; // default first store
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

            view.Columns.push_back({ it->Name, it->Type });

            if (isAggregate) {
                query.Aggregates.push_back({ aggType, query.Expressions.size(), query.SelectColumns.size() - 1 });
            }
        }
    }

    // 6. Parse WHERE predicates (flat ANDs)
    bool hasIsValidFilter = false;
    if (!wherePart.empty()) {
        std::stringstream predStream(wherePart);
        std::string predText;
        while (std::getline(predStream, predText, 'A')) { // crude split on AND
            if (predText.substr(0, 2) == "ND") predText = predText.substr(2);
            predText.erase(std::remove_if(predText.begin(), predText.end(), ::isspace), predText.end());
            if (predText.empty()) continue;

            // Check if predicate is IsValid
            if (predText.find("IsValid") != std::string::npos) {
                hasIsValidFilter = true;
                // TODO: parse operator and value into DataQueryPredicate
            }

            // TODO: parse other predicates here
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

    view.Query = query;
    return { query, view };
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
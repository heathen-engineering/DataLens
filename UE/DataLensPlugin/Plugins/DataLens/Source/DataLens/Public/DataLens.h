/******************************************************************************
 * DataLens.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-14 - 2025-11-15
 ******************************************************************************/

#pragma once

#include <vector>
#include "DataStore.h"

class DataLens
{
public:
	/// <summary>
	/// Creates a new DataLens system with the indicated schema
	/// </summary>
	/// <param name="schema"></param>
	explicit DataLens(const DataLensSchema& schema);
	/// <summary>
	/// Serialize the schema and stores to a byte array.
	/// This is useful for writing to disk as a save file.
	/// Its generally recommended to compress the byte array before writing.
	/// </summary>
	/// <returns>The uncompressed bytes of the serialized data</returns>
	std::vector<uint8_t> Serialize() const;
	/// <summary>
	/// Deserialize an uncompressed byte array representing a complete DataLens system.
	/// This is useful for reading from disk, e.g. loading a file.
	/// </summary>
	/// <param name="data">This should be an uncompressed raw byte array, same as you would have gotten from Serialize</param>
	void Deserialize(const std::vector<uint8_t>& data);

	/************************************************************
	 *
	 * Data Store Features
	 *
	 ************************************************************/
	/// <summary>
	/// Get the index of the store matching this name.
	/// You should cash this index to avoid future string lookups
	/// </summary>
	/// <param name="name">The case sensitive name of the store you wish to find</param>
	/// <returns>The index of the matching store if any, if none this will return SIZE_MAX</returns>
	size_t FindStore(const std::string& name) const;
	size_t GetStoreRowCount(size_t id);
	
	/************************************************************
	 * 
	 * Data View Features
	 * 
	 ************************************************************/

	bool HasView(const std::string& name);
	bool IsViewValid(size_t id);
	size_t FindOrCreateView(const std::string& name);
	void RemoveView(size_t id);
	void SetViewSelect(size_t id, const std::string sql);
	void SetViewInsert(size_t id, const std::string sql);
	void SetViewUpdate(size_t id, const std::string sql);
	void SetViewDelete(size_t id, const std::string sql);
	void SetViewFrequency(size_t id, uint8_t frequency);
	void SetViewCanInsert(size_t id, bool canInsert);
	void SetViewCanUpdate(size_t id, bool canUpdate);
	std::vector<DataViewColumnSchema> GetViewSchema(size_t id) const;
	bool GetViewRow(size_t view, size_t row, uint8_t* buffer) const;
	bool SetViewRow(size_t view, size_t row, const uint8_t* data);
	size_t AddViewRow(size_t view, const uint8_t* data);
	bool RemoveViewRow(size_t view, size_t row);
	void RefreshView(size_t id);
	void FlushView(size_t id);

private:
	DataLensSchema mSchema; 
	std::vector<DataStore> mStores;
	
	/// <summary>
	/// Creates a new QueryObject based on the sql like expression provided
	/// </summary>
	/// <param name="sql">A TSQL like expression defining the query you wish to run</param>
	/// <returns>A compiled and ready to run query</returns>
	std::pair<DataQueryObject, DataViewSchema> GetQuery(const std::string& sql) const;
	/// <summary>
	/// Creates a new UpdateObject based on the sql like expression provided
	/// </summary>
	/// <param name="sql">A TSQL like expression defining the query you wish to run</param>
	/// <returns>A compiled and ready to run update</returns>
	DataUpdateObject GetUpdate(const std::string sql);

	/// <summary>
	/// Run a prepared query object and return the raw row major results.
	/// </summary>
	/// <param name="query">The query to run, this must be a pre-prepared query</param>
	/// <returns>The row major results as a byte array, it is up to the consumer to translate the bytes to the expected column order</returns>
	QueryResultCache RunQuery(const DataQueryObject& query);
	/// <summary>
	/// 
	/// </summary>
	/// <param name="viewRegistry"></param>
	/// <returns></returns>
	std::vector<DataCommandValue> RunUpdate(const DataViewRegistry& viewRegistry);

	void ApplySort(std::vector<uint8_t>& results, const DataQueryObject& query);
	void ApplyLimitOffset(std::vector<uint8_t>& results, const DataQueryObject& query);
	size_t GetRowStride(const DataQueryObject& query);
	static void WriteString(std::vector<uint8_t>& out, const std::string& str);
	static std::string ReadString(const uint8_t* data, size_t& offset, size_t dataSize);

	std::string ToUpper(const std::string& s) const;
	std::string Trim(const std::string& s) const;
	std::vector<std::string> SplitCSV(const std::string& s) const;
	std::vector<uint8_t> LiteralToBytes(const std::string& lit, DataLensValueType type) const;
	size_t ParseOperandToExprIndex(const std::string& token, DataUpdateObject& update);
	DataQueryOperator OpStringToOperator(const std::string& op) const;
	DataQueryPredicate ParseWhereToPredicate(const std::string& whereText) const;
};
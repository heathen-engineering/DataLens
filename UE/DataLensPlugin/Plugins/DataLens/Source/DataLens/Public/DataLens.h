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
	/// <summary>
	/// Get the index of the store matching this name.
	/// You should cash this index to avoid future string lookups
	/// </summary>
	/// <param name="name">The case sensitive name of the store you wish to find</param>
	/// <returns>The index of the matching store if any, if none this will return SIZE_MAX</returns>
	size_t FindStore(const std::string& name) const;
	/// <summary>
	/// Creates a new QueryObject based on the sql like expression provided
	/// </summary>
	/// <param name="sql">A TSQL like expression defining the query you wish to run</param>
	/// <returns>A compiled and ready to run query</returns>
	DataQueryObject GetQuery(const std::string& sql) const;
	/// <summary>
	/// Run a prepared query object and return the raw row major results.
	/// </summary>
	/// <param name="query">The query to run, this must be a pre-prepared query</param>
	/// <returns>The row major results as a byte array, it is up to the consumer to translate the bytes to the expected column order</returns>
	std::vector<uint8_t> RunQuery(const DataQueryObject& query);
	// Update
	// CommitAll
	// Commit
private:
	DataLensSchema mSchema; 
	std::vector<DataStore> mStores;
	// storage for our pending update commands
	// storage for our registered views
	// CommitViews
	// RefreshViews
	void ApplySort(std::vector<uint8_t>& results, const DataQueryObject& query);
	void ApplyLimitOffset(std::vector<uint8_t>& results, const DataQueryObject& query);
	size_t GetRowStride(const DataQueryObject& query);
	static void WriteString(std::vector<uint8_t>& out, const std::string& str);
	static std::string ReadString(const uint8_t* data, size_t& offset, size_t dataSize);
};
/******************************************************************************
 * DataLens.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-14 - 2025-11-15
 ******************************************************************************/

#pragma once

#include "DataLensSchema.h"
#include "DataStore.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
	bool IsViewValid(size_t id) const;
	size_t FindOrCreateView(const std::string& name);
	void RemoveView(size_t id);
	bool SetViewSelect(size_t id, const std::string sql);
	bool SetViewInsert(size_t id, const std::string sql);
	bool SetViewUpdate(size_t id, const std::string sql);
	bool SetViewDelete(size_t id, const std::string sql);
	bool SetViewFrequency(size_t id, uint8_t frequency);
	bool SetViewCanInsert(size_t id, bool canInsert);
	bool SetViewCanUpdate(size_t id, bool canUpdate);
	std::vector<DataViewColumnSchema> GetViewSchema(size_t id) const;
	bool GetViewRow(size_t view, size_t row, uint8_t* buffer) const;
	bool SetViewRow(size_t view, size_t row, const uint8_t* data);
	size_t AddViewRow(size_t view, const uint8_t* data);
	bool RemoveViewRow(size_t view, size_t row);
	bool RefreshView(size_t id);
	bool FlushView(size_t id);

private:
	DataLensSchema mSchema; 
	std::vector<DataStore> mStores;
	std::vector<DataViewRegistry> mViews;
	std::unordered_map<std::string, size_t> mViewNameToID;
	
	/// <summary>
	/// Creates a new QueryObject based on the sql like expression provided
	/// </summary>
	/// <param name="sql">A TSQL like expression defining the query you wish to run</param>
	/// <returns>A compiled and ready to run query</returns>
	std::pair<DataQueryObject, std::vector<DataViewColumnSchema>> GetQuery(const std::string& sql) const;
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

	/// <summary>Gather all row indices from involved stores before applying predicates.</summary>
	/// <param name="query">The query object describing involved stores.</param>
	/// <returns>A vector of row index vectors, one per store.</returns>
	std::vector<std::vector<size_t>> GatherCandidateRows(const DataQueryObject& query) const;

	/// <summary>Generate Cartesian product or join-compliant row sets for multi-store queries.</summary>
	/// <param name="storeIndices">Indices of stores involved.</param>
	/// <param name="joins">Join definitions between stores.</param>
	/// <returns>A vector of joined row index sets.</returns>
	std::vector<std::vector<size_t>> PerformJoins(const std::vector<size_t>& storeIndices, const std::vector<DataQueryJoin>& joins) const;

	/// <summary>Evaluate a single predicate against a row.</summary>
	/// <param name="pred">The predicate to evaluate.</param>
	/// <param name="rowPtrs">Pointers to the row data.</param>
	/// <param name="stores">Reference to all data stores.</param>
	/// <returns>True if the row satisfies the predicate, false otherwise.</returns>
	bool EvaluatePredicate(const DataQueryPredicate& pred, const std::vector<uint8_t*>& rowPtrs, const std::vector<DataStore>& stores) const;

	/// <summary>Filter a set of candidate rows by all predicates, including nested ones.</summary>
	/// <param name="candidateRows">Candidate row index sets.</param>
	/// <param name="query">The query object containing predicates.</param>
	/// <returns>Filtered candidate rows.</returns>
	std::vector<std::vector<size_t>> FilterRowsByPredicates(const std::vector<std::vector<size_t>>& candidateRows, const DataQueryObject& query) const;

	/// <summary>
	/// Evaluate an expression tree by index into a set of expressions for a specific row.
	/// </summary>
	/// <param name="exprIndex">Root expression index</param>
	/// <param name="expressions">The full array of expressions</param>
	/// <param name="rowPtrs">Pointers to current row data per store</param>
	/// <param name="outRow">Buffer to write the result</param>
	void EvaluateExpressionTree(size_t exprIndex, const std::vector<DataQueryExpr>& expressions, const std::vector<uint8_t*>& rowPtrs, std::vector<uint8_t>& outRow) const;

	/// <summary>Apply a binary operator to two typed operands and store result in output.</summary>
	void ApplyBinaryOp(DataQueryOperator op, const void* left, DataLensValueType leftType, const void* right, DataLensValueType rightType, void* out, DataLensValueType outType) const;

	/// <summary>Apply a unary operator to a typed operand and store result in output.</summary>
	void ApplyUnaryOp(DataQueryOperator op, const void* operand, DataLensValueType operandType, void* out, DataLensValueType outType) const;

	/// <summary>Evaluate a single expression tree for a given row.</summary>
	/// <param name="expr">The expression to evaluate.</param>
	/// <param name="rowPtrs">Pointers to the row data.</param>
	/// <param name="outRow">Output buffer for evaluated row data.</param>
	void EvaluateExpression(const DataQueryExpr& expr, const std::vector<uint8_t*>& rowPtrs, std::vector<uint8_t>& outRow) const;

	/// <summary>Evaluate all calculated columns for a set of rows.</summary>
	/// <param name="expressions">Expression trees to evaluate.</param>
	/// <param name="rowPtrs">Pointers to row data sets.</param>
	/// <param name="outputRows">Output buffer for evaluated rows.</param>
	void EvaluateExpressions(const std::vector<DataQueryExpr>& expressions, const std::vector<std::vector<uint8_t*>>& rowPtrs, std::vector<std::vector<uint8_t>>& outputRows) const;

	/// <summary>Group rows based on GroupByColumns.</summary>
	/// <param name="rows">Rows to group.</param>
	/// <param name="groupByColumns">Indices of columns to group by.</param>
	/// <returns>Map of group keys to row indices.</returns>
	std::unordered_map<std::string, std::vector<size_t>> GroupRows(const std::vector<std::vector<uint8_t>>& rows, const std::vector<size_t>& groupByColumns) const;

	/// <summary>Apply aggregates to grouped rows.</summary>
	/// <param name="aggregates">Aggregates to apply.</param>
	/// <param name="groupedRows">Grouped row indices to update with aggregate results.</param>
	void ApplyAggregates(const std::vector<DataQueryAggregate>& aggregates, std::unordered_map<std::string, std::vector<size_t>>& groupedRows) const;

	/// <summary>Build the flat row-major cache for a DataView.</summary>
	/// <param name="view">The view schema defining the cache.</param>
	/// <param name="evaluatedRows">Rows after evaluation.</param>
	/// <param name="cache">The cache to populate.</param>
	void BuildRowCache(const DataViewSchema& view, const std::vector<std::vector<uint8_t>>& evaluatedRows, DataViewRegistry::CacheMetadata& cache);

	/// <summary>Sort cache based on query SortColumns, including expression support.</summary>
	/// <param name="cache">The cache to sort.</param>
	/// <param name="sortColumns">Sort column definitions.</param>
	void SortRowCache(DataViewRegistry::CacheMetadata& cache, const std::vector<DataQuerySortColumn>& sortColumns) const;

	/// <summary>Apply LIMIT and OFFSET to final cache data.</summary>
	/// <param name="cache">The cache to modify.</param>
	/// <param name="query">Query object containing limit and offset.</param>
	void ApplyLimitOffsetToCache(DataViewRegistry::CacheMetadata& cache, const DataQueryObject& query) const;

	/// <summary>Convert cache rows to DataCommandValue objects for target stores.</summary>
	/// <param name="viewRegistry">View registry containing cache data.</param>
	/// <returns>Vector of DataCommandValue updates.</returns>
	std::vector<DataCommandValue> BuildUpdatesFromCache(const DataViewRegistry& viewRegistry) const;

	/// <summary>Apply all DataCommandValue updates to target stores.</summary>
	/// <param name="updates">Updates to apply.</param>
	/// <returns>True if all updates succeeded.</returns>
	bool ApplyUpdates(const std::vector<DataCommandValue>& updates);

	/// <summary>Get a pointer to a specific row in a store.</summary>
	/// <param name="storeIndex">Index of the store.</param>
	/// <param name="rowIndex">Index of the row.</param>
	/// <returns>Pointer to the row data.</returns>
	uint8_t* GetRowPointer(size_t storeIndex, size_t rowIndex) const;

	/// <summary>Copy a single value from source to destination with type conversion.</summary>
	/// <param name="src">Source pointer.</param>
	/// <param name="srcType">Source type.</param>
	/// <param name="dst">Destination pointer.</param>
	/// <param name="dstType">Destination type.</param>
	void CopyValue(const uint8_t* src, DataLensValueType srcType, uint8_t* dst, DataLensValueType dstType) const;

	/// <summary>Generate a group key string from row pointers for given group columns.</summary>
	/// <param name="rowPtrs">Pointers to the rows.</param>
	/// <param name="groupColumns">Indices of columns used for grouping.</param>
	/// <returns>String key representing the group.</returns>
	std::string MakeGroupKey(const std::vector<uint8_t*>& rowPtrs, const std::vector<size_t>& groupColumns) const;
};
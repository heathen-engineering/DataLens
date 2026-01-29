/******************************************************************************
 * DataLens.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2026-01-29
 ******************************************************************************/

#pragma once

#include "DataLensSchema.h"
#include "DataStore.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

DECLARE_LOG_CATEGORY_EXTERN(LogDataLens, Log, All);

class DataLens
{
public:
	/// <summary>
	/// Creates a new DataLens system with the indicated schema
	/// </summary>
	/// <param name="schema"></param>
	explicit DataLens(const DataLensSchema& schema);

	bool IsValid() const { return bIsValid; }

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

private:
	DataLensSchema mSchema;
	std::vector<DataStore> mStores;
	std::vector<DataViewRegistry> mViews;
	std::unordered_map<std::string, size_t> mViewNameToID;
	
	bool bIsValid = true;
	
	bool ResolveJoinsInline(const DataQueryObject& query, std::vector<size_t>& rowIndices) const;
	bool EvaluatePredicatesInline(const DataQueryObject& query, const std::vector<size_t>& rowIndices, size_t viewIndex) const;
	void EvaluateExpressionInline(const DataQueryExpr& expr,
								  const std::vector<size_t>& rowIndices,
								  const std::vector<size_t>& storeMapping,
								  const DataQueryObject& query,
								  void* dst) const;
	uint8_t* AppendEmptyRow(DataViewRegistry::CacheMetadata& cache);
	void ApplyGroupingAndAggregatesInPlace(const DataViewSchema& view,
										   DataViewRegistry::CacheMetadata& cache,
										   const DataQueryObject& query);

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
	DataUpdateObject GetUpdate(size_t viewIndex, const std::string sql);

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
	DataQueryPredicate ParseWhereToPredicate(size_t viewIndex, const std::string& whereText) const;

	/// <summary>
	/// Evaluates a single predicate against a specific combination of rows from one or more stores.
	/// </summary>
	/// <param name="pred">The predicate to evaluate.</param>
	/// <param name="rowIndices">
	/// A vector of row indices corresponding to the candidate rows for each store in storeMapping. 
	/// Each element represents the row index in the respective store.
	/// </param>
	/// <param name="storeMapping">
	/// A vector mapping the query's logical store indices to actual store IDs in the DataLens instance. 
	/// Used to resolve which store each rowIndex belongs to.
	/// </param>
	/// <param name="viewIndex">
	/// Optional index of the view to use if the predicate references cached columns. 
	/// Defaults to SIZE_MAX if no view is involved.
	/// </param>
	/// <returns>
	/// True if the row combination satisfies the predicate; otherwise, false.
	/// </returns>
	bool EvaluatePredicate(const DataQueryPredicate& pred,
									 const std::vector<size_t>& rowIndices,
									 const std::vector<size_t>& storeMapping,
									 size_t viewIndex = SIZE_MAX) const;
	
	/// <summary>
	/// Evaluate an expression tree by index into a set of expressions for a specific row.
	/// </summary>
	/// <param name="exprIndex">Root expression index</param>
	/// <param name="expressions">The full array of expressions</param>
	/// <param name="rowPtrs">Pointers to current row data per store</param>
	/// <param name="outRow">Buffer to write the result</param>
	void EvaluateExpressionTree(size_t exprIndex, const std::vector<DataQueryExpr>& expressions,
	                            const std::vector<uint8_t*>& rowPtrs, std::vector<uint8_t>& outRow) const;

	/// <summary>Apply a binary operator to two typed operands and store result in output.</summary>
	void ApplyBinaryOp(DataQueryOperator op, const void* left, DataLensValueType leftType, const void* right,
	                   DataLensValueType rightType, void* out, DataLensValueType outType) const;

	/// <summary>Apply a unary operator to a typed operand and store result in output.</summary>
	void ApplyUnaryOp(DataQueryOperator op, const void* operand, DataLensValueType operandType, void* out,
	                  DataLensValueType outType) const;

	/// <summary>Evaluate a single expression tree for a given row.</summary>
	/// <param name="expr">The expression to evaluate.</param>
	/// <param name="rowPtrs">Pointers to the row data.</param>
	/// <param name="outRow">Output buffer for evaluated row data.</param>
	void EvaluateExpression(const std::vector<DataQueryExpr>& expressions,
							const DataQueryExpr& expr,
							const std::vector<size_t>& rowIndicesPerStore,
							const std::vector<size_t>& storeMapping,
							std::vector<uint8_t>& outValue) const;
	
	/// <summary>Group rows based on GroupByColumns.</summary>
	/// <param name="SchemaColumns">The structure of the rows passed in</param>
	/// <param name="rows">Rows to group.</param>
	/// <param name="groupByColumns">Indices of columns to group by.</param>
	/// <returns>Map of group keys to row indices.</returns>
	std::unordered_map<std::string, std::vector<size_t>> GroupRows(
		const std::vector<DataViewColumnSchema> SchemaColumns, 
		const std::vector<std::vector<uint8_t>>& rows,
		const std::vector<size_t>& groupByColumns) const;

	/// <summary>
	/// Apply aggregates to grouped rows.
	/// </summary>
	/// <param name="schemaColumns">
	/// The schema for the evaluated rows, defining column types and strides.
	/// </param>
	/// <param name="evaluatedRows">
	/// Evaluated rows after expression execution (row-major, each row is a byte vector).
	/// </param>
	/// <param name="aggregates">
	/// The aggregates to apply (SUM, AVG, COUNT, MIN, MAX, etc.).
	/// </param>
	/// <param name="groupedRows">
	/// Map of group keys to row indices. This will be updated in-place to contain
	/// the aggregated rows (usually by replacing the row index with the aggregated
	/// row index or updating the referenced row values).
	/// </param>
	void ApplyAggregates(
	const std::vector<DataViewColumnSchema>& schema,
	const std::vector<std::vector<uint8_t>>& evaluatedRows,
	const std::vector<DataQueryAggregate>& aggregates,
	std::unordered_map<std::string, std::vector<size_t>>& groupedRows) const;

	/// <summary>
	/// Build the flat row-major cache for a DataView.
	/// </summary>
	/// <param name="view">
	/// The view schema defining the cache layout (columns + query).
	/// </param>
	/// <param name="evaluatedRows">
	/// Rows after expression evaluation, grouped/aggregated if required.
	/// Each row is a vector of bytes matching the view's column schema.
	/// </param>
	/// <param name="cache">
	/// Cache metadata to populate (RowStride, ColumnOffsets, Data, RowStartPointers, DirtyRows).
	/// </param>
	void BuildRowCache(const DataViewSchema& view, const std::vector<std::vector<uint8_t>>& evaluatedRows,
	                   DataViewRegistry::CacheMetadata& cache);

	/// <summary>
	/// Sort cache based on query SortColumns, including expression support.
	/// </summary>
	/// <param name="cache">The cache to sort.</param>
	/// <param name="view">The owning view used to understand column type and stride</param>
	/// <param name="sortColumns">Sort column definitions.</param>
	void SortRowCache(DataViewRegistry::CacheMetadata& cache,
				  const DataViewSchema& view,
				  const std::vector<DataQuerySortColumn>& sortColumns) const;

	/// <summary>Apply LIMIT and OFFSET to final cache data.</summary>
	/// <param name="cache">The cache to modify.</param>
	/// <param name="query">Query object containing limit and offset.</param>
	void ApplyLimitOffsetToCache(DataViewRegistry::CacheMetadata& cache, const DataQueryObject& query) const;

	/// <summary>
	/// Convert cache rows to DataCommandValue objects for target stores.
	/// </summary>
	/// <param name="viewRegistry">View registry containing cache data.</param>
	/// <returns>Vector of DataCommandValue updates.</returns>
	std::vector<DataCommandValue> BuildUpdatesFromCache(const DataViewRegistry& viewRegistry) const;

	/// <summary>
	/// Apply all DataCommandValue updates to target stores.
	/// </summary>
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
	std::string MakeGroupKey(const std::vector<uint8_t*>& rowPtrs, const std::vector<size_t>& groupColumns) const
	{
		return "";
	}
};

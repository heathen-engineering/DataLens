/******************************************************************************
 * Schema.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2026-01-27
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

static constexpr const char* DataLensRowFlagsName = "DataLensRowFlags";

enum class DataLensValueType : uint8_t
{
	Bool, // 1 byte
	Int8, // signed 8-bit
	UInt8, // unsigned 8-bit
	Int16, // signed 16-bit
	UInt16, // unsigned 16-bit
	Int32, // signed 32-bit
	UInt32, // unsigned 32-bit
	Int64, // signed 64-bit
	UInt64, // unsigned 64-bit
	Float, // 32-bit IEEE 754
	Double, // 64-bit IEEE 754
	GUID // 128-bit universally unique identifier
};

enum class DataQueryOperator : uint8_t
{
	Equals,
	NotEquals,
	Less,
	LessOrEqual,
	Greater,
	GreaterOrEqual,
	BitmaskHas,
	BitmaskNot,
	Range,
	Not,
	Add,
	Subtract,
	Multiply,
	Divide,
	Modulo,
	And,
	Or,
	Xor,
	Negate,
	LogicalNot,
	Sum,
	Count,
	Min,
	Max
};

enum class DataUpdateType : uint8_t
{
	Add,
	Set,
	Remove
};

enum class DataQueryAggregateType : uint8_t
{
	None = 0,
	Sum,
	SumIf,
	Count,
	Min,
	Max,
	Avg
};

struct DataQueryAggregate
{
	DataQueryAggregateType Type;
	size_t TargetExprIndex; // index into Expressions
	size_t OutputColumnIndex; // index into SelectColumns
};

enum class DataQueryExprType : uint8_t
{
	ColumnRef,
	Constant,
	UnaryOp,
	BinaryOp,
	Aggregate
};

struct DataQueryExpr
{
	DataQueryExprType Type;
	DataLensValueType ResultType;

	// Column reference
	size_t StoreIndex = SIZE_MAX;
	size_t ColumnIndex = SIZE_MAX;

	// Constant
	union
	{
		int64_t IntValue;
		uint64_t UIntValue;
		double DoubleValue;
		float FloatValue;
		bool BoolValue;
		uint64_t BitmaskValue;
	};

	// Unary operator
	DataQueryOperator UnaryOperator;
	size_t OperandIndex = SIZE_MAX;

	// Binary operator
	DataQueryOperator BinaryOperator;
	size_t LeftOperandIndex = SIZE_MAX;
	size_t RightOperandIndex = SIZE_MAX;

	// Aggregate
	DataQueryAggregateType AggType = DataQueryAggregateType::None;
	size_t TargetExprIndex = SIZE_MAX; // for grouping/aggregates
	bool IsGroupLevel = false; // true if evaluated per group
};

struct DataUpdateExpr
{
	DataQueryExprType Type;
	DataLensValueType ResultType;

	// Column reference
	size_t StoreIndex = SIZE_MAX;
	size_t ColumnIndex = SIZE_MAX;

	// Constant
	union
	{
		int64_t IntValue;
		uint64_t UIntValue;
		double DoubleValue;
		float FloatValue;
		bool BoolValue;
		uint64_t BitmaskValue;
	};

	// Unary operator
	DataQueryOperator UnaryOperator;
	size_t OperandIndex = SIZE_MAX;

	// Binary operator
	DataQueryOperator BinaryOperator;
	size_t LeftOperandIndex = SIZE_MAX;
	size_t RightOperandIndex = SIZE_MAX;
};

namespace DataLensValueTypeUtils
{
	// External linkage: defined in DataLensSchema.cpp and used across translation units.
	// (Were declared 'static' originally, which only linked under UE's unity build.)
	size_t GetStride(DataLensValueType type);

	void ConvertData(const void* src, DataLensValueType fromType, void* dst, DataLensValueType toType);

	void Copy(
		const void* src,
		DataLensValueType srcType,
		void* dst,
		DataLensValueType dstType,
		size_t dstStride);

	void Copy(
		const void* src,
		DataLensValueType srcType,
		void* dst,
		DataLensValueType dstType);

	// A2 range-narrowing: smallest byte-aligned integer type that holds a declared range.
	// Unsigned: UInt8/UInt16/UInt32/UInt64. Signed: Int8/Int16/Int32/Int64.
	DataLensValueType SmallestUnsignedForMax(uint64_t maxValue);
	DataLensValueType SmallestSignedForRange(int64_t minValue, int64_t maxValue);
}

struct DataStoreColumnSchema
{
	std::string Name;
	DataLensValueType Type;

	size_t GetStride() const
	{
		return DataLensValueTypeUtils::GetStride(Type);
	}

	bool Optional = false;
	std::vector<uint8_t> DefaultValue; // optional
};

struct DataViewColumnSchema
{
	std::string Name;
	DataLensValueType Type;

	size_t GetStride() const
	{
		return DataLensValueTypeUtils::GetStride(Type);
	}
};

struct DataStoreSchema
{
	std::string Name;
	std::vector<DataStoreColumnSchema> Columns;
	uint64_t DefaultCapacity;
	uint32_t Version = 1;
	
	size_t GetColumnIndex(const std::string& name) const;

	DataStoreSchema() = default;

	DataStoreSchema(
		const std::string& name,
		const std::vector<DataStoreColumnSchema>& columns,
		uint64_t rowCount,
		uint32_t version = 1
	)
		: Name(name)
		, DefaultCapacity(rowCount)
		, Version(version)
	{
		Columns = columns;

		// Ensure row flags column exists as first column
		if (Columns.empty() || Columns[0].Name != DataLensRowFlagsName || Columns[0].Type != DataLensValueType::UInt8)
		{
			DataStoreColumnSchema flagColumn;
			flagColumn.Name = DataLensRowFlagsName;
			flagColumn.Type = DataLensValueType::UInt8;
			flagColumn.Optional = false;
			flagColumn.DefaultValue = { 0 };

			// If columns already have something, insert flag column at front
			if (!Columns.empty())
			{
				Columns.insert(Columns.begin(), flagColumn);
			}
			else
			{
				Columns.push_back(flagColumn);
			}
		}
	}

	size_t GetStride() const;

	bool Validate() const;
};

class DataLensSchema
{
public:
	void AddStore(const DataStoreSchema& store);

	const DataStoreSchema* GetStore(const std::string& name) const;

	bool HasStore(const std::string& name) const;

	// Indexed access
	const DataStoreSchema& operator[](size_t index) const { return mStores.at(index); }
	DataStoreSchema& operator[](size_t index) { return mStores.at(index); }

	// Number of stores
	size_t Count() const;

private:
	std::vector<DataStoreSchema> mStores;
};

struct DataQueryJoin
{
	size_t LeftStoreIndex;
	size_t LeftColumnIndex;
	size_t RightStoreIndex;
	size_t RightColumnIndex;
};

struct DataQueryColumn
{
	bool IsCalculated = false;
	size_t ExprRootIndex = SIZE_MAX; // index into DataQueryObject::Expressions
	size_t StoreIndex = SIZE_MAX; // only for non-calculated columns
	size_t ColumnIndex = SIZE_MAX; // only for non-calculated columns
	DataLensValueType ResultType; // inferred type of column (for cache stride)
};

struct DataUpdateColumn
{
	/// <summary>
	/// SIZE_MAX if target is the view cache
	/// </summary>
	size_t TargetStoreIndex;

	/// <summary>
	/// Index in the target store or view
	/// </summary>
	size_t TargetColumnIndex;

	/// <summary>
	/// Index in the DataView cache to read from (if not constant or expression)
	/// </summary>
	size_t SourceColumnIndex;

	/// <summary>
	/// True if this column should always write the same constant value
	/// </summary>
	bool IsConstant = false;

	/// <summary>
	/// Bytes for constant
	/// </summary>
	std::vector<uint8_t> ConstantValue;

	/// <summary>
	/// Root index of a calculated expression, optional
	/// </summary>
	size_t ExprRootIndex = SIZE_MAX;

	/// <summary>
	/// Result type of this update column (for type conversion)
	/// </summary>
	DataLensValueType ResultType;
};

struct DataQuerySortColumn
{
	size_t StoreIndex;
	size_t ColumnIndex;
	bool Descending = false;
	bool IsExpression = false;
	size_t ExprRootIndex = SIZE_MAX; // if sorting by a calculated column
};

struct DataQueryPredicate
{
	size_t StoreIndex;      // LHS store
	size_t ColumnIndex;     // LHS column
	DataLensValueType ColumnType;

	DataQueryOperator Op;

	// RHS type:
	bool IsRhsColumn = false;    
	size_t RhsStoreIndex = SIZE_MAX;  
	size_t RhsColumnIndex = SIZE_MAX; 

	union
	{
		int64_t IntValue;
		uint64_t UIntValue;
		double DoubleValue;
		float FloatValue;
		uint64_t BitmaskValue;
		bool BoolValue;
	};

	union
	{
		int64_t IntValueHigh;
		uint64_t UIntValueHigh;
		double DoubleValueHigh;
		float FloatValueHigh;
	};

	size_t SubPredicateStartIndex = 0;
	size_t SubPredicateCount = 0;
};

struct DataQueryObject
{
	/// <summary>
	/// Stores involved in query
	/// </summary>
	std::vector<size_t> DataStoreIndices;

	/// <summary>
	/// Predicates (flat array, supports nested via SubPredicateStartIndex/SubPredicateCount)
	/// </summary>
	std::vector<DataQueryPredicate> Predicates;

	/// <summary>
	/// Joins (always equality-based)
	/// </summary>
	std::vector<DataQueryJoin> Joins;

	/// <summary>
	/// Calculated columns support
	/// </summary>
	std::vector<DataQueryExpr> Expressions;

	std::vector<DataQueryAggregate> Aggregates;

	/// <summary>
	/// Columns to return
	/// </summary>
	bool SelectAll = false;
	std::vector<DataQueryColumn> SelectColumns;

	/// <summary>
	/// Sort info
	/// </summary>
	std::vector<DataQuerySortColumn> SortColumns;

	// Limit / Offset
	size_t Limit = SIZE_MAX;
	size_t Offset = 0;

	/// <summary>
	/// Optional grouping/aggregates
	/// </summary>
	std::vector<size_t> GroupByColumns; // indices into SelectColumns
};

struct DataUpdateObject
{
	/// <summary>
	/// Type of this update (Add, Set, Remove)
	/// </summary>
	DataUpdateType Type = DataUpdateType::Set;

	/// <summary>
	/// Which stores are affected
	/// </summary>
	std::vector<size_t> TargetStores;

	/// <summary>
	/// Column mappings (constant, cache column, or expression)
	/// </summary>
	std::vector<DataUpdateColumn> Columns;

	/// <summary>
	/// Predicates to filter affected rows
	/// </summary>
	std::vector<DataQueryPredicate> Predicates;

	/// <summary>
	/// Optional sort, limit, offset for ordered/batched updates
	/// </summary>
	std::vector<DataQuerySortColumn> SortColumns;
	size_t Limit = SIZE_MAX;
	size_t Offset = 0;

	/// <summary>
	/// Expression trees for computed assignments
	/// </summary>
	std::vector<DataUpdateExpr> Expressions;
	
	/// <summary>
	/// Defines the joins
	/// </summary>
	std::vector<DataQueryJoin> SourceJoins;

	/// <summary>
	/// Legacy convenience flags
	/// </summary>
	bool InsertIfNotExists = false;
	bool UpdateIfExists = false;
	bool DeleteIfRemoved = false;
};

struct DataViewSchema
{
	std::string Name;
	std::vector<DataViewColumnSchema> Columns;
	DataQueryObject Query;

	/// <summary>
	/// Optional update object for Inserts
	/// </summary>
	DataUpdateObject Insert;

	/// <summary>
	/// Optional update object for Updates
	/// </summary>
	DataUpdateObject Update;

	/// <summary>
	/// Optional update object for Deletes
	/// </summary>
	DataUpdateObject Delete;
	
	size_t GetColumnIndex(const std::string& name) const;
};

struct QueryResultCache
{
	std::vector<uint8_t> Data; // flat row-major memory
	std::vector<size_t> ColumnOffsets; // per column byte offset
	std::vector<uint8_t*> RowStartPointers; // pointers to each row start
	size_t RowStride = 0; // total bytes per row
};

struct DataViewRegistry
{
	enum class RowState : uint8_t
	{
		Unchanged = 0,
		Modified = 1,
		New = 2,
		Removed = 3
	};

	/// <summary>
	/// Defines the query, update and column layout of the view
	/// </summary>
	DataViewSchema Schema;
	/// <summary>
	/// 0 = disabled, 255 = as fast as you can including faster than 255 times a second, all other values are the Htz or updates a second
	/// </summary>
	uint8_t Frequency = 255;

	/// <summary>
	/// The current data cache
	/// </summary>
	struct CacheMetadata
	{
		/// <summary>
		/// Total bytes per cached row
		/// </summary>
		size_t RowStride = 0;
		/// <summary>
		/// Offset of each column in the row
		/// </summary>
		std::vector<size_t> ColumnOffsets;
		/// <summary>
		/// Pointers to start of each row in DataCache
		/// </summary>
		std::vector<uint8_t*> RowStartPointers;
		/// <summary>
		/// The data blob that is the cashed data
		/// </summary>
		std::vector<uint8_t> Data;
		/// <summary>
		/// Dirty and Remove flags.
		/// index 0 of the inner collection indicates Row Remove flag, if true the row is to be fully removed
		/// if false the system should check each flag as columnIndex+1 indicating if that column in that row has been updated
		/// </summary>
		std::vector<std::vector<RowState>> DirtyRows;
	} Cache;
};

struct DataCommandValue
{
	/// <summary>
	/// The Lens index of the store to be modified
	/// </summary>
	size_t Store;
	/// <summary>
	/// The Store index of the column to be modified
	/// </summary>
	size_t Column;
	/// <summary>
	/// The Store index of the record to be modified.
	/// If this is SIZE_MAX then this is a new record
	/// </summary>
	size_t Record;
	/// <summary>
	/// The raw bytes to be applied this must be in the correct native pattern for the DataStore.
	/// Use
	/// </summary>
	std::vector<uint8_t> Value;
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
		if constexpr (!NeedSwap)
		{
			return val;
		}
		return (val >> 24) |
			((val >> 8) & 0x0000FF00) |
			((val << 8) & 0x00FF0000) |
			(val << 24);
	}

	inline uint32_t FromLittle(uint32_t val) { return ToLittle(val); }

	inline uint64_t ToLittle(uint64_t val)
	{
		if constexpr (!NeedSwap)
		{
			return val;
		}
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

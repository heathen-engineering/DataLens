/******************************************************************************
 * DataStore.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2026-01-29
 ******************************************************************************/

#pragma once

#include <cstring>
#include "datalens/DataLensSchema.h"

/// <summary>Arithmetic a System applies to a target column cell (cur OP operand).</summary>
enum class DataSystemOp : int32_t
{
	Set = 0,
	Add = 1,
	Sub = 2,
	Mul = 3,
	Min = 4,
	Max = 5,
};

/// <summary>Comparison a System predicate applies (compareCell CMP threshold).</summary>
enum class DataCompareOp : int32_t
{
	Always       = 0,
	Equal        = 1,
	NotEqual     = 2,
	Less         = 3,
	LessEqual    = 4,
	Greater      = 5,
	GreaterEqual = 6,
};

/// <summary>
/// DataStore provides a column-oriented, in-memory table of raw bytes with
/// flexible per-column stride, supporting both unchecked raw access and safe
/// bounds-checked operations. Useful as a low-level data container for
/// structured, high-performance storage and serialization.
/// </summary>
class DataStore
{
	friend class DataLens;
public:
	DataStore() = default;

	/// <summary>
	/// Initialise with column metadata and preallocate rows.
	/// </summary>
	/// <param name="columns"></param>
	/// <param name="preallocRows"></param>
	DataStore(const std::vector<DataStoreColumnSchema>& columns, size_t preallocRows);

	/// <summary>
	/// Initialise with column metadata and pre-load data from a byte array (row-major layout).
	/// </summary>
	/// <param name="columns"></param>
	/// <param name="data"></param>
	DataStore(const std::vector<DataStoreColumnSchema>& columns, const std::vector<uint8_t>& data);

	/// <summary>
	/// Initialise with column metadata, pre-load data, and optionally preallocate extra rows.
	/// </summary>
	/// <param name="columns"></param>
	/// <param name="data"></param>
	/// <param name="extraRows"></param>
	DataStore(const std::vector<DataStoreColumnSchema>& columns, const std::vector<uint8_t>& data, size_t extraRows);

	/// <summary>
	/// Get a typed value from a cell (unchecked).
	/// </summary>
	/// <typeparam name="T"></typeparam>
	/// <param name="row"></param>
	/// <param name="col"></param>
	/// <returns></returns>
	template <typename T>
	T GetRaw(size_t row, size_t col) const
	{
		T value;
		std::memcpy(&value, GetRawCell(row, col), sizeof(T));
		return value;
	}

	/// <summary>
	/// Set a typed value to a cell (unchecked).
	/// </summary>
	/// <typeparam name="T"></typeparam>
	/// <param name="row"></param>
	/// <param name="col"></param>
	/// <param name="value"></param>
	template <typename T>
	void SetRaw(size_t row, size_t col, const T& value)
	{
		std::memcpy(GetRawCellMutable(row, col), &value, sizeof(T));
	}

	/// <summary>
	/// Try to read a cell safely with stride-awareness.
	/// </summary>
	/// <typeparam name="T"></typeparam>
	/// <param name="row"></param>
	/// <param name="col"></param>
	/// <param name="outValue"></param>
	/// <returns></returns>
	template <typename T>
	bool TryGet(size_t row, size_t col, T& outValue) const
	{
		if (!IsValid(row, col))
		{
			return false;
		}
		outValue = T{};
		size_t copySize = std::min(sizeof(T), mColumns[col].GetStride());
		std::memcpy(&outValue, GetRawCell(row, col), copySize);
		return true;
	}

	/// <summary>
	/// Try to write a cell safely with stride-awareness.
	/// </summary>
	/// <typeparam name="T"></typeparam>
	/// <param name="row"></param>
	/// <param name="col"></param>
	/// <param name="value"></param>
	/// <returns></returns>
	template <typename T>
	bool TrySet(size_t row, size_t col, const T& value)
	{
		if (!IsValid(row, col))
		{
			return false;
		}
		size_t copySize = std::min(sizeof(T), mColumns[col].GetStride());
		std::memcpy(GetRawCellMutable(row, col), &value, copySize);
		return true;
	}

	/// <summary>
	/// Load raw row-major data into the datastore.
	/// </summary>
	/// <param name="src"></param>
	void LoadRaw(const std::vector<uint8_t>& src);

	/// <summary>
	/// Dump the current table data in row-major layout.
	/// </summary>
	/// <returns></returns>
	std::vector<uint8_t> Dump() const;

	size_t GetRowCount() const;
	size_t GetColumnCount() const;
	size_t GetColumnStride(size_t col) const;
	
	bool IsValidRow(size_t row) const;
	bool IsLockedRow(size_t row) const;
	void SetValid(size_t row, bool valid);
	void SetLocked(size_t row, bool locked);

	/// <summary>
	/// Allocate the next free (invalid) row slot, mark it valid, and return its index.
	/// O(1) amortised via a dense validity bitmask + bit-scan. Fixed capacity: returns
	/// SIZE_MAX when no free slot remains (the store does not grow).
	/// </summary>
	size_t AllocRow();

	/// <summary>
	/// Release a row (mark invalid + clear its locked bit) so a later AllocRow can reuse it.
	/// </summary>
	void FreeRow(size_t row);

	/// <summary>Number of currently-valid (live) rows.</summary>
	size_t GetLiveCount() const;

	/// <summary>
	/// Run a data-described System over this store: for every live row that satisfies the
	/// optional predicate (compareCol CMP threshold), apply (targetCol = targetCol OP operand).
	/// Linear column scan with a per-row select write-back (always writes, value chosen
	/// branchlessly), so it stays vectorisable. Target and predicate columns are interpreted as
	/// type T. Returns the number of rows affected.
	/// </summary>
	template <typename T>
	size_t RunColumnSystem(size_t targetCol, DataSystemOp op, T operand,
	                       bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		if (targetCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;

		uint8_t* tcol = mColumnsData[targetCol].data();
		const size_t tstride = mColumns[targetCol].GetStride();
		const uint8_t* pcol = hasPredicate ? mColumnsData[compareCol].data() : nullptr;
		const size_t pstride = hasPredicate ? mColumns[compareCol].GetStride() : 0;

		size_t affected = 0;
		for (size_t r = 0; r < mRowCount; ++r)
		{
			const bool live = (mValidBits[r >> 6] >> (r & 63)) & 1ULL;

			T cur;
			std::memcpy(&cur, tcol + r * tstride, sizeof(T));

			T computed;
			switch (op)
			{
			case DataSystemOp::Set: computed = operand; break;
			case DataSystemOp::Add: computed = static_cast<T>(cur + operand); break;
			case DataSystemOp::Sub: computed = static_cast<T>(cur - operand); break;
			case DataSystemOp::Mul: computed = static_cast<T>(cur * operand); break;
			case DataSystemOp::Min: computed = cur < operand ? cur : operand; break;
			case DataSystemOp::Max: computed = cur > operand ? cur : operand; break;
			default: computed = cur; break;
			}

			bool match = true;
			if (hasPredicate)
			{
				T pv;
				std::memcpy(&pv, pcol + r * pstride, sizeof(T));
				switch (cmp)
				{
				case DataCompareOp::Always:       match = true; break;
				case DataCompareOp::Equal:        match = (pv == threshold); break;
				case DataCompareOp::NotEqual:     match = (pv != threshold); break;
				case DataCompareOp::Less:         match = (pv <  threshold); break;
				case DataCompareOp::LessEqual:    match = (pv <= threshold); break;
				case DataCompareOp::Greater:      match = (pv >  threshold); break;
				case DataCompareOp::GreaterEqual: match = (pv >= threshold); break;
				default: match = true; break;
				}
			}

			const bool apply = live && match;
			const T result = apply ? computed : cur; // select write-back (lowers to cmov)
			std::memcpy(tcol + r * tstride, &result, sizeof(T));
			affected += apply ? 1u : 0u;
		}
		return affected;
	}

	/// <summary>
	/// Total bytes per row (sum of all column strides).
	/// </summary>
	/// <returns></returns>
	size_t GetRowStride() const;
	void ConvertToSchema(const DataStoreSchema& newSchema);
	bool CompareCells(size_t rowA, size_t columnA, const DataStore& other, size_t rowB, size_t columnB) const;
	void CopyCellToFlatRow(size_t rowIndex, size_t columnIndex, void* dst) const;
	bool MatchesPredicate(size_t rowIndex, const DataQueryPredicate& pred) const;

private:
	std::vector<DataStoreColumnSchema> mColumns;
	std::vector<std::vector<uint8_t>> mColumnsData; // column-major storage
	size_t mRowCount{0};

	// Row liveness/locking live in dense bitmasks (1 bit/row), separate from column data, so
	// validity never overlaps real column bytes and can be bit-scanned/SIMD-tested in bulk.
	std::vector<uint64_t> mValidBits;   // authoritative row validity
	std::vector<uint64_t> mLockedBits;  // row locked/pinned
	size_t mLiveCount{0};
	size_t mAllocCursor{0};             // word-index hint for the next AllocRow scan

	/// <summary>
	/// Get a pointer to a raw cell (unchecked).
	/// </summary>
	/// <param name="row"></param>
	/// <param name="col"></param>
	/// <returns></returns>
	uint8_t* GetRawCellMutable(size_t row, size_t col);

	/// <summary>
	/// Get a pointer to a raw cell (unchecked).
	/// </summary>
	/// <param name="row"></param>
	/// <param name="col"></param>
	/// <returns></returns>
	const uint8_t* GetRawCell(size_t row, size_t col) const;

	void InitializeColumns(size_t rows);

	void LoadDataFromRowMajor(const std::vector<uint8_t>& src, size_t rows);

	bool IsValid(size_t row, size_t col) const;

	const void* GetCellPointer(size_t rowIndex, size_t columnIndex) const;
};

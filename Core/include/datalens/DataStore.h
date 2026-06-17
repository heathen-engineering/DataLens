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
#include <cstdint>
#include <type_traits>
#include "datalens/AlignedAllocator.h"
#include "datalens/DataLensSchema.h"

/// <summary>Arithmetic a System applies to a target column cell (cur OP operand). The bitwise ops
/// (And/Or/Xor/AndNot) are integer-only; on a floating-point column they are a no-op.</summary>
enum class DataSystemOp : int32_t
{
	Set    = 0,
	Add    = 1,
	Sub    = 2,
	Mul    = 3,
	Min    = 4,
	Max    = 5,
	And    = 6, // cur & operand   (mask bits)
	Or     = 7, // cur | operand   (set bits)
	Xor    = 8, // cur ^ operand   (toggle bits)
	AndNot = 9, // cur & ~operand  (clear bits)
};

/// <summary>Comparison a System predicate applies (compareCell CMP threshold). The bitmask compares
/// (HasAllBits/HasAnyBits/LacksBits) are integer-only; on a floating-point column they never match.</summary>
enum class DataCompareOp : int32_t
{
	Always       = 0,
	Equal        = 1,
	NotEqual     = 2,
	Less         = 3,
	LessEqual    = 4,
	Greater      = 5,
	GreaterEqual = 6,
	HasAllBits   = 7, // (cell & threshold) == threshold
	HasAnyBits   = 8, // (cell & threshold) != 0
	LacksBits    = 9, // (cell & threshold) == 0
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

	/// <summary>
	/// Const pointer to the start of a column's contiguous (cache-line-aligned) byte buffer; cell
	/// `row` is at `GetColumnRaw(col) + row * GetColumnStride(col)`. For bulk readers that copy raw
	/// bytes without per-cell type dispatch (e.g. DataView gather). Returns nullptr if out of range.
	/// </summary>
	const uint8_t* GetColumnRaw(size_t col) const;

	/// <summary>Cache line the column buffers are aligned to (so concurrent Systems writing
	/// different columns never false-share). See <see cref="IsColumnCacheAligned"/>.</summary>
	static constexpr size_t CacheLineSize() { return 64; }

	/// <summary>True if the column's data buffer starts on a cache-line boundary (diagnostic).</summary>
	bool IsColumnCacheAligned(size_t col) const;
	
	bool IsValidRow(size_t row) const;
	bool IsLockedRow(size_t row) const;
	void SetValid(size_t row, bool valid);
	void SetLocked(size_t row, bool locked);

	/// <summary>
	/// Set a row's Simulation LOD level (0 = highest fidelity / runs always; higher = coarser /
	/// less relevant). LOD is per-row metadata held in a dense byte array, separate from column data
	/// (like the validity bitmask), so a System can scope its work to a band of relevance.
	/// </summary>
	void SetLod(size_t row, uint8_t level);

	/// <summary>Get a row's Simulation LOD level (0 if out of range).</summary>
	uint8_t GetLod(size_t row) const;

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
		return RunColumnSystemChunk<T>(0, mRowCount, targetCol, op, operand,
		                               hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>
	/// Run a System over a disjoint row sub-range [rowBegin, rowEnd). Safe to call concurrently
	/// from multiple threads on non-overlapping ranges (it writes only its own rows and reads
	/// shared validity/predicate state). The Lens uses this to chunk a System across its threads.
	/// </summary>
	template <typename T>
	size_t RunColumnSystemChunk(size_t rowBegin, size_t rowEnd,
	                            size_t targetCol, DataSystemOp op, T operand,
	                            bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		if (targetCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunColumnSystemChunkImpl<T, false, false>(rowBegin, rowEnd, targetCol, op, operand, 0,
		                                                 hasPredicate, compareCol, cmp, threshold, 0, 255);
	}

	/// <summary>
	/// Cross-column System: like <see cref="RunColumnSystem"/> but the operand for each row is read
	/// from <paramref name="operandCol"/> instead of being a scalar constant — e.g.
	/// (targetCol = targetCol + bonusCol) or a per-row clamp (current = min(current, maxCol)).
	/// This is the substrate for attribute aggregation in HATE. Target, operand and predicate columns
	/// are all interpreted as type T. Returns the number of rows affected.
	/// </summary>
	template <typename T>
	size_t RunColumnSystemColumn(size_t targetCol, DataSystemOp op, size_t operandCol,
	                             bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		return RunColumnSystemColumnChunk<T>(0, mRowCount, targetCol, op, operandCol,
		                                     hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>
	/// Disjoint sub-range [rowBegin, rowEnd) form of <see cref="RunColumnSystemColumn"/>, used by the
	/// Lens to chunk a cross-column System across its threads (each chunk writes only its own rows).
	/// </summary>
	template <typename T>
	size_t RunColumnSystemColumnChunk(size_t rowBegin, size_t rowEnd,
	                                  size_t targetCol, DataSystemOp op, size_t operandCol,
	                                  bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		if (targetCol >= mColumns.size() || operandCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunColumnSystemChunkImpl<T, true, false>(rowBegin, rowEnd, targetCol, op, T{}, operandCol,
		                                                hasPredicate, compareCol, cmp, threshold, 0, 255);
	}

	/// <summary>
	/// LOD-banded scalar System: like <see cref="RunColumnSystem"/> but only rows whose Simulation LOD
	/// is within [minLod, maxLod] are affected. The data-side primitive behind Simulation LOD — a
	/// System scopes its work to a relevance band (see <see cref="SetLod"/>).
	/// </summary>
	template <typename T>
	size_t RunColumnSystemInLodBand(size_t targetCol, DataSystemOp op, T operand,
	                                bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold,
	                                uint8_t minLod, uint8_t maxLod)
	{
		return RunColumnSystemInLodBandChunk<T>(0, mRowCount, targetCol, op, operand,
		                                        hasPredicate, compareCol, cmp, threshold, minLod, maxLod);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunColumnSystemInLodBand"/> (used by the Lens).</summary>
	template <typename T>
	size_t RunColumnSystemInLodBandChunk(size_t rowBegin, size_t rowEnd,
	                                     size_t targetCol, DataSystemOp op, T operand,
	                                     bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold,
	                                     uint8_t minLod, uint8_t maxLod)
	{
		if (targetCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunColumnSystemChunkImpl<T, false, true>(rowBegin, rowEnd, targetCol, op, operand, 0,
		                                                hasPredicate, compareCol, cmp, threshold, minLod, maxLod);
	}

	/// <summary>LOD-banded cross-column System: <see cref="RunColumnSystemColumn"/> scoped to [minLod, maxLod].</summary>
	template <typename T>
	size_t RunColumnSystemColumnInLodBand(size_t targetCol, DataSystemOp op, size_t operandCol,
	                                      bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold,
	                                      uint8_t minLod, uint8_t maxLod)
	{
		return RunColumnSystemColumnInLodBandChunk<T>(0, mRowCount, targetCol, op, operandCol,
		                                              hasPredicate, compareCol, cmp, threshold, minLod, maxLod);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunColumnSystemColumnInLodBand"/> (used by the Lens).</summary>
	template <typename T>
	size_t RunColumnSystemColumnInLodBandChunk(size_t rowBegin, size_t rowEnd,
	                                           size_t targetCol, DataSystemOp op, size_t operandCol,
	                                           bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold,
	                                           uint8_t minLod, uint8_t maxLod)
	{
		if (targetCol >= mColumns.size() || operandCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunColumnSystemChunkImpl<T, true, true>(rowBegin, rowEnd, targetCol, op, T{}, operandCol,
		                                               hasPredicate, compareCol, cmp, threshold, minLod, maxLod);
	}

	/// <summary>
	/// Scaled cross-column System (fused multiply): the per-row operand is `operandCol[r] * scale`
	/// before applying `targetCol = targetCol OP (operandCol * scale)`. This is the integration /
	/// scaled-modifier primitive — e.g. `pos += vel * dt` (Add) or `effective += base * mult`. Reduces
	/// what would be a multiply-then-combine to a single branchless pass. Returns rows affected.
	/// </summary>
	template <typename T>
	size_t RunColumnSystemScaledColumn(size_t targetCol, DataSystemOp op, size_t operandCol, T scale,
	                                   bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		return RunColumnSystemScaledColumnChunk<T>(0, mRowCount, targetCol, op, operandCol, scale,
		                                           hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunColumnSystemScaledColumn"/> (used by the Lens).</summary>
	template <typename T>
	size_t RunColumnSystemScaledColumnChunk(size_t rowBegin, size_t rowEnd,
	                                        size_t targetCol, DataSystemOp op, size_t operandCol, T scale,
	                                        bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		if (targetCol >= mColumns.size() || operandCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		// scale rides in the `operand` parameter (read as `scale` inside the kernel).
		return RunColumnSystemChunkImpl<T, true, false, true>(rowBegin, rowEnd, targetCol, op, scale, operandCol,
		                                                      hasPredicate, compareCol, cmp, threshold, 0, 255);
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
	/// <summary>
	/// Shared kernel for both the scalar (<see cref="RunColumnSystemChunk"/>) and cross-column
	/// (<see cref="RunColumnSystemColumnChunk"/>) Systems. <c>OperandIsColumn</c> is a compile-time
	/// switch: when false the operand is the scalar <paramref name="operand"/>; when true it is read
	/// per-row from <paramref name="operandCol"/>. The dead branch is eliminated at compile time, so
	/// the hot loop carries no per-row branch on the operand source and stays vectorisable.
	/// Bounds are validated by the public callers before this runs.
	/// </summary>
	template <typename T, bool OperandIsColumn, bool UseLod, bool ScaleOperand = false>
	size_t RunColumnSystemChunkImpl(size_t rowBegin, size_t rowEnd,
	                                size_t targetCol, DataSystemOp op, T operand, size_t operandCol,
	                                bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold,
	                                uint8_t minLod, uint8_t maxLod)
	{
		// ScaleOperand only applies in cross-column mode: rhs = operandCol[r] * scale (operand holds
		// the scalar scale). This is the fused-multiply primitive (e.g. pos += vel * dt).
		static_assert(!ScaleOperand || OperandIsColumn, "ScaleOperand requires OperandIsColumn");
		const T scale = operand;
		uint8_t* tcol = mColumnsData[targetCol].data();
		const size_t tstride = mColumns[targetCol].GetStride();
		const uint8_t* ocol = OperandIsColumn ? mColumnsData[operandCol].data() : nullptr;
		const size_t ostride = OperandIsColumn ? mColumns[operandCol].GetStride() : 0;
		const uint8_t* pcol = hasPredicate ? mColumnsData[compareCol].data() : nullptr;
		const size_t pstride = hasPredicate ? mColumns[compareCol].GetStride() : 0;
		const uint8_t* lod = UseLod ? mLodLevels.data() : nullptr;

		size_t affected = 0;
		for (size_t r = rowBegin; r < rowEnd; ++r)
		{
			bool live = (mValidBits[r >> 6] >> (r & 63)) & 1ULL;
			if (UseLod)
			{
				const uint8_t lv = lod[r];
				live = live && (lv >= minLod) && (lv <= maxLod);
			}

			T cur;
			std::memcpy(&cur, tcol + r * tstride, sizeof(T));

			T rhs;
			if (OperandIsColumn)
			{
				std::memcpy(&rhs, ocol + r * ostride, sizeof(T));
				if (ScaleOperand)
					rhs = static_cast<T>(rhs * scale);
			}
			else
				rhs = operand;

			T computed;
			switch (op)
			{
			case DataSystemOp::Set: computed = rhs; break;
			case DataSystemOp::Add: computed = static_cast<T>(cur + rhs); break;
			case DataSystemOp::Sub: computed = static_cast<T>(cur - rhs); break;
			case DataSystemOp::Mul: computed = static_cast<T>(cur * rhs); break;
			case DataSystemOp::Min: computed = cur < rhs ? cur : rhs; break;
			case DataSystemOp::Max: computed = cur > rhs ? cur : rhs; break;
			case DataSystemOp::And:
			case DataSystemOp::Or:
			case DataSystemOp::Xor:
			case DataSystemOp::AndNot:
				if constexpr (std::is_integral<T>::value)
				{
					switch (op)
					{
					case DataSystemOp::And:    computed = static_cast<T>(cur & rhs); break;
					case DataSystemOp::Or:     computed = static_cast<T>(cur | rhs); break;
					case DataSystemOp::Xor:    computed = static_cast<T>(cur ^ rhs); break;
					case DataSystemOp::AndNot: computed = static_cast<T>(cur & ~rhs); break;
					default:                   computed = cur; break;
					}
				}
				else
				{
					computed = cur; // bitwise ops are undefined on floating-point columns: no-op
				}
				break;
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
				case DataCompareOp::HasAllBits:
				case DataCompareOp::HasAnyBits:
				case DataCompareOp::LacksBits:
					if constexpr (std::is_integral<T>::value)
					{
						switch (cmp)
						{
						case DataCompareOp::HasAllBits: match = ((pv & threshold) == threshold); break;
						case DataCompareOp::HasAnyBits: match = ((pv & threshold) != T{0}); break;
						case DataCompareOp::LacksBits:  match = ((pv & threshold) == T{0}); break;
						default:                        match = true; break;
						}
					}
					else
					{
						match = false; // bitmask compares never match on floating-point columns
					}
					break;
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

	// Each column buffer is allocated cache-line-aligned (64B) so that two Systems writing different
	// columns concurrently can never false-share a line. Distinct 64-aligned, non-overlapping
	// allocations never occupy the same cache line, so aligning the start is sufficient.
	using ColumnBuffer = std::vector<uint8_t, datalens::AlignedAllocator<uint8_t, 64>>;

	std::vector<DataStoreColumnSchema> mColumns;
	std::vector<ColumnBuffer> mColumnsData; // column-major storage, each column cache-line-aligned
	size_t mRowCount{0};

	// Row liveness/locking live in dense bitmasks (1 bit/row), separate from column data, so
	// validity never overlaps real column bytes and can be bit-scanned/SIMD-tested in bulk.
	std::vector<uint64_t> mValidBits;   // authoritative row validity
	std::vector<uint64_t> mLockedBits;  // row locked/pinned
	std::vector<uint8_t>  mLodLevels;   // per-row Simulation LOD (0 = highest fidelity)
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

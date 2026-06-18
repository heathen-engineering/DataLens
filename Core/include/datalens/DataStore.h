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

/// <summary>A pass-level response curve x->y in [0,1] applied to a System's per-row operand before it
/// is combined into the target (DataLens-Spec A3.11). The curve and its parameters are uniform for the
/// whole pass (no per-row branch or table lookup), so a "consideration" stays the contiguous, branchless
/// RunColumnSystem shape. v1 curves are transcendental-free.</summary>
enum class DataCurveType : int32_t
{
	Linear     = 0, // y = p0 * x + p1            (slope, intercept)
	Power      = 1, // y = x ^ (int)p0            (repeated multiply; p0 = integer exponent, clamped 0..16)
	Smoothstep = 2, // y = x*x*(3 - 2x)
	Threshold  = 3, // y = x >= p0 ? 1 : 0        (step at p0)
};

/// <summary>Pass-level parameters for <see cref="DataCurveType"/>: the input normalise range [min,max]
/// (raw operand -> x in [0,1], clamped), two curve params, and an invert flag (y -> 1 - y). The default
/// is the identity (Linear y=x over [0,1]).</summary>
struct CurveSpec
{
	DataCurveType type = DataCurveType::Linear;
	float min = 0.0f, max = 1.0f; // normalise raw operand: x = clamp01((raw - min)/(max - min))
	float p0 = 1.0f, p1 = 0.0f;   // curve params (see DataCurveType)
	bool invert = false;          // y -> 1 - y after evaluation/clamp
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

	/// <summary>Sentinel for a System's predicate type meaning "same type as the operand/target" — the
	/// default. A different value (Int32/Float) selects a mixed-type predicate (see the System kernel).</summary>
	static constexpr DataLensValueType kSameType = static_cast<DataLensValueType>(-1);

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
	/// Scalar System with a MIXED-TYPE predicate (A "trigger gate"): apply (targetCol = targetCol OP
	/// operand) to every live row where the predicate column — interpreted as <paramref name="predType"/>
	/// — satisfies (cmp <paramref name="predThreshold"/>). Lets a float-attribute effect be gated by an
	/// int tag-bitmask column (predType = Int32, cmp = HasAllBits/HasAnyBits/LacksBits) in one branchless
	/// pass. Returns the number of rows affected.
	/// </summary>
	template <typename T>
	size_t RunColumnSystemTypedPred(size_t targetCol, DataSystemOp op, T operand,
	                                size_t compareCol, DataCompareOp cmp,
	                                DataLensValueType predType, double predThreshold)
	{
		return RunColumnSystemTypedPredChunk<T>(0, mRowCount, targetCol, op, operand,
		                                        compareCol, cmp, predType, predThreshold);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunColumnSystemTypedPred"/> (used by the Lens).</summary>
	template <typename T>
	size_t RunColumnSystemTypedPredChunk(size_t rowBegin, size_t rowEnd,
	                                     size_t targetCol, DataSystemOp op, T operand,
	                                     size_t compareCol, DataCompareOp cmp,
	                                     DataLensValueType predType, double predThreshold)
	{
		if (targetCol >= mColumns.size() || compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunColumnSystemChunkImpl<T, false, false>(rowBegin, rowEnd, targetCol, op, operand, 0,
		                                                 true, compareCol, cmp, T{}, 0, 255,
		                                                 predType, predThreshold);
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
	/// Curved cross-column System (DataLens-Spec A3.11): the per-row operand read from <paramref name="operandCol"/>
	/// is passed through the pass-level response <paramref name="curve"/> (normalise to [0,1] -> curve ->
	/// [0,1]) before applying `targetCol = targetCol OP curve(operandCol)`. This is the HATE §8 considerations
	/// primitive: one consideration = one contiguous, branchless pass folding a curved metric into a score
	/// accumulator (combine with Mul for product aggregation, Add for weighted-sum). Returns rows affected.
	/// </summary>
	template <typename T>
	size_t RunColumnSystemCurvedColumn(size_t targetCol, DataSystemOp op, size_t operandCol, const CurveSpec& curve,
	                                   bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		return RunColumnSystemCurvedColumnChunk<T>(0, mRowCount, targetCol, op, operandCol, curve,
		                                           hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunColumnSystemCurvedColumn"/> (used by the Lens).</summary>
	template <typename T>
	size_t RunColumnSystemCurvedColumnChunk(size_t rowBegin, size_t rowEnd,
	                                        size_t targetCol, DataSystemOp op, size_t operandCol, const CurveSpec& curve,
	                                        bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		if (targetCol >= mColumns.size() || operandCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunColumnSystemChunkImpl<T, true, false, false, true>(rowBegin, rowEnd, targetCol, op, T{}, operandCol,
		                                                             hasPredicate, compareCol, cmp, threshold, 0, 255,
		                                                             kSameType, 0.0, curve);
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
	/// <summary>Evaluate one predicate comparison in the predicate column's own type P. Bitmask ops
	/// (HasAllBits/HasAnyBits/LacksBits) are integer-only (false on floating-point). Shared by the
	/// same-type and mixed-type predicate paths.</summary>
	/// <summary>Evaluate a pass-level response curve at a raw operand value (DataLens-Spec A3.11). The raw
	/// value is normalised to x in [0,1] over [c.min, c.max] (clamped), the curve is applied, the result
	/// clamped to [0,1], then optionally inverted. Transcendental-free (Power uses repeated multiply).
	/// Uniform per pass: every row in a System pass uses the same CurveSpec, so there is no per-row branch
	/// on curve type beyond a loop-invariant switch.</summary>
	static float EvalCurve(float raw, const CurveSpec& c)
	{
		const float denom = c.max - c.min;
		float x = denom > 0.0f ? (raw - c.min) / denom : 0.0f;
		x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);

		float y;
		switch (c.type)
		{
		case DataCurveType::Linear:     y = c.p0 * x + c.p1; break;
		case DataCurveType::Power:
		{
			int k = static_cast<int>(c.p0);
			if (k < 0) k = 0; else if (k > 16) k = 16; // bounded, transcendental-free
			y = 1.0f;
			for (int i = 0; i < k; ++i) y *= x; // x^k (k=0 -> constant 1)
			break;
		}
		case DataCurveType::Smoothstep: y = x * x * (3.0f - 2.0f * x); break;
		case DataCurveType::Threshold:  y = (x >= c.p0) ? 1.0f : 0.0f; break;
		default:                        y = x; break;
		}

		y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
		if (c.invert) y = 1.0f - y;
		return y;
	}

	template <typename P>
	static bool ComparePredicate(P pv, DataCompareOp cmp, P threshold)
	{
		switch (cmp)
		{
		case DataCompareOp::Always:       return true;
		case DataCompareOp::Equal:        return pv == threshold;
		case DataCompareOp::NotEqual:     return pv != threshold;
		case DataCompareOp::Less:         return pv <  threshold;
		case DataCompareOp::LessEqual:    return pv <= threshold;
		case DataCompareOp::Greater:      return pv >  threshold;
		case DataCompareOp::GreaterEqual: return pv >= threshold;
		case DataCompareOp::HasAllBits:
		case DataCompareOp::HasAnyBits:
		case DataCompareOp::LacksBits:
			if constexpr (std::is_integral<P>::value)
			{
				switch (cmp)
				{
				case DataCompareOp::HasAllBits: return (pv & threshold) == threshold;
				case DataCompareOp::HasAnyBits: return (pv & threshold) != P{0};
				case DataCompareOp::LacksBits:  return (pv & threshold) == P{0};
				default:                        return true;
				}
			}
			else
			{
				return false; // bitmask compares never match on floating-point columns
			}
		default: return true;
		}
	}

	/// <summary>
	/// Shared System kernel. The PREDICATE may be a different type than the operand/target type T
	/// (A "mixed-type predicate"): when <paramref name="predType"/> is <see cref="kSameType"/> the
	/// predicate is read as T with the T <paramref name="threshold"/>; otherwise it is read in
	/// <paramref name="predType"/> (Int32/Float) against <paramref name="predThresholdRaw"/>. This lets,
	/// e.g., a float-attribute effect be gated by an int tag-bitmask column in one branchless pass.
	/// </summary>
	template <typename T, bool OperandIsColumn, bool UseLod, bool ScaleOperand = false, bool ApplyCurve = false>
	size_t RunColumnSystemChunkImpl(size_t rowBegin, size_t rowEnd,
	                                size_t targetCol, DataSystemOp op, T operand, size_t operandCol,
	                                bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold,
	                                uint8_t minLod, uint8_t maxLod,
	                                DataLensValueType predType = kSameType, double predThresholdRaw = 0.0,
	                                const CurveSpec& curve = CurveSpec{})
	{
		// ScaleOperand only applies in cross-column mode: rhs = operandCol[r] * scale (operand holds
		// the scalar scale). This is the fused-multiply primitive (e.g. pos += vel * dt).
		static_assert(!ScaleOperand || OperandIsColumn, "ScaleOperand requires OperandIsColumn");
		// ApplyCurve transforms the per-row operand (a metric) through a pass-level response curve before
		// the combine — the considerations primitive (A3.11). It reads the operand from a column.
		static_assert(!ApplyCurve || OperandIsColumn, "ApplyCurve requires OperandIsColumn");
		static_assert(!(ApplyCurve && ScaleOperand), "ApplyCurve and ScaleOperand are mutually exclusive");
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
				if (ApplyCurve)
					rhs = static_cast<T>(EvalCurve(static_cast<float>(rhs), curve));
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
				// predType resolves at compile-time-invariant cost (loop-invariant branch). Same-type is
				// the common path; the int/float cases enable a predicate column of a different type.
				if (predType == kSameType)
				{
					T pv;
					std::memcpy(&pv, pcol + r * pstride, sizeof(T));
					match = ComparePredicate<T>(pv, cmp, threshold);
				}
				else if (predType == DataLensValueType::Int32)
				{
					int32_t pv;
					std::memcpy(&pv, pcol + r * pstride, sizeof(int32_t));
					match = ComparePredicate<int32_t>(pv, cmp, static_cast<int32_t>(predThresholdRaw));
				}
				else if (predType == DataLensValueType::Float)
				{
					float pv;
					std::memcpy(&pv, pcol + r * pstride, sizeof(float));
					match = ComparePredicate<float>(pv, cmp, static_cast<float>(predThresholdRaw));
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

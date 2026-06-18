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
	/// Counter-based noise fill (DataLens-Spec A3.12): each live row gets `targetCol = targetCol OP noise`,
	/// where `noise = noiseLo + (noiseHi-noiseLo) * u01(row, tick, seed)` and `u01` is a STATELESS
	/// counter-based PRNG (no global RNG). Because the value depends only on the global row index, the
	/// tick and the seed, results are reproducible across runs, machines and replay (and identical whether
	/// run serially or chunked across the Lens pool). The perturb term of HATE-Spec §8.4: with op Set this
	/// fills a noise column; with op Add it jitters an accumulator (`Score += noise`). Returns rows affected.
	/// </summary>
	template <typename T>
	size_t RunNoiseColumn(size_t targetCol, DataSystemOp op, T noiseLo, T noiseHi, uint64_t seed, uint64_t tick,
	                      bool hasPredicate = false, size_t compareCol = 0,
	                      DataCompareOp cmp = DataCompareOp::Always, T threshold = T{})
	{
		return RunNoiseColumnChunk<T>(0, mRowCount, targetCol, op, noiseLo, noiseHi, seed, tick,
		                              hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunNoiseColumn"/> (used by the Lens). The noise per
	/// row is keyed on the GLOBAL row index, so any [rowBegin,rowEnd) chunking yields identical results.</summary>
	template <typename T>
	size_t RunNoiseColumnChunk(size_t rowBegin, size_t rowEnd, size_t targetCol, DataSystemOp op,
	                           T noiseLo, T noiseHi, uint64_t seed, uint64_t tick,
	                           bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		if (targetCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunNoiseColumnChunkImpl<T, false>(rowBegin, rowEnd, targetCol, op, 0, noiseLo, noiseHi, seed, tick,
		                                         hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>
	/// Counter-based noise PERTURB (DataLens-Spec A3.12 / HATE-Spec §8.4): `targetCol = targetCol OP
	/// (operandCol[r] * noise)`, with `noise = noiseLo + (noiseHi-noiseLo) * u01(row, tick, seed)`. This is
	/// the headline perturb `Score' = Score + Variance * Noise` in one branchless pass when operandCol is
	/// the per-actor Variance column and op is Add: rows whose Variance is 0 are unchanged (perfect play),
	/// higher Variance is increasingly stochastic. Same stateless, reproducible, chunk-invariant PRNG as
	/// <see cref="RunNoiseColumn"/>. Returns rows affected.
	/// </summary>
	template <typename T>
	size_t RunNoisePerturbColumn(size_t targetCol, DataSystemOp op, size_t operandCol,
	                             T noiseLo, T noiseHi, uint64_t seed, uint64_t tick,
	                             bool hasPredicate = false, size_t compareCol = 0,
	                             DataCompareOp cmp = DataCompareOp::Always, T threshold = T{})
	{
		return RunNoisePerturbColumnChunk<T>(0, mRowCount, targetCol, op, operandCol, noiseLo, noiseHi, seed, tick,
		                                     hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunNoisePerturbColumn"/> (used by the Lens).</summary>
	template <typename T>
	size_t RunNoisePerturbColumnChunk(size_t rowBegin, size_t rowEnd, size_t targetCol, DataSystemOp op,
	                                  size_t operandCol, T noiseLo, T noiseHi, uint64_t seed, uint64_t tick,
	                                  bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		if (targetCol >= mColumns.size() || operandCol >= mColumns.size())
			return 0;
		if (hasPredicate && compareCol >= mColumns.size())
			return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;
		return RunNoiseColumnChunkImpl<T, true>(rowBegin, rowEnd, targetCol, op, operandCol, noiseLo, noiseHi,
		                                        seed, tick, hasPredicate, compareCol, cmp, threshold);
	}

	/// <summary>
	/// Argmax-across-columns (DataLens-Spec A3.13): the §8.5 selection "pick". For each live row, reduce
	/// the K score columns to the INDEX of the largest score and write it into <paramref name="choiceCol"/>
	/// (an Int32 column). Ties resolve to the LOWEST index (strict greater-than) so the choice is
	/// deterministic. If the winning score is below <paramref name="minScore"/> the row gets
	/// <paramref name="noChoice"/> (a sentinel, e.g. -1 = "do nothing"), which composes with the §8.5
	/// `Choice = Command>=0 ? Command : pick` override. K=0 writes <paramref name="noChoice"/> everywhere.
	/// Returns rows written. One branchless reduction pass — the AI selection step at column speed.
	/// </summary>
	template <typename T>
	size_t RunArgmaxColumns(size_t choiceCol, const size_t* scoreCols, size_t scoreColCount,
	                        T minScore, int32_t noChoice)
	{
		return RunArgmaxColumnsChunk<T>(0, mRowCount, choiceCol, scoreCols, scoreColCount, minScore, noChoice);
	}

	/// <summary>Disjoint sub-range form of <see cref="RunArgmaxColumns"/> (used by the Lens). Each row is
	/// independent, so any [rowBegin,rowEnd) chunking yields identical results.</summary>
	// A utility-AI actor rarely chooses between more than a few dozen candidate abilities; a fixed cap
	// lets us hoist the score-column base pointers onto the stack (no per-row column lookups) and keeps
	// the reduction allocation-free on the parallel hot path.
	static constexpr size_t kMaxArgmaxColumns = 64;

	template <typename T>
	size_t RunArgmaxColumnsChunk(size_t rowBegin, size_t rowEnd, size_t choiceCol,
	                             const size_t* scoreCols, size_t scoreColCount, T minScore, int32_t noChoice)
	{
		if (choiceCol >= mColumns.size() || scoreColCount > kMaxArgmaxColumns)
			return 0;
		for (size_t k = 0; k < scoreColCount; ++k)
			if (scoreCols[k] >= mColumns.size())
				return 0;
		if (rowEnd > mRowCount)
			rowEnd = mRowCount;

		// Hoist the score-column bases/strides out of the row loop.
		const uint8_t* sbase[kMaxArgmaxColumns];
		size_t sstride[kMaxArgmaxColumns];
		for (size_t k = 0; k < scoreColCount; ++k)
		{
			sbase[k] = mColumnsData[scoreCols[k]].data();
			sstride[k] = mColumns[scoreCols[k]].GetStride();
		}
		uint8_t* cchoice = mColumnsData[choiceCol].data();
		const size_t cstride = mColumns[choiceCol].GetStride();

		size_t written = 0;
		for (size_t r = rowBegin; r < rowEnd; ++r)
		{
			const bool live = (mValidBits[r >> 6] >> (r & 63)) & 1ULL;

			int32_t choice = noChoice;
			if (scoreColCount > 0)
			{
				size_t bestIdx = 0;
				T bestVal;
				std::memcpy(&bestVal, sbase[0] + r * sstride[0], sizeof(T));
				for (size_t k = 1; k < scoreColCount; ++k)
				{
					T v;
					std::memcpy(&v, sbase[k] + r * sstride[k], sizeof(T));
					if (v > bestVal) { bestVal = v; bestIdx = k; } // strict > keeps the lowest index on a tie
				}
				choice = (bestVal >= minScore) ? static_cast<int32_t>(bestIdx) : noChoice;
			}

			int32_t cur;
			std::memcpy(&cur, cchoice + r * cstride, sizeof(int32_t));
			const int32_t result = live ? choice : cur; // select write-back: leave dead rows untouched
			std::memcpy(cchoice + r * cstride, &result, sizeof(int32_t));
			written += live ? 1u : 0u;
		}
		return written;
	}

	/// <summary>SplitMix64 finaliser — a fast, well-distributed integer hash. The stateless counter-based
	/// PRNG core for <see cref="Noise01"/> (DataLens-Spec A3.12).</summary>
	static uint64_t SplitMix64(uint64_t z)
	{
		z += 0x9E3779B97F4A7C15ULL;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		return z ^ (z >> 31);
	}

	/// <summary>Counter-based PRNG: a uniform value in [0,1) keyed on (row, tick, seed) with no global
	/// state, so it is reproducible across runs, machines and replay, and identical regardless of how the
	/// row range is chunked across worker threads (the key is the GLOBAL row index). The top 24 bits feed
	/// a float, giving exact representability of every step in [0,1). Public so callers can predict/verify
	/// the noise a noise System will apply.</summary>
	static float Noise01(uint64_t row, uint64_t tick, uint64_t seed)
	{
		const uint64_t h = SplitMix64(seed ^ SplitMix64(row ^ SplitMix64(tick)));
		return static_cast<float>(h >> 40) * (1.0f / 16777216.0f); // (h>>40) in [0, 2^24) -> [0,1)
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

	/// <summary>
	/// Counter-based noise kernel (DataLens-Spec A3.12). Per live row r passing the optional same-type
	/// predicate: `noise = noiseLo + (noiseHi-noiseLo) * Noise01(r, tick, seed)`; the right-hand side is
	/// `operandCol[r] * noise` when <c>OperandIsColumn</c> (the §8.4 perturb, e.g. Variance * Noise) or
	/// `noise` itself otherwise (a fill); then `target = target OP rhs` via a select write-back. Numeric
	/// ops only (Set/Add/Sub/Mul/Min/Max) — noise on a bitmask column is meaningless. The dead operand-source
	/// branch is eliminated at compile time, so the hot loop is branch-free on operand source.
	/// </summary>
	template <typename T, bool OperandIsColumn>
	size_t RunNoiseColumnChunkImpl(size_t rowBegin, size_t rowEnd, size_t targetCol, DataSystemOp op,
	                               size_t operandCol, T noiseLo, T noiseHi, uint64_t seed, uint64_t tick,
	                               bool hasPredicate, size_t compareCol, DataCompareOp cmp, T threshold)
	{
		uint8_t* tcol = mColumnsData[targetCol].data();
		const size_t tstride = mColumns[targetCol].GetStride();
		const uint8_t* ocol = OperandIsColumn ? mColumnsData[operandCol].data() : nullptr;
		const size_t ostride = OperandIsColumn ? mColumns[operandCol].GetStride() : 0;
		const uint8_t* pcol = hasPredicate ? mColumnsData[compareCol].data() : nullptr;
		const size_t pstride = hasPredicate ? mColumns[compareCol].GetStride() : 0;
		const float span = static_cast<float>(noiseHi) - static_cast<float>(noiseLo);

		size_t affected = 0;
		for (size_t r = rowBegin; r < rowEnd; ++r)
		{
			const bool live = (mValidBits[r >> 6] >> (r & 63)) & 1ULL;

			T cur;
			std::memcpy(&cur, tcol + r * tstride, sizeof(T));

			const float noise = static_cast<float>(noiseLo) + span * Noise01(r, tick, seed);
			T rhs;
			if (OperandIsColumn)
			{
				T operand;
				std::memcpy(&operand, ocol + r * ostride, sizeof(T));
				rhs = static_cast<T>(static_cast<float>(operand) * noise);
			}
			else
				rhs = static_cast<T>(noise);

			T computed;
			switch (op)
			{
			case DataSystemOp::Set: computed = rhs; break;
			case DataSystemOp::Add: computed = static_cast<T>(cur + rhs); break;
			case DataSystemOp::Sub: computed = static_cast<T>(cur - rhs); break;
			case DataSystemOp::Mul: computed = static_cast<T>(cur * rhs); break;
			case DataSystemOp::Min: computed = cur < rhs ? cur : rhs; break;
			case DataSystemOp::Max: computed = cur > rhs ? cur : rhs; break;
			default:                computed = cur; break; // bitwise ops are undefined for noise
			}

			bool match = true;
			if (hasPredicate)
			{
				T pv;
				std::memcpy(&pv, pcol + r * pstride, sizeof(T));
				match = ComparePredicate<T>(pv, cmp, threshold);
			}

			const bool apply = live && match;
			const T result = apply ? computed : cur; // select write-back (lowers to cmov)
			std::memcpy(tcol + r * tstride, &result, sizeof(T));
			affected += apply ? 1u : 0u;
		}
		return affected;
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

/******************************************************************************
 * c_api.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Minimal, stable C ABI over the DataLens core for per-engine Foundation
 * bindings (Unity P/Invoke first). THIN SLICE: covers DataStore create/access
 * only — enough to prove the managed<->native boundary. The full world/Lens/IR
 * ABI lands in A7.
 *
 * Value type codes mirror DataLensValueType:
 *   0 Bool, 1 Int8, 2 UInt8, 3 Int16, 4 UInt16, 5 Int32, 6 UInt32,
 *   7 Int64, 8 UInt64, 9 Float, 10 Double, 11 GUID
 ******************************************************************************/

#ifndef DATALENS_C_API_H
#define DATALENS_C_API_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
    #define DL_API __declspec(dllexport)
#else
    #define DL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dl_store dl_store; /* opaque handle to a DataStore */

/* ABI version for the managed side to sanity-check against. Bump on breaking change. */
DL_API int32_t dl_abi_version(void);

/* Range-narrowing (A2): smallest byte-aligned value-type code that holds the given range. */
DL_API int32_t dl_smallest_uint_type(uint64_t maxValue);
DL_API int32_t dl_smallest_int_type(int64_t minValue, int64_t maxValue);

/* Create a store. colTags/colStrides are parallel arrays of length colCount: colTags are the columns'
 * globally-unique ids (u64), colStrides their byte widths (Core is type-blind). colDefaults is the
 * columns' default values concatenated in order (Sum(colStrides) bytes), used to seed each new row on
 * AllocRow; pass NULL for all-zero defaults. Returns NULL on invalid args. Caller owns (dl_store_destroy). */
DL_API dl_store* dl_store_create(const uint64_t* colTags,
                                 const uint64_t* colStrides,
                                 const uint8_t*  colDefaults,
                                 int32_t colCount,
                                 uint64_t preallocRows);

DL_API void dl_store_destroy(dl_store* store);

DL_API uint64_t dl_store_row_count(const dl_store* store);
DL_API uint64_t dl_store_column_count(const dl_store* store);
DL_API uint64_t dl_store_row_stride(const dl_store* store);

/* Typed cell access. Return 1 on success, 0 on out-of-range / type mismatch.
 * Bounds- and stride-checked at the boundary (TryGet/TrySet semantics). */
DL_API int32_t dl_store_set_f32(dl_store* store, uint64_t row, uint64_t col, float value);
DL_API int32_t dl_store_get_f32(const dl_store* store, uint64_t row, uint64_t col, float* out);

DL_API int32_t dl_store_set_i32(dl_store* store, uint64_t row, uint64_t col, int32_t value);
DL_API int32_t dl_store_get_i32(const dl_store* store, uint64_t row, uint64_t col, int32_t* out);

DL_API int32_t dl_store_set_f64(dl_store* store, uint64_t row, uint64_t col, double value);
DL_API int32_t dl_store_get_f64(const dl_store* store, uint64_t row, uint64_t col, double* out);

/* Row liveness (dense validity bitmask). */
DL_API void    dl_store_set_valid(dl_store* store, uint64_t row, int32_t valid);
DL_API int32_t dl_store_is_valid(const dl_store* store, uint64_t row);

/* Per-row Simulation LOD (A3.5): 0 = highest fidelity / always runs, higher = coarser. Held in a
 * dense byte array separate from column data. level is clamped to 0..255. */
DL_API void    dl_store_set_lod(dl_store* store, uint64_t row, int32_t level);
DL_API int32_t dl_store_get_lod(const dl_store* store, uint64_t row);

/* Explicit fixed-capacity allocation. dl_store_alloc_row returns the new row index, or
 * UINT64_MAX when the store is at capacity. */
DL_API uint64_t dl_store_alloc_row(dl_store* store);
DL_API void     dl_store_free_row(dl_store* store, uint64_t row);
DL_API uint64_t dl_store_live_count(const dl_store* store);

/* Systems (A3): run a data-described conditional column transform over all live rows.
 * For every live row where (compareCol CMP threshold) holds, apply (targetCol = targetCol OP operand).
 *   op  : 0 Set, 1 Add, 2 Sub, 3 Mul, 4 Min, 5 Max
 *   cmp : 0 Always, 1 Eq, 2 Ne, 3 Lt, 4 Le, 5 Gt, 6 Ge   (ignored when hasPredicate == 0)
 * targetCol and compareCol are interpreted as the function's element type. Returns rows affected. */
DL_API uint64_t dl_store_run_f32(dl_store* store, uint64_t targetCol, int32_t op, float operand,
                                 int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold);
DL_API uint64_t dl_store_run_i32(dl_store* store, uint64_t targetCol, int32_t op, int32_t operand,
                                 int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold);

/* Cross-column Systems (A3.3): like dl_store_run_* but the operand for each row is read from
 * operandCol instead of being a scalar (e.g. targetCol = targetCol + operandCol, or a per-row
 * clamp targetCol = min(targetCol, operandCol)). Same op/cmp codes. Returns rows affected. */
DL_API uint64_t dl_store_run_col_f32(dl_store* store, uint64_t targetCol, int32_t op, uint64_t operandCol,
                                     int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold);
DL_API uint64_t dl_store_run_col_i32(dl_store* store, uint64_t targetCol, int32_t op, uint64_t operandCol,
                                     int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold);

/* Lens (A3): owns a worker pool and RUNS Systems in parallel. dl_lens_create(0) uses
 * hardware_concurrency. dl_lens_run_* parallelise the same System as dl_store_run_* across the
 * Lens's threads, with identical results regardless of thread count. */
typedef struct dl_lens dl_lens;

DL_API dl_lens* dl_lens_create(int32_t threadCount);
DL_API void     dl_lens_destroy(dl_lens* lens);
DL_API int32_t  dl_lens_thread_count(const dl_lens* lens);

DL_API uint64_t dl_lens_run_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                float operand, int32_t hasPredicate, uint64_t compareCol, int32_t cmp,
                                float threshold);
DL_API uint64_t dl_lens_run_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                int32_t operand, int32_t hasPredicate, uint64_t compareCol, int32_t cmp,
                                int32_t threshold);

/* Parallel cross-column Systems (A3.3): the Lens form of dl_store_run_col_*. */
DL_API uint64_t dl_lens_run_col_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                    uint64_t operandCol, int32_t hasPredicate, uint64_t compareCol,
                                    int32_t cmp, float threshold);
DL_API uint64_t dl_lens_run_col_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                    uint64_t operandCol, int32_t hasPredicate, uint64_t compareCol,
                                    int32_t cmp, int32_t threshold);

/* Parallel curved cross-column Systems (A3.11): rhs = curve(operandCol[r]) before the combine — the
 * HATE §8 considerations primitive. curveType is a DataCurveType (0 Linear / 1 Power / 2 Smoothstep /
 * 3 Threshold); the operand is normalised over [curveMin,curveMax] then curved to [0,1], invert flips it. */
DL_API uint64_t dl_lens_run_curved_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                       uint64_t operandCol, int32_t curveType, float curveMin, float curveMax,
                                       float curveP0, float curveP1, int32_t curveInvert,
                                       int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold);
DL_API uint64_t dl_lens_run_curved_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                       uint64_t operandCol, int32_t curveType, float curveMin, float curveMax,
                                       float curveP0, float curveP1, int32_t curveInvert,
                                       int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold);

/* Counter-based noise (A3.12): noise = noiseLo + (noiseHi-noiseLo)*u01(row,tick,seed), a stateless PRNG
 * keyed on the GLOBAL row index (reproducible across runs/machines/replay; serial==parallel). The fill
 * form does `targetCol = targetCol OP noise` (op Set fills a noise column, Add jitters an accumulator).
 * The perturb form does `targetCol = targetCol OP (operandCol[r]*noise)` — the HATE §8.4
 * `Score += Variance*Noise` in one pass (Variance 0 rows unchanged). Numeric ops only. */
DL_API uint64_t dl_lens_run_noise_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                      float noiseLo, float noiseHi, uint64_t seed, uint64_t tick,
                                      int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold);
DL_API uint64_t dl_lens_run_noise_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                      int32_t noiseLo, int32_t noiseHi, uint64_t seed, uint64_t tick,
                                      int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold);
DL_API uint64_t dl_lens_run_noise_perturb_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                              uint64_t operandCol, float noiseLo, float noiseHi, uint64_t seed,
                                              uint64_t tick, int32_t hasPredicate, uint64_t compareCol,
                                              int32_t cmp, float threshold);
DL_API uint64_t dl_lens_run_noise_perturb_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                              uint64_t operandCol, int32_t noiseLo, int32_t noiseHi, uint64_t seed,
                                              uint64_t tick, int32_t hasPredicate, uint64_t compareCol,
                                              int32_t cmp, int32_t threshold);

/* Argmax-across-columns (A3.13): reduce `scoreColCount` score columns to the index of the per-row max,
 * written into `choiceCol` (an Int32 column) — the HATE §8.5 selection "pick". Ties resolve to the lowest
 * index; a winning score below `minScore` writes the `noChoice` sentinel (e.g. -1 = do nothing, composing
 * with the §8.5 Command override). `scoreCols` points to `scoreColCount` column indices (max 64).
 * Returns rows written. */
DL_API uint64_t dl_lens_run_argmax_f32(dl_lens* lens, dl_store* store, uint64_t choiceCol,
                                       const uint64_t* scoreCols, uint64_t scoreColCount,
                                       float minScore, int32_t noChoice);
DL_API uint64_t dl_lens_run_argmax_i32(dl_lens* lens, dl_store* store, uint64_t choiceCol,
                                       const uint64_t* scoreCols, uint64_t scoreColCount,
                                       int32_t minScore, int32_t noChoice);

/* Batched Systems (A3.4): a data-described System the Lens can schedule. The Lens runs a whole
 * array of these in one call, executing non-conflicting Systems concurrently while preserving
 * deterministic submission order for conflicting ones (see dl_lens_run_batch).
 *   elem_type        : DataLensValueType code; 5 Int32 and 9 Float supported (others skipped)
 *   op / cmp         : DataSystemOp / DataCompareOp codes (see dl_store_run_*)
 *   operand_is_column: 0 = scalar `operand`, 1 = read operand per-row from `operand_col`
 *   has_predicate    : 0 = apply to all live rows, 1 = gate on (compare_col CMP threshold)
 * Scalar `operand`/`threshold` are carried as double and cast to the element type at execution
 * (Int32 fits exactly). Layout is fixed/padded for a stable ABI; mirror it exactly on the managed side. */
typedef struct dl_system_desc
{
    dl_store* store;
    int32_t   elem_type;
    int32_t   op;
    int32_t   operand_is_column;
    int32_t   has_predicate;
    int32_t   cmp;
    int32_t   _pad;            /* keep the following uint64 fields 8-byte aligned */
    uint64_t  target_col;
    uint64_t  operand_col;
    uint64_t  compare_col;
    double    operand;
    double    threshold;
    /* Response curve (A3.11): when apply_curve, the per-row operand (cross-column only) is normalised
     * over [curve_min,curve_max] and passed through curve_type before the combine. */
    int32_t   apply_curve;
    int32_t   curve_type;      /* DataCurveType: 0 Linear / 1 Power / 2 Smoothstep / 3 Threshold */
    int32_t   curve_invert;
    int32_t   _pad2;
    float     curve_min;
    float     curve_max;
    float     curve_p0;
    float     curve_p1;
} dl_system_desc;

/* Run `count` Systems via the Lens. Returns the total rows affected across the batch. */
DL_API uint64_t dl_lens_run_batch(dl_lens* lens, const dl_system_desc* descs, uint64_t count);

/* Run the batch but only over rows whose Simulation LOD is within [minLod, maxLod] (A3.5) — the
 * "this tick runs at fidelity band [min,max]" model. The band applies to every System in the batch. */
DL_API uint64_t dl_lens_run_batch_lod(dl_lens* lens, const dl_system_desc* descs, uint64_t count,
                                      int32_t minLod, int32_t maxLod);

/* ── Query/Update IR (A4) ──────────────────────────────────────────────────
 * A pointer-free, serialisable program of System ops. Stores are referenced by INDEX into the table
 * passed to dl_lens_execute / dl_lens_tick. One op (mirror this struct exactly on the managed side):
 * fixed 96-byte layout. min_lod/max_lod are clamped to 0..255. */
typedef struct dl_ir_op
{
    int32_t store_index;
    int32_t elem_type;        /* DataLensValueType: 5 Int32, 9 Float */
    int32_t target_col;
    int32_t op;               /* DataSystemOp */
    int32_t operand_is_column;
    int32_t operand_col;
    int32_t has_predicate;
    int32_t compare_col;
    int32_t cmp;              /* DataCompareOp */
    int32_t min_lod;
    int32_t max_lod;
    int32_t _pad;
    double  operand;
    double  threshold;
    /* Response curve (A3.11): when apply_curve, the per-row operand (cross-column only) is normalised
     * over [curve_min,curve_max] and passed through curve_type before the combine. */
    int32_t apply_curve;
    int32_t curve_type;       /* DataCurveType */
    int32_t curve_invert;
    int32_t _pad2;
    float   curve_min;
    float   curve_max;
    float   curve_p0;
    float   curve_p1;
} dl_ir_op;

typedef struct dl_ir_program dl_ir_program; /* opaque */

DL_API dl_ir_program* dl_ir_create(void);
DL_API void           dl_ir_destroy(dl_ir_program* program);
DL_API void           dl_ir_add_system(dl_ir_program* program, const dl_ir_op* op);
DL_API uint64_t       dl_ir_count(const dl_ir_program* program);

/* Serialise to / parse from a flat byte buffer. dl_ir_serialize writes up to bufLen bytes and returns
 * the number of bytes the program needs (call with buf==NULL to query the size). dl_ir_deserialize
 * returns a new program (caller owns) or NULL on a bad/truncated buffer. */
DL_API uint64_t       dl_ir_serialize(const dl_ir_program* program, uint8_t* buf, uint64_t bufLen);
DL_API dl_ir_program* dl_ir_deserialize(const uint8_t* data, uint64_t size);

/* Execute a program against a store table (resolves store_index). Returns total rows affected. */
DL_API uint64_t dl_lens_execute(dl_lens* lens, const dl_ir_program* program,
                                dl_store* const* stores, uint64_t storeCount);

/* ── Tick / cadence scheduler (A5) ─────────────────────────────────────────
 * The Lens owns a tick counter, a set of scheduled programs, and a set of scheduled views. Each Tick
 * advances the counter, runs the due programs, then refreshes the due views. Programs are copied in;
 * views are referenced (the caller owns the dl_view). period < 1 is clamped to 1. */
DL_API uint64_t dl_lens_add_scheduled_program(dl_lens* lens, const dl_ir_program* program,
                                              uint64_t period, int32_t minLod, int32_t maxLod, uint64_t phase);
DL_API void     dl_lens_clear_schedule(dl_lens* lens);
DL_API uint64_t dl_lens_scheduled_program_count(const dl_lens* lens);

DL_API uint64_t dl_lens_add_scheduled_view(dl_lens* lens, struct dl_view* view, uint64_t storeIndex,
                                           uint64_t period, uint64_t phase);
DL_API uint64_t dl_lens_add_scheduled_view_lod(dl_lens* lens, struct dl_view* view, uint64_t storeIndex,
                                               uint64_t period, int32_t minLod, int32_t maxLod, uint64_t phase);
DL_API void     dl_lens_clear_scheduled_views(dl_lens* lens);
DL_API uint64_t dl_lens_scheduled_view_count(const dl_lens* lens);

DL_API uint64_t dl_lens_current_tick(const dl_lens* lens);
DL_API void     dl_lens_reset_tick(dl_lens* lens, uint64_t tick);
DL_API uint64_t dl_lens_tick(dl_lens* lens, dl_store* const* stores, uint64_t storeCount);

/* Refresh a view from a store, materialised in PARALLEL across the Lens pool (identical result to
 * dl_view_refresh, much faster for large views). _lod variant restricts to [minLod, maxLod]. */
DL_API void dl_lens_refresh_view(dl_lens* lens, struct dl_view* view, const dl_store* store);
DL_API void dl_lens_refresh_view_lod(dl_lens* lens, struct dl_view* view, const dl_store* store,
                                     int32_t minLod, int32_t maxLod);

/* Mixed-type predicate System: a float-valued op (targetCol = targetCol OP operand) applied to every
 * live row where an INT32 predicate column satisfies (cmp threshold). The branchless one-pass form of
 * "gate a float-attribute effect by an int tag-bitmask column" (cmp 7/8/9 = HasAll/HasAny/Lacks bits).
 * Returns rows affected. */
DL_API uint64_t dl_lens_run_f32_pred_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                         float operand, uint64_t compareCol, int32_t cmp, int32_t threshold);

/* The mirror: an INT32 op gated by a FLOAT predicate column (e.g. knock out an int eligibility flag
 * where a float resource column is below a threshold). Returns rows affected. */
DL_API uint64_t dl_lens_run_i32_pred_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                         int32_t operand, uint64_t compareCol, int32_t cmp, float threshold);

/* ── Read-only DataView (A5) ───────────────────────────────────────────────
 * A row-major snapshot of selected store columns. Build with the source column indices, then Refresh
 * from a store (a copy — safe to read while the Lens mutates the store). */
typedef struct dl_view dl_view; /* opaque */

DL_API dl_view* dl_view_create(const uint64_t* sourceColumns, uint64_t columnCount);
DL_API void     dl_view_destroy(dl_view* view);
DL_API void     dl_view_refresh(dl_view* view, const dl_store* store);
DL_API void     dl_view_refresh_lod(dl_view* view, const dl_store* store, int32_t minLod, int32_t maxLod);

DL_API uint64_t dl_view_row_count(const dl_view* view);
DL_API uint64_t dl_view_column_count(const dl_view* view);
DL_API uint64_t dl_view_row_stride(const dl_view* view);
DL_API uint64_t dl_view_source_row(const dl_view* view, uint64_t viewRow);

DL_API int32_t  dl_view_get_f32(const dl_view* view, uint64_t viewRow, uint64_t viewCol, float* out);
DL_API int32_t  dl_view_get_i32(const dl_view* view, uint64_t viewRow, uint64_t viewCol, int32_t* out);
DL_API int32_t  dl_view_get_f64(const dl_view* view, uint64_t viewRow, uint64_t viewCol, double* out);

/* Bulk/zero-copy access to the whole row-major snapshot (valid until the next refresh): the base
 * pointer + its byte size. For one marshalled copy into a managed array (essential at scale). */
DL_API const void* dl_view_data(const dl_view* view);
DL_API uint64_t    dl_view_byte_size(const dl_view* view);

/* ---- The read/write View (DataLens-Spec.md §6.4): projection + index-joins + scope -> raw payload +
 * change flags, with an Insert/Update/Delete write-back. POD mirrors of the view IR follow (all ids and
 * indices are u64; aligned/type/op cross as int32). ---- */
typedef struct dl_rwview dl_rwview; /* opaque handle to a datalens::View */

typedef struct dl_view_join
{
    uint64_t target_store;
    int32_t  aligned;          /* nonzero = index-aligned (target row == base row) */
    uint64_t index_column;     /* (aligned=0) base column holding the target row index */
    uint64_t absent_sentinel;  /* index value meaning "absent" (e.g. int32.Max) */
} dl_view_join;

typedef struct dl_view_column
{
    uint64_t source;           /* 0 = base store; k>=1 = the (k-1)th join's target */
    uint64_t column;           /* column index within that store */
} dl_view_column;

typedef struct dl_view_scope
{
    uint64_t column;           /* base store column index */
    int32_t  type;             /* DataLensValueType (interprets the cell for the comparison) */
    int32_t  op;               /* DataCompareOp */
    int64_t  ivalue;           /* integer / bitmask threshold */
    double   dvalue;           /* float / double threshold */
} dl_view_scope;

typedef struct dl_view_write
{
    uint64_t view_column;
    uint64_t target_store;
    uint64_t target_column;
} dl_view_write;

/* One node of a view's predicate program (RPN of leaves + And/Or/Not). kind: 0 leaf, 1 and, 2 or, 3 not.
   A leaf addresses a cell like a projected column (source 0 = base, k>=1 = the (k-1)th join). is_range
   nonzero = the fused interval test [ivalue, ivalue_hi] / [dvalue, dvalue_hi]; otherwise `op` is used
   against the single threshold (ivalue / dvalue). Connective nodes ignore the leaf fields. */
typedef struct dl_view_predicate
{
    int32_t  kind;
    int32_t  is_range;
    int32_t  source;
    int32_t  column;
    int32_t  type;             /* DataLensValueType */
    int32_t  op;               /* DataCompareOp (ignored when is_range) */
    int64_t  ivalue;           /* threshold / range lo (integer / bitmask) */
    int64_t  ivalue_hi;        /* range hi (integer) */
    double   dvalue;           /* threshold / range lo (float / double) */
    double   dvalue_hi;        /* range hi (float / double) */
} dl_view_predicate;

/* Create a compiled read/write view. Parallel arrays; any count may be 0. Caller owns (dl_rwview_destroy). */
DL_API dl_rwview* dl_rwview_create(uint64_t baseStore,
                                   const dl_view_join* joins, int32_t joinCount,
                                   const dl_view_column* columns, int32_t columnCount,
                                   const dl_view_scope* scope, int32_t scopeCount);
DL_API void dl_rwview_destroy(dl_rwview* view);

/* Set the write-back: Insert/Update column maps + Delete target stores. */
DL_API void dl_rwview_set_writeback(dl_rwview* view,
                                    const dl_view_write* insert, int32_t insertCount,
                                    const dl_view_write* update, int32_t updateCount,
                                    const uint64_t* deleteStores, int32_t deleteCount);

/* Set the predicate program (RPN of leaves + And/Or/Not, evaluated per base row after joins resolve).
   count 0 clears it. ANDs with any scope list passed to dl_rwview_create. */
DL_API void dl_rwview_set_scope_program(dl_rwview* view,
                                        const dl_view_predicate* preds, int32_t predCount);

/* Hydrate / commit against a store table (indexed by store index). Commit returns the ops applied. */
DL_API void     dl_rwview_refresh(dl_rwview* view, const dl_store* const* stores, int32_t storeCount);
DL_API uint64_t dl_rwview_commit(dl_rwview* view, dl_store* const* stores, int32_t storeCount);

/* Payload + layout (the raw row-major bytes the Foundation marshals into a managed DataView). */
DL_API uint64_t       dl_rwview_row_count(const dl_rwview* view);
DL_API uint64_t       dl_rwview_column_count(const dl_rwview* view);
DL_API uint64_t       dl_rwview_row_stride(const dl_rwview* view);
DL_API uint64_t       dl_rwview_byte_size(const dl_rwview* view);
DL_API const uint8_t* dl_rwview_data(const dl_rwview* view);
DL_API uint8_t*       dl_rwview_mutable_data(dl_rwview* view);
DL_API uint64_t       dl_rwview_column_offset(const dl_rwview* view, uint64_t col);
DL_API uint64_t       dl_rwview_column_stride(const dl_rwview* view, uint64_t col);

/* Change state + insert (the write surface). */
DL_API uint8_t  dl_rwview_get_state(const dl_rwview* view, uint64_t viewRow);
DL_API void     dl_rwview_set_state(dl_rwview* view, uint64_t viewRow, uint8_t state);
DL_API uint64_t dl_rwview_add_row(dl_rwview* view);

/* Source map (write-back record resolution). Returns UINT64_MAX (NoRow) when absent. */
DL_API uint64_t dl_rwview_source_base_row(const dl_rwview* view, uint64_t viewRow);
DL_API uint64_t dl_rwview_source_join_row(const dl_rwview* view, uint64_t viewRow, uint64_t join);

/* Schedule a read/write view on the Lens (commit -> Systems -> refresh each due tick). */
DL_API uint64_t dl_lens_add_scheduled_rwview(dl_lens* lens, dl_rwview* view, uint64_t period, uint64_t phase);
DL_API void     dl_lens_clear_scheduled_rwviews(dl_lens* lens);
DL_API uint64_t dl_lens_scheduled_rwview_count(const dl_lens* lens);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATALENS_C_API_H */

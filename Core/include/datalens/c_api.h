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

/* Create a store. colNames/colTypes are parallel arrays of length colCount.
 * Returns NULL on invalid arguments. Caller owns the handle (dl_store_destroy). */
DL_API dl_store* dl_store_create(const char* const* colNames,
                                 const int32_t* colTypes,
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATALENS_C_API_H */

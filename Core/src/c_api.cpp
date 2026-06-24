/******************************************************************************
 * c_api.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Thin C ABI shim over DataStore. No engine types cross the boundary.
 ******************************************************************************/

#include "datalens/c_api.h"

#include "datalens/DataStore.h"
#include "datalens/DataLensSchema.h"
#include "datalens/DataView.h"
#include "datalens/Ir.h"
#include "datalens/Lens.h"
#include "datalens/View.h"

#include <string>
#include <vector>

namespace
{
    DataStore* AsStore(dl_store* s) { return reinterpret_cast<DataStore*>(s); }
    const DataStore* AsStore(const dl_store* s) { return reinterpret_cast<const DataStore*>(s); }
    datalens::Lens* AsLens(dl_lens* l) { return reinterpret_cast<datalens::Lens*>(l); }
    const datalens::Lens* AsLens(const dl_lens* l) { return reinterpret_cast<const datalens::Lens*>(l); }
    datalens::IrProgram* AsProgram(dl_ir_program* p) { return reinterpret_cast<datalens::IrProgram*>(p); }
    const datalens::IrProgram* AsProgram(const dl_ir_program* p) { return reinterpret_cast<const datalens::IrProgram*>(p); }
    datalens::DataView* AsView(dl_view* v) { return reinterpret_cast<datalens::DataView*>(v); }
    const datalens::DataView* AsView(const dl_view* v) { return reinterpret_cast<const datalens::DataView*>(v); }
    datalens::View* AsRwView(dl_rwview* v) { return reinterpret_cast<datalens::View*>(v); }
    const datalens::View* AsRwView(const dl_rwview* v) { return reinterpret_cast<const datalens::View*>(v); }

    // The store table crosses the ABI as dl_store*; each is a DataStore*.
    DataStore* const* AsStoreTable(dl_store* const* stores)
    {
        return reinterpret_cast<DataStore* const*>(stores);
    }

    std::uint8_t ClampLod(int32_t v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

    // Convert the C descriptor array to core SystemDescs. When useBand is true, force every System's
    // LOD band to [minLod, maxLod] (the per-batch fidelity model); otherwise leave each at "all rows".
    std::vector<datalens::SystemDesc> BuildDescs(const dl_system_desc* descs, uint64_t count,
                                                 bool useBand, uint8_t minLod, uint8_t maxLod)
    {
        std::vector<datalens::SystemDesc> v;
        v.reserve(static_cast<size_t>(count));
        for (uint64_t k = 0; k < count; ++k)
        {
            const dl_system_desc& s = descs[k];
            datalens::SystemDesc d;
            d.store           = AsStore(s.store);
            d.elemType        = static_cast<DataLensValueType>(s.elem_type);
            d.targetCol       = static_cast<size_t>(s.target_col);
            d.op              = static_cast<DataSystemOp>(s.op);
            d.operandIsColumn = s.operand_is_column != 0;
            d.operandCol      = static_cast<size_t>(s.operand_col);
            d.operand         = s.operand;
            d.hasPredicate    = s.has_predicate != 0;
            d.compareCol      = static_cast<size_t>(s.compare_col);
            d.cmp             = static_cast<DataCompareOp>(s.cmp);
            d.threshold       = s.threshold;
            d.applyCurve      = s.apply_curve != 0;
            d.curve.type      = static_cast<DataCurveType>(s.curve_type);
            d.curve.min       = s.curve_min;
            d.curve.max       = s.curve_max;
            d.curve.p0        = s.curve_p0;
            d.curve.p1        = s.curve_p1;
            d.curve.invert    = s.curve_invert != 0;
            if (useBand)
            {
                d.minLod = minLod;
                d.maxLod = maxLod;
            }
            v.push_back(d);
        }
        return v;
    }
}

extern "C" {

int32_t dl_abi_version(void)
{
    return 1;
}

int32_t dl_smallest_uint_type(uint64_t maxValue)
{
    return static_cast<int32_t>(DataLensValueTypeUtils::SmallestUnsignedForMax(maxValue));
}

int32_t dl_smallest_int_type(int64_t minValue, int64_t maxValue)
{
    return static_cast<int32_t>(DataLensValueTypeUtils::SmallestSignedForRange(minValue, maxValue));
}

dl_store* dl_store_create(const uint64_t* colTags,
                          const uint64_t* colStrides,
                          const uint8_t*  colDefaults,
                          int32_t colCount,
                          uint64_t preallocRows)
{
    if (colCount <= 0 || colTags == nullptr || colStrides == nullptr)
        return nullptr;

    std::vector<DataStoreColumnSchema> cols;
    cols.reserve(static_cast<size_t>(colCount));
    size_t defOffset = 0; // colDefaults is the columns' default values concatenated in order
    for (int32_t i = 0; i < colCount; ++i)
    {
        const size_t stride = static_cast<size_t>(colStrides[i]);
        DataStoreColumnSchema c(static_cast<DataLensId>(colTags[i]), stride);
        if (colDefaults != nullptr)
            c.DefaultValue.assign(colDefaults + defOffset, colDefaults + defOffset + stride);
        defOffset += stride;
        cols.push_back(std::move(c));
    }

    return reinterpret_cast<dl_store*>(new DataStore(cols, static_cast<size_t>(preallocRows)));
}

void dl_store_destroy(dl_store* store)
{
    delete AsStore(store);
}

uint64_t dl_store_row_count(const dl_store* store)
{
    return store ? AsStore(store)->GetRowCount() : 0;
}

uint64_t dl_store_column_count(const dl_store* store)
{
    return store ? AsStore(store)->GetColumnCount() : 0;
}

uint64_t dl_store_row_stride(const dl_store* store)
{
    return store ? AsStore(store)->GetRowStride() : 0;
}

int32_t dl_store_set_f32(dl_store* store, uint64_t row, uint64_t col, float value)
{
    if (!store) return 0;
    return AsStore(store)->TrySet<float>(static_cast<size_t>(row), static_cast<size_t>(col), value) ? 1 : 0;
}

int32_t dl_store_get_f32(const dl_store* store, uint64_t row, uint64_t col, float* out)
{
    if (!store || !out) return 0;
    return AsStore(store)->TryGet<float>(static_cast<size_t>(row), static_cast<size_t>(col), *out) ? 1 : 0;
}

int32_t dl_store_set_i32(dl_store* store, uint64_t row, uint64_t col, int32_t value)
{
    if (!store) return 0;
    return AsStore(store)->TrySet<int32_t>(static_cast<size_t>(row), static_cast<size_t>(col), value) ? 1 : 0;
}

int32_t dl_store_get_i32(const dl_store* store, uint64_t row, uint64_t col, int32_t* out)
{
    if (!store || !out) return 0;
    return AsStore(store)->TryGet<int32_t>(static_cast<size_t>(row), static_cast<size_t>(col), *out) ? 1 : 0;
}

int32_t dl_store_set_f64(dl_store* store, uint64_t row, uint64_t col, double value)
{
    if (!store) return 0;
    return AsStore(store)->TrySet<double>(static_cast<size_t>(row), static_cast<size_t>(col), value) ? 1 : 0;
}

int32_t dl_store_get_f64(const dl_store* store, uint64_t row, uint64_t col, double* out)
{
    if (!store || !out) return 0;
    return AsStore(store)->TryGet<double>(static_cast<size_t>(row), static_cast<size_t>(col), *out) ? 1 : 0;
}

void dl_store_set_valid(dl_store* store, uint64_t row, int32_t valid)
{
    if (store) AsStore(store)->SetValid(static_cast<size_t>(row), valid != 0);
}

int32_t dl_store_is_valid(const dl_store* store, uint64_t row)
{
    return (store && AsStore(store)->IsValidRow(static_cast<size_t>(row))) ? 1 : 0;
}

void dl_store_set_lod(dl_store* store, uint64_t row, int32_t level)
{
    if (!store) return;
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    AsStore(store)->SetLod(static_cast<size_t>(row), static_cast<uint8_t>(level));
}

int32_t dl_store_get_lod(const dl_store* store, uint64_t row)
{
    return store ? static_cast<int32_t>(AsStore(store)->GetLod(static_cast<size_t>(row))) : 0;
}

uint64_t dl_store_alloc_row(dl_store* store)
{
    if (!store) return UINT64_MAX;
    size_t row = AsStore(store)->AllocRow();
    return (row == SIZE_MAX) ? UINT64_MAX : static_cast<uint64_t>(row);
}

void dl_store_free_row(dl_store* store, uint64_t row)
{
    if (store) AsStore(store)->FreeRow(static_cast<size_t>(row));
}

uint64_t dl_store_live_count(const dl_store* store)
{
    return store ? AsStore(store)->GetLiveCount() : 0;
}

uint64_t dl_store_run_f32(dl_store* store, uint64_t targetCol, int32_t op, float operand,
                          int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold)
{
    if (!store) return 0;
    return AsStore(store)->RunColumnSystem<float>(
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_store_run_i32(dl_store* store, uint64_t targetCol, int32_t op, int32_t operand,
                          int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold)
{
    if (!store) return 0;
    return AsStore(store)->RunColumnSystem<int32_t>(
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_store_run_col_f32(dl_store* store, uint64_t targetCol, int32_t op, uint64_t operandCol,
                              int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold)
{
    if (!store) return 0;
    return AsStore(store)->RunColumnSystemColumn<float>(
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_store_run_col_i32(dl_store* store, uint64_t targetCol, int32_t op, uint64_t operandCol,
                              int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold)
{
    if (!store) return 0;
    return AsStore(store)->RunColumnSystemColumn<int32_t>(
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

dl_lens* dl_lens_create(int32_t threadCount)
{
    const unsigned n = threadCount > 0 ? static_cast<unsigned>(threadCount) : 0u;
    return reinterpret_cast<dl_lens*>(new datalens::Lens(n));
}

void dl_lens_destroy(dl_lens* lens)
{
    delete AsLens(lens);
}

int32_t dl_lens_thread_count(const dl_lens* lens)
{
    return lens ? static_cast<int32_t>(AsLens(lens)->ThreadCount()) : 0;
}

uint64_t dl_lens_run_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op, float operand,
                         int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystem<float>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op, int32_t operand,
                         int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystem<int32_t>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_col_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                             uint64_t operandCol, int32_t hasPredicate, uint64_t compareCol,
                             int32_t cmp, float threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemColumn<float>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_col_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                             uint64_t operandCol, int32_t hasPredicate, uint64_t compareCol,
                             int32_t cmp, int32_t threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemColumn<int32_t>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_curved_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                uint64_t operandCol, int32_t curveType, float curveMin, float curveMax,
                                float curveP0, float curveP1, int32_t curveInvert,
                                int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold)
{
    if (!lens || !store) return 0;
    CurveSpec c;
    c.type = static_cast<DataCurveType>(curveType);
    c.min = curveMin; c.max = curveMax; c.p0 = curveP0; c.p1 = curveP1; c.invert = curveInvert != 0;
    return AsLens(lens)->RunSystemCurvedColumn<float>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        c, hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_curved_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                uint64_t operandCol, int32_t curveType, float curveMin, float curveMax,
                                float curveP0, float curveP1, int32_t curveInvert,
                                int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold)
{
    if (!lens || !store) return 0;
    CurveSpec c;
    c.type = static_cast<DataCurveType>(curveType);
    c.min = curveMin; c.max = curveMax; c.p0 = curveP0; c.p1 = curveP1; c.invert = curveInvert != 0;
    return AsLens(lens)->RunSystemCurvedColumn<int32_t>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        c, hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_noise_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                               float noiseLo, float noiseHi, uint64_t seed, uint64_t tick,
                               int32_t hasPredicate, uint64_t compareCol, int32_t cmp, float threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemNoiseColumn<float>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), noiseLo, noiseHi, seed, tick,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_noise_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                               int32_t noiseLo, int32_t noiseHi, uint64_t seed, uint64_t tick,
                               int32_t hasPredicate, uint64_t compareCol, int32_t cmp, int32_t threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemNoiseColumn<int32_t>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), noiseLo, noiseHi, seed, tick,
        hasPredicate != 0, static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_noise_perturb_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                       uint64_t operandCol, float noiseLo, float noiseHi, uint64_t seed,
                                       uint64_t tick, int32_t hasPredicate, uint64_t compareCol,
                                       int32_t cmp, float threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemNoisePerturbColumn<float>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        noiseLo, noiseHi, seed, tick, hasPredicate != 0, static_cast<size_t>(compareCol),
        static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_noise_perturb_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                       uint64_t operandCol, int32_t noiseLo, int32_t noiseHi, uint64_t seed,
                                       uint64_t tick, int32_t hasPredicate, uint64_t compareCol,
                                       int32_t cmp, int32_t threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemNoisePerturbColumn<int32_t>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), static_cast<size_t>(operandCol),
        noiseLo, noiseHi, seed, tick, hasPredicate != 0, static_cast<size_t>(compareCol),
        static_cast<DataCompareOp>(cmp), threshold);
}

uint64_t dl_lens_run_argmax_f32(dl_lens* lens, dl_store* store, uint64_t choiceCol,
                                const uint64_t* scoreCols, uint64_t scoreColCount,
                                float minScore, int32_t noChoice)
{
    if (!lens || !store) return 0;
    if (scoreColCount > 0 && !scoreCols) return 0;
    std::vector<size_t> cols(static_cast<size_t>(scoreColCount));
    for (uint64_t i = 0; i < scoreColCount; ++i) cols[static_cast<size_t>(i)] = static_cast<size_t>(scoreCols[i]);
    return AsLens(lens)->RunSystemArgmaxColumns<float>(*AsStore(store), static_cast<size_t>(choiceCol),
        cols.data(), cols.size(), minScore, noChoice);
}

uint64_t dl_lens_run_argmax_i32(dl_lens* lens, dl_store* store, uint64_t choiceCol,
                                const uint64_t* scoreCols, uint64_t scoreColCount,
                                int32_t minScore, int32_t noChoice)
{
    if (!lens || !store) return 0;
    if (scoreColCount > 0 && !scoreCols) return 0;
    std::vector<size_t> cols(static_cast<size_t>(scoreColCount));
    for (uint64_t i = 0; i < scoreColCount; ++i) cols[static_cast<size_t>(i)] = static_cast<size_t>(scoreCols[i]);
    return AsLens(lens)->RunSystemArgmaxColumns<int32_t>(*AsStore(store), static_cast<size_t>(choiceCol),
        cols.data(), cols.size(), minScore, noChoice);
}

uint64_t dl_lens_run_batch(dl_lens* lens, const dl_system_desc* descs, uint64_t count)
{
    if (!lens || (!descs && count != 0)) return 0;
    return AsLens(lens)->RunSystems(BuildDescs(descs, count, false, 0, 255));
}

uint64_t dl_lens_run_batch_lod(dl_lens* lens, const dl_system_desc* descs, uint64_t count,
                               int32_t minLod, int32_t maxLod)
{
    if (!lens || (!descs && count != 0)) return 0;
    if (minLod < 0)   minLod = 0;
    if (minLod > 255) minLod = 255;
    if (maxLod < 0)   maxLod = 0;
    if (maxLod > 255) maxLod = 255;
    return AsLens(lens)->RunSystems(
        BuildDescs(descs, count, true, static_cast<uint8_t>(minLod), static_cast<uint8_t>(maxLod)));
}

// ── Query/Update IR (A4) ──────────────────────────────────────────────────

dl_ir_program* dl_ir_create(void)
{
    return reinterpret_cast<dl_ir_program*>(new datalens::IrProgram());
}

void dl_ir_destroy(dl_ir_program* program)
{
    delete AsProgram(program);
}

void dl_ir_add_system(dl_ir_program* program, const dl_ir_op* op)
{
    if (!program || !op) return;
    datalens::IrSystemOp o;
    o.storeIndex      = static_cast<uint32_t>(op->store_index);
    o.elemType        = static_cast<DataLensValueType>(op->elem_type);
    o.targetCol       = static_cast<uint32_t>(op->target_col);
    o.op              = static_cast<DataSystemOp>(op->op);
    o.operandIsColumn = static_cast<uint32_t>(op->operand_is_column);
    o.operandCol      = static_cast<uint32_t>(op->operand_col);
    o.hasPredicate    = static_cast<uint32_t>(op->has_predicate);
    o.compareCol      = static_cast<uint32_t>(op->compare_col);
    o.cmp             = static_cast<DataCompareOp>(op->cmp);
    o.minLod          = ClampLod(op->min_lod);
    o.maxLod          = ClampLod(op->max_lod);
    o.operand         = op->operand;
    o.threshold       = op->threshold;
    o.applyCurve      = static_cast<uint32_t>(op->apply_curve);
    o.curveType       = op->curve_type;
    o.curveInvert     = static_cast<uint32_t>(op->curve_invert);
    o.curveMin        = op->curve_min;
    o.curveMax        = op->curve_max;
    o.curveP0         = op->curve_p0;
    o.curveP1         = op->curve_p1;
    AsProgram(program)->Add(o);
}

uint64_t dl_ir_count(const dl_ir_program* program)
{
    return program ? AsProgram(program)->Count() : 0;
}

uint64_t dl_ir_serialize(const dl_ir_program* program, uint8_t* buf, uint64_t bufLen)
{
    if (!program) return 0;
    const std::vector<uint8_t> bytes = AsProgram(program)->Serialize();
    if (buf != nullptr)
    {
        const uint64_t n = bytes.size() < bufLen ? bytes.size() : bufLen;
        std::memcpy(buf, bytes.data(), static_cast<size_t>(n));
    }
    return bytes.size();
}

dl_ir_program* dl_ir_deserialize(const uint8_t* data, uint64_t size)
{
    auto* program = new datalens::IrProgram();
    if (!datalens::IrProgram::Deserialize(data, static_cast<size_t>(size), *program))
    {
        delete program;
        return nullptr;
    }
    return reinterpret_cast<dl_ir_program*>(program);
}

uint64_t dl_lens_execute(dl_lens* lens, const dl_ir_program* program,
                         dl_store* const* stores, uint64_t storeCount)
{
    if (!lens || !program) return 0;
    return AsLens(lens)->Execute(*AsProgram(program), AsStoreTable(stores), static_cast<size_t>(storeCount));
}

// ── Tick / cadence scheduler (A5) ──────────────────────────────────────────

uint64_t dl_lens_add_scheduled_program(dl_lens* lens, const dl_ir_program* program,
                                       uint64_t period, int32_t minLod, int32_t maxLod, uint64_t phase)
{
    if (!lens || !program) return 0;
    return AsLens(lens)->AddScheduledProgram(*AsProgram(program), period, ClampLod(minLod), ClampLod(maxLod), phase);
}

void dl_lens_clear_schedule(dl_lens* lens)
{
    if (lens) AsLens(lens)->ClearSchedule();
}

uint64_t dl_lens_scheduled_program_count(const dl_lens* lens)
{
    return lens ? AsLens(lens)->ScheduledProgramCount() : 0;
}

uint64_t dl_lens_add_scheduled_view(dl_lens* lens, struct dl_view* view, uint64_t storeIndex,
                                    uint64_t period, uint64_t phase)
{
    if (!lens) return 0;
    return AsLens(lens)->AddScheduledView(AsView(view), static_cast<size_t>(storeIndex), period, phase);
}

uint64_t dl_lens_add_scheduled_view_lod(dl_lens* lens, struct dl_view* view, uint64_t storeIndex,
                                        uint64_t period, int32_t minLod, int32_t maxLod, uint64_t phase)
{
    if (!lens) return 0;
    return AsLens(lens)->AddScheduledViewInLodBand(AsView(view), static_cast<size_t>(storeIndex),
        period, ClampLod(minLod), ClampLod(maxLod), phase);
}

void dl_lens_clear_scheduled_views(dl_lens* lens)
{
    if (lens) AsLens(lens)->ClearScheduledViews();
}

uint64_t dl_lens_scheduled_view_count(const dl_lens* lens)
{
    return lens ? AsLens(lens)->ScheduledViewCount() : 0;
}

uint64_t dl_lens_current_tick(const dl_lens* lens)
{
    return lens ? AsLens(lens)->CurrentTick() : 0;
}

void dl_lens_reset_tick(dl_lens* lens, uint64_t tick)
{
    if (lens) AsLens(lens)->ResetTick(tick);
}

uint64_t dl_lens_tick(dl_lens* lens, dl_store* const* stores, uint64_t storeCount)
{
    if (!lens) return 0;
    return AsLens(lens)->Tick(AsStoreTable(stores), static_cast<size_t>(storeCount));
}

void dl_lens_refresh_view(dl_lens* lens, struct dl_view* view, const dl_store* store)
{
    if (lens && view && store) AsLens(lens)->RefreshView(*AsView(view), *AsStore(store));
}

void dl_lens_refresh_view_lod(dl_lens* lens, struct dl_view* view, const dl_store* store,
                              int32_t minLod, int32_t maxLod)
{
    if (lens && view && store)
        AsLens(lens)->RefreshView(*AsView(view), *AsStore(store), true, ClampLod(minLod), ClampLod(maxLod));
}

uint64_t dl_lens_run_f32_pred_i32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                  float operand, uint64_t compareCol, int32_t cmp, int32_t threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemTypedPred<float>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp),
        DataLensValueType::Int32, static_cast<double>(threshold));
}

uint64_t dl_lens_run_i32_pred_f32(dl_lens* lens, dl_store* store, uint64_t targetCol, int32_t op,
                                  int32_t operand, uint64_t compareCol, int32_t cmp, float threshold)
{
    if (!lens || !store) return 0;
    return AsLens(lens)->RunSystemTypedPred<int32_t>(*AsStore(store),
        static_cast<size_t>(targetCol), static_cast<DataSystemOp>(op), operand,
        static_cast<size_t>(compareCol), static_cast<DataCompareOp>(cmp),
        DataLensValueType::Float, static_cast<double>(threshold));
}

// ── Read-only DataView (A5) ─────────────────────────────────────────────────

dl_view* dl_view_create(const uint64_t* sourceColumns, uint64_t columnCount)
{
    std::vector<size_t> cols;
    cols.reserve(static_cast<size_t>(columnCount));
    for (uint64_t i = 0; i < columnCount; ++i)
        cols.push_back(static_cast<size_t>(sourceColumns ? sourceColumns[i] : 0));
    return reinterpret_cast<dl_view*>(new datalens::DataView(std::move(cols)));
}

void dl_view_destroy(dl_view* view)
{
    delete AsView(view);
}

void dl_view_refresh(dl_view* view, const dl_store* store)
{
    if (view && store) AsView(view)->Refresh(*AsStore(store));
}

void dl_view_refresh_lod(dl_view* view, const dl_store* store, int32_t minLod, int32_t maxLod)
{
    if (view && store) AsView(view)->RefreshInLodBand(*AsStore(store), ClampLod(minLod), ClampLod(maxLod));
}

uint64_t dl_view_row_count(const dl_view* view)    { return view ? AsView(view)->RowCount() : 0; }
uint64_t dl_view_column_count(const dl_view* view) { return view ? AsView(view)->ColumnCount() : 0; }
uint64_t dl_view_row_stride(const dl_view* view)   { return view ? AsView(view)->RowStride() : 0; }

uint64_t dl_view_source_row(const dl_view* view, uint64_t viewRow)
{
    return (view && viewRow < AsView(view)->RowCount()) ? AsView(view)->SourceRow(static_cast<size_t>(viewRow)) : 0;
}

int32_t dl_view_get_f32(const dl_view* view, uint64_t viewRow, uint64_t viewCol, float* out)
{
    if (!view || !out) return 0;
    const datalens::DataView* v = AsView(view);
    if (viewRow >= v->RowCount() || viewCol >= v->ColumnCount()) return 0;
    *out = v->Get<float>(static_cast<size_t>(viewRow), static_cast<size_t>(viewCol));
    return 1;
}

int32_t dl_view_get_i32(const dl_view* view, uint64_t viewRow, uint64_t viewCol, int32_t* out)
{
    if (!view || !out) return 0;
    const datalens::DataView* v = AsView(view);
    if (viewRow >= v->RowCount() || viewCol >= v->ColumnCount()) return 0;
    *out = v->Get<int32_t>(static_cast<size_t>(viewRow), static_cast<size_t>(viewCol));
    return 1;
}

int32_t dl_view_get_f64(const dl_view* view, uint64_t viewRow, uint64_t viewCol, double* out)
{
    if (!view || !out) return 0;
    const datalens::DataView* v = AsView(view);
    if (viewRow >= v->RowCount() || viewCol >= v->ColumnCount()) return 0;
    *out = v->Get<double>(static_cast<size_t>(viewRow), static_cast<size_t>(viewCol));
    return 1;
}

const void* dl_view_data(const dl_view* view)
{
    return view ? static_cast<const void*>(AsView(view)->Data()) : nullptr;
}

uint64_t dl_view_byte_size(const dl_view* view)
{
    return view ? AsView(view)->ByteSize() : 0;
}

/* ---- The read/write View (§6.4) ---- */

dl_rwview* dl_rwview_create(uint64_t baseStore,
                            const dl_view_join* joins, int32_t joinCount,
                            const dl_view_column* columns, int32_t columnCount,
                            const dl_view_scope* scope, int32_t scopeCount)
{
    std::vector<datalens::ViewJoin> j;
    for (int32_t i = 0; joins && i < joinCount; ++i)
    {
        datalens::ViewJoin vj;
        vj.TargetStore = static_cast<size_t>(joins[i].target_store);
        vj.Aligned = joins[i].aligned != 0;
        vj.IndexColumn = static_cast<size_t>(joins[i].index_column);
        vj.AbsentSentinel = joins[i].absent_sentinel;
        j.push_back(vj);
    }
    std::vector<datalens::ViewColumn> c;
    for (int32_t i = 0; columns && i < columnCount; ++i)
        c.push_back({static_cast<size_t>(columns[i].source), static_cast<size_t>(columns[i].column)});
    std::vector<datalens::ViewScope> s;
    for (int32_t i = 0; scope && i < scopeCount; ++i)
    {
        datalens::ViewScope vs;
        vs.Column = static_cast<size_t>(scope[i].column);
        vs.Type = static_cast<DataLensValueType>(scope[i].type);
        vs.Op = static_cast<DataCompareOp>(scope[i].op);
        vs.IValue = scope[i].ivalue;
        vs.DValue = scope[i].dvalue;
        s.push_back(vs);
    }
    return reinterpret_cast<dl_rwview*>(
        new datalens::View(static_cast<size_t>(baseStore), std::move(j), std::move(c), std::move(s)));
}

void dl_rwview_destroy(dl_rwview* view) { delete AsRwView(view); }

void dl_rwview_set_writeback(dl_rwview* view,
                             const dl_view_write* insert, int32_t insertCount,
                             const dl_view_write* update, int32_t updateCount,
                             const uint64_t* deleteStores, int32_t deleteCount)
{
    if (!view) return;
    datalens::ViewWriteBack wb;
    for (int32_t i = 0; insert && i < insertCount; ++i)
        wb.Insert.push_back({static_cast<size_t>(insert[i].view_column),
                             static_cast<size_t>(insert[i].target_store),
                             static_cast<size_t>(insert[i].target_column)});
    for (int32_t i = 0; update && i < updateCount; ++i)
        wb.Update.push_back({static_cast<size_t>(update[i].view_column),
                             static_cast<size_t>(update[i].target_store),
                             static_cast<size_t>(update[i].target_column)});
    for (int32_t i = 0; deleteStores && i < deleteCount; ++i)
        wb.Delete.push_back(static_cast<size_t>(deleteStores[i]));
    AsRwView(view)->SetWriteBack(std::move(wb));
}

void dl_rwview_set_scope_program(dl_rwview* view, const dl_view_predicate* preds, int32_t predCount)
{
    if (!view) return;
    std::vector<datalens::ViewPredicate> prog;
    for (int32_t i = 0; preds && i < predCount; ++i)
    {
        datalens::ViewPredicate p;
        p.Kind   = static_cast<datalens::ViewPredicateKind>(preds[i].kind);
        p.Range  = preds[i].is_range != 0;
        p.Source = static_cast<size_t>(preds[i].source);
        p.Column = static_cast<size_t>(preds[i].column);
        p.Type   = static_cast<DataLensValueType>(preds[i].type);
        p.Op     = static_cast<DataCompareOp>(preds[i].op);
        p.IValue = preds[i].ivalue;
        p.IHi    = preds[i].ivalue_hi;
        p.DValue = preds[i].dvalue;
        p.DHi    = preds[i].dvalue_hi;
        prog.push_back(p);
    }
    AsRwView(view)->SetScopeProgram(std::move(prog));
}

void dl_rwview_refresh(dl_rwview* view, const dl_store* const* stores, int32_t storeCount)
{
    if (!view) return;
    std::vector<const DataStore*> t;
    t.reserve(static_cast<size_t>(storeCount));
    for (int32_t i = 0; stores && i < storeCount; ++i)
        t.push_back(AsStore(stores[i]));
    AsRwView(view)->Refresh(t);
}

uint64_t dl_rwview_commit(dl_rwview* view, dl_store* const* stores, int32_t storeCount)
{
    if (!view) return 0;
    std::vector<DataStore*> t;
    t.reserve(static_cast<size_t>(storeCount));
    for (int32_t i = 0; stores && i < storeCount; ++i)
        t.push_back(AsStore(stores[i]));
    return AsRwView(view)->Commit(t);
}

uint64_t dl_rwview_row_count(const dl_rwview* view)    { return view ? AsRwView(view)->RowCount() : 0; }
uint64_t dl_rwview_column_count(const dl_rwview* view) { return view ? AsRwView(view)->ColumnCount() : 0; }
uint64_t dl_rwview_row_stride(const dl_rwview* view)   { return view ? AsRwView(view)->RowStride() : 0; }
uint64_t dl_rwview_byte_size(const dl_rwview* view)    { return view ? AsRwView(view)->ByteSize() : 0; }
const uint8_t* dl_rwview_data(const dl_rwview* view)   { return view ? AsRwView(view)->Data() : nullptr; }
uint8_t* dl_rwview_mutable_data(dl_rwview* view)       { return view ? AsRwView(view)->MutableRowData(0) : nullptr; }

uint64_t dl_rwview_column_offset(const dl_rwview* view, uint64_t col)
{
    return (view && col < AsRwView(view)->ColumnCount()) ? AsRwView(view)->ColumnOffset(static_cast<size_t>(col)) : 0;
}
uint64_t dl_rwview_column_stride(const dl_rwview* view, uint64_t col)
{
    return (view && col < AsRwView(view)->ColumnCount()) ? AsRwView(view)->ColumnStride(static_cast<size_t>(col)) : 0;
}

uint8_t dl_rwview_get_state(const dl_rwview* view, uint64_t viewRow)
{
    return (view && viewRow < AsRwView(view)->RowCount())
        ? static_cast<uint8_t>(AsRwView(view)->State(static_cast<size_t>(viewRow))) : 0;
}
void dl_rwview_set_state(dl_rwview* view, uint64_t viewRow, uint8_t state)
{
    if (view && viewRow < AsRwView(view)->RowCount())
        AsRwView(view)->SetState(static_cast<size_t>(viewRow), static_cast<datalens::ViewRowState>(state));
}
uint64_t dl_rwview_add_row(dl_rwview* view) { return view ? AsRwView(view)->AddRow() : 0; }

uint64_t dl_rwview_source_base_row(const dl_rwview* view, uint64_t viewRow)
{
    return (view && viewRow < AsRwView(view)->RowCount())
        ? AsRwView(view)->SourceBaseRow(static_cast<size_t>(viewRow)) : datalens::View::NoRow;
}
uint64_t dl_rwview_source_join_row(const dl_rwview* view, uint64_t viewRow, uint64_t join)
{
    return (view && viewRow < AsRwView(view)->RowCount())
        ? AsRwView(view)->SourceJoinRow(static_cast<size_t>(viewRow), static_cast<size_t>(join)) : datalens::View::NoRow;
}

uint64_t dl_lens_add_scheduled_rwview(dl_lens* lens, dl_rwview* view, uint64_t period, uint64_t phase)
{
    return (lens && view) ? AsLens(lens)->AddScheduledRwView(AsRwView(view), period, phase) : 0;
}
void dl_lens_clear_scheduled_rwviews(dl_lens* lens) { if (lens) AsLens(lens)->ClearScheduledRwViews(); }
uint64_t dl_lens_scheduled_rwview_count(const dl_lens* lens) { return lens ? AsLens(lens)->ScheduledRwViewCount() : 0; }

} // extern "C"

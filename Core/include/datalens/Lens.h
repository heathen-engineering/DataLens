/******************************************************************************
 * Lens.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * The Lens is the orchestration system: it owns the worker pool and RUNS Systems
 * over Stores. This is the first slice — running a single conditional column
 * System in parallel by chunking its row range across the pool. View refresh and
 * commit consolidation arrive in later phases.
 ******************************************************************************/

#pragma once

#include "datalens/DataStore.h"
#include "datalens/DataView.h"
#include "datalens/Ir.h"
#include "datalens/ThreadPool.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace datalens
{
    /// <summary>
    /// A data-described System: one column transform the Lens can run. This is the unit the Lens
    /// schedules, batches, and (later) compiles to/from the IR — "a System as data" rather than a
    /// function call. The operand is either a scalar (<c>operand</c>) or read per-row from
    /// <c>operandCol</c> (<c>operandIsColumn</c>); an optional predicate gates which rows apply.
    /// Scalar fields are carried as <c>double</c> and cast to the element type at execution
    /// (Int32 fits exactly; Float is exact).
    /// </summary>
    struct SystemDesc
    {
        DataStore*        store          = nullptr;
        DataLensValueType elemType       = DataLensValueType::Int32; // Int32 / Float supported
        std::size_t       targetCol      = 0;
        DataSystemOp      op             = DataSystemOp::Set;
        bool              operandIsColumn = false;
        std::size_t       operandCol     = 0;
        double            operand        = 0.0;
        // When set (cross-column only), the per-row operand is operandCol[r] * operandScale — the
        // fused-multiply primitive (e.g. pos += vel * dt). Default scale 1 leaves cross-column ops
        // unchanged even when scaleOperand is toggled.
        bool              scaleOperand   = false;
        double            operandScale   = 1.0;
        bool              hasPredicate   = false;
        std::size_t       compareCol     = 0;
        DataCompareOp     cmp            = DataCompareOp::Always;
        double            threshold      = 0.0;
        // Simulation LOD band the System applies to: only rows with lod in [minLod, maxLod] are
        // affected. The default 0..255 means "all rows" (no LOD scoping).
        std::uint8_t      minLod         = 0;
        std::uint8_t      maxLod         = 255;
    };

    /// <summary>
    /// An IR program registered with the Lens to run on a cadence (A5 tick scheduling). It runs on
    /// ticks where <c>tick % period == phase % period</c>, scoped to the LOD band [minLod, maxLod].
    /// This is how Simulation LOD becomes execution *frequency*: register the LOD-0 systems at period
    /// 1 (every tick), the LOD-1 systems at period 4, LOD-2 at period 16, etc. The band override means
    /// each tier's program only touches rows at that relevance (see DataStore::SetLod).
    /// </summary>
    struct ScheduledProgram
    {
        IrProgram    program;
        std::uint64_t period = 1;   // run every `period` ticks (clamped to >= 1)
        std::uint64_t phase  = 0;   // tick offset within the period
        std::uint8_t  minLod = 0;
        std::uint8_t  maxLod = 255;
    };

    /// <summary>
    /// A read-only view registered with the Lens to refresh on a cadence (A5). The Lens does NOT own
    /// the view (the caller owns and reads it); it just re-materialises it from store
    /// <c>storeIndex</c> on ticks where <c>tick % period == phase % period</c>, optionally restricted
    /// to a LOD band. Refresh happens AFTER this tick's Systems run, so the view reflects the new
    /// state (run Systems → refresh Views).
    /// </summary>
    struct ScheduledView
    {
        DataView*     view       = nullptr; // non-owning
        std::size_t   storeIndex = 0;       // store in the table to refresh from
        std::uint64_t period     = 1;
        std::uint64_t phase      = 0;
        bool          useBand    = false;
        std::uint8_t  minLod     = 0;
        std::uint8_t  maxLod     = 255;
    };

    class Lens
    {
    public:
        /// threadCount == 0 uses hardware_concurrency.
        explicit Lens(unsigned threadCount = 0) : mPool(threadCount) {}

        unsigned ThreadCount() const { return mPool.ThreadCount(); }

        /// Run a conditional column System over the store, parallelised across the Lens's threads.
        /// Each thread processes a disjoint row range (race-free); the result is identical to the
        /// single-threaded run regardless of thread count (rows are independent). Returns rows affected.
        template <typename T>
        std::size_t RunSystem(DataStore& store, std::size_t targetCol, DataSystemOp op, T operand,
                              bool hasPredicate, std::size_t compareCol, DataCompareOp cmp, T threshold)
        {
            const std::size_t rows = store.GetRowCount();
            std::atomic<std::size_t> affected{0};

            mPool.ParallelFor(rows, kMinChunk, [&](std::size_t begin, std::size_t end) {
                const std::size_t a = store.RunColumnSystemChunk<T>(
                    begin, end, targetCol, op, operand, hasPredicate, compareCol, cmp, threshold);
                affected.fetch_add(a, std::memory_order_relaxed);
            });

            return affected.load(std::memory_order_relaxed);
        }

        /// Run a cross-column System (operand read per-row from operandCol) over the store,
        /// parallelised across the Lens's threads. Same disjoint-range / determinism guarantees as
        /// RunSystem. Returns rows affected.
        template <typename T>
        std::size_t RunSystemColumn(DataStore& store, std::size_t targetCol, DataSystemOp op, std::size_t operandCol,
                                    bool hasPredicate, std::size_t compareCol, DataCompareOp cmp, T threshold)
        {
            const std::size_t rows = store.GetRowCount();
            std::atomic<std::size_t> affected{0};

            mPool.ParallelFor(rows, kMinChunk, [&](std::size_t begin, std::size_t end) {
                const std::size_t a = store.RunColumnSystemColumnChunk<T>(
                    begin, end, targetCol, op, operandCol, hasPredicate, compareCol, cmp, threshold);
                affected.fetch_add(a, std::memory_order_relaxed);
            });

            return affected.load(std::memory_order_relaxed);
        }

        /// Run a scalar System over only the rows whose Simulation LOD is in [minLod, maxLod],
        /// parallelised across the Lens's threads. Returns rows affected.
        template <typename T>
        std::size_t RunSystemInLodBand(DataStore& store, std::size_t targetCol, DataSystemOp op, T operand,
                                       bool hasPredicate, std::size_t compareCol, DataCompareOp cmp, T threshold,
                                       std::uint8_t minLod, std::uint8_t maxLod)
        {
            const std::size_t rows = store.GetRowCount();
            std::atomic<std::size_t> affected{0};

            mPool.ParallelFor(rows, kMinChunk, [&](std::size_t begin, std::size_t end) {
                const std::size_t a = store.RunColumnSystemInLodBandChunk<T>(
                    begin, end, targetCol, op, operand, hasPredicate, compareCol, cmp, threshold, minLod, maxLod);
                affected.fetch_add(a, std::memory_order_relaxed);
            });

            return affected.load(std::memory_order_relaxed);
        }

        /// Cross-column form of RunSystemInLodBand (operand read per-row from operandCol).
        template <typename T>
        std::size_t RunSystemColumnInLodBand(DataStore& store, std::size_t targetCol, DataSystemOp op,
                                             std::size_t operandCol, bool hasPredicate, std::size_t compareCol,
                                             DataCompareOp cmp, T threshold, std::uint8_t minLod, std::uint8_t maxLod)
        {
            const std::size_t rows = store.GetRowCount();
            std::atomic<std::size_t> affected{0};

            mPool.ParallelFor(rows, kMinChunk, [&](std::size_t begin, std::size_t end) {
                const std::size_t a = store.RunColumnSystemColumnInLodBandChunk<T>(
                    begin, end, targetCol, op, operandCol, hasPredicate, compareCol, cmp, threshold, minLod, maxLod);
                affected.fetch_add(a, std::memory_order_relaxed);
            });

            return affected.load(std::memory_order_relaxed);
        }

        /// Scaled cross-column System (fused multiply: rhs = operandCol[r] * scale), parallelised.
        template <typename T>
        std::size_t RunSystemScaledColumn(DataStore& store, std::size_t targetCol, DataSystemOp op,
                                          std::size_t operandCol, T scale, bool hasPredicate,
                                          std::size_t compareCol, DataCompareOp cmp, T threshold)
        {
            const std::size_t rows = store.GetRowCount();
            std::atomic<std::size_t> affected{0};

            mPool.ParallelFor(rows, kMinChunk, [&](std::size_t begin, std::size_t end) {
                const std::size_t a = store.RunColumnSystemScaledColumnChunk<T>(
                    begin, end, targetCol, op, operandCol, scale, hasPredicate, compareCol, cmp, threshold);
                affected.fetch_add(a, std::memory_order_relaxed);
            });

            return affected.load(std::memory_order_relaxed);
        }

        /// Run a batch of data-described Systems (across any number of stores) in one call and return
        /// the total rows affected. The Lens assigns each System a dependency LEVEL — one past the
        /// highest level of any earlier System it conflicts with (a write/read hazard on the same
        /// (store,column)) — and runs each level's Systems CONCURRENTLY across the pool. Because every
        /// conflicting pair lands in strictly increasing, submission-ordered levels while independent
        /// Systems share a level, the result is deterministic and identical to running the batch
        /// sequentially. This packs more parallelism than consecutive grouping: e.g. writes to col0,
        /// col0, col1, col1 schedule as {0:[op0,op2], 1:[op1,op3]} (2 levels), not 3. A level of one
        /// System is itself parallelised by row.
        std::size_t RunSystems(const SystemDesc* descs, std::size_t count)
        {
            if (count == 0) return 0;

            // level[j] = 1 + max(level[i]) over earlier conflicting i (0 if none). O(n^2) in the batch
            // size, which is small; the per-row work dwarfs it.
            std::vector<std::size_t> level(count, 0);
            std::size_t maxLevel = 0;
            for (std::size_t j = 0; j < count; ++j)
            {
                std::size_t lv = 0;
                for (std::size_t i = 0; i < j; ++i)
                    if (DescsConflict(descs[i], descs[j]) && level[i] + 1 > lv)
                        lv = level[i] + 1;
                level[j] = lv;
                if (lv > maxLevel) maxLevel = lv;
            }

            std::size_t total = 0;
            for (std::size_t L = 0; L <= maxLevel; ++L)
            {
                // The ops at this level are mutually conflict-free, so run each one ROW-parallel across
                // the whole pool, sequentially. This keeps every core busy even when a level has only a
                // few (large) Systems — the typical workload — rather than stranding the work on
                // one-op-per-thread (which left most cores idle). Order within a level is irrelevant (no
                // conflicts), so the result stays deterministic == sequential submission order.
                for (std::size_t k = 0; k < count; ++k)
                    if (level[k] == L)
                        total += RunOneParallel(descs[k]);
            }

            return total;
        }

        std::size_t RunSystems(const std::vector<SystemDesc>& descs)
        {
            return RunSystems(descs.data(), descs.size());
        }

        /// Execute an IR program (A4): resolve each op's store INDEX against the supplied store table,
        /// then run the resulting Systems via the batch/wave executor. An op whose storeIndex is out of
        /// range resolves to a null store and contributes nothing (validated once here; the kernels
        /// already bounds-check columns). Returns total rows affected.
        std::size_t Execute(const IrProgram& program, DataStore* const* stores, std::size_t storeCount)
        {
            return RunSystems(BuildExecDescs(program, stores, storeCount, false, 0, 255));
        }

        /// As Execute, but force every op to the LOD band [minLod, maxLod] (the per-tick fidelity model
        /// the scheduler uses). Overrides any per-op band on the IR.
        std::size_t ExecuteInLodBand(const IrProgram& program, DataStore* const* stores,
                                     std::size_t storeCount, std::uint8_t minLod, std::uint8_t maxLod)
        {
            return RunSystems(BuildExecDescs(program, stores, storeCount, true, minLod, maxLod));
        }

        // ── Tick / cadence scheduler (A5) ────────────────────────────────────
        // The Lens owns a tick counter and a set of scheduled programs; each Tick advances the counter
        // and runs every program due this tick (period/phase) over its LOD band. This turns Simulation
        // LOD into execution frequency: coarser tiers run on fewer ticks.

        /// Register a program to run on a cadence. Returns its index in the schedule. period < 1 is
        /// clamped to 1 (every tick).
        std::size_t AddScheduledProgram(const IrProgram& program, std::uint64_t period,
                                        std::uint8_t minLod = 0, std::uint8_t maxLod = 255,
                                        std::uint64_t phase = 0)
        {
            ScheduledProgram s;
            s.program = program;
            s.period  = period < 1 ? 1 : period;
            s.phase   = phase;
            s.minLod  = minLod;
            s.maxLod  = maxLod;
            mSchedule.push_back(std::move(s));
            return mSchedule.size() - 1;
        }

        void ClearSchedule() { mSchedule.clear(); }
        std::size_t ScheduledProgramCount() const { return mSchedule.size(); }

        /// Register a read-only view to refresh on a cadence (refreshes all live rows of the store).
        /// The Lens does not own the view. Returns its index in the view schedule.
        std::size_t AddScheduledView(DataView* view, std::size_t storeIndex, std::uint64_t period,
                                     std::uint64_t phase = 0)
        {
            ScheduledView v;
            v.view = view; v.storeIndex = storeIndex;
            v.period = period < 1 ? 1 : period; v.phase = phase;
            mScheduledViews.push_back(v);
            return mScheduledViews.size() - 1;
        }

        /// As AddScheduledView, but each refresh is restricted to the LOD band [minLod, maxLod].
        std::size_t AddScheduledViewInLodBand(DataView* view, std::size_t storeIndex, std::uint64_t period,
                                              std::uint8_t minLod, std::uint8_t maxLod, std::uint64_t phase = 0)
        {
            ScheduledView v;
            v.view = view; v.storeIndex = storeIndex;
            v.period = period < 1 ? 1 : period; v.phase = phase;
            v.useBand = true; v.minLod = minLod; v.maxLod = maxLod;
            mScheduledViews.push_back(v);
            return mScheduledViews.size() - 1;
        }

        void ClearScheduledViews() { mScheduledViews.clear(); }
        std::size_t ScheduledViewCount() const { return mScheduledViews.size(); }

        std::uint64_t CurrentTick() const { return mTick; }
        void ResetTick(std::uint64_t tick = 0) { mTick = tick; }

        /// Advance one tick: first run every scheduled program due this tick (registration order, each
        /// internally parallel, scoped to its LOD band), THEN refresh every scheduled view due this
        /// tick — so views reflect this tick's mutations (run Systems → refresh Views). Returns the
        /// total rows affected by the Systems this tick (view refresh is a read, not counted).
        std::size_t Tick(DataStore* const* stores, std::size_t storeCount)
        {
            ++mTick;

            std::size_t affected = 0;
            for (const ScheduledProgram& s : mSchedule)
                if (mTick % s.period == s.phase % s.period)
                    affected += ExecuteInLodBand(s.program, stores, storeCount, s.minLod, s.maxLod);

            for (const ScheduledView& v : mScheduledViews)
            {
                if (v.view == nullptr || v.storeIndex >= storeCount) continue;
                if (mTick % v.period != v.phase % v.period) continue;
                RefreshView(*v.view, *stores[v.storeIndex], v.useBand, v.minLod, v.maxLod);
            }

            return affected;
        }

        /// Refresh a view from a store, materialising it in PARALLEL across the Lens's pool (the live-row
        /// selection is a cheap serial pass; the per-column copy is chunked by view-row range). Result is
        /// identical to DataView::Refresh. Use this instead of DataView::Refresh when the view is large.
        void RefreshView(DataView& view, const DataStore& store,
                         bool useBand = false, std::uint8_t minLod = 0, std::uint8_t maxLod = 255)
        {
            view.PrepareRefresh(store, useBand, minLod, maxLod);
            const std::size_t rows = view.RowCount();
            mPool.ParallelFor(rows, kMinChunk, [&](std::size_t begin, std::size_t end) {
                view.MaterializeRange(store, begin, end);
            });
        }

    private:
        // Build SystemDescs from an IR program against the store table. When applyBand is true, every
        // op's LOD band is overridden to [bandMin, bandMax]; otherwise each op keeps its own band.
        std::vector<SystemDesc> BuildExecDescs(const IrProgram& program, DataStore* const* stores,
                                               std::size_t storeCount, bool applyBand,
                                               std::uint8_t bandMin, std::uint8_t bandMax)
        {
            const std::vector<IrSystemOp>& ops = program.Systems();
            std::vector<SystemDesc> descs;
            descs.reserve(ops.size());
            for (const IrSystemOp& o : ops)
            {
                SystemDesc d;
                d.store           = (o.storeIndex < storeCount) ? stores[o.storeIndex] : nullptr;
                d.elemType        = o.elemType;
                d.targetCol       = o.targetCol;
                d.op              = o.op;
                d.operandIsColumn = o.operandIsColumn != 0;
                d.operandCol      = o.operandCol;
                d.operand         = o.operand;
                d.hasPredicate    = o.hasPredicate != 0;
                d.compareCol      = o.compareCol;
                d.cmp             = o.cmp;
                d.threshold       = o.threshold;
                d.minLod          = applyBand ? bandMin : o.minLod;
                d.maxLod          = applyBand ? bandMax : o.maxLod;
                descs.push_back(d);
            }
            return descs;
        }

        // Does System d touch column `col` of its store (as its read-modify-write target, its operand
        // column, or its predicate column)?
        static bool DescTouchesColumn(std::size_t col, const SystemDesc& d)
        {
            return col == d.targetCol
                || (d.operandIsColumn && col == d.operandCol)
                || (d.hasPredicate && col == d.compareCol);
        }

        // Two Systems conflict when they share a store and one's WRITE (its target column) hits any
        // column the other accesses — i.e. a write/write or write/read hazard. Read-read never
        // conflicts. Different stores never conflict.
        static bool DescsConflict(const SystemDesc& a, const SystemDesc& b)
        {
            if (a.store != b.store) return false;
            return DescTouchesColumn(a.targetCol, b) || DescTouchesColumn(b.targetCol, a);
        }

        static bool IsBanded(const SystemDesc& d) { return !(d.minLod == 0 && d.maxLod == 255); }

        // Run one System parallelised by row across the pool.
        template <typename T>
        std::size_t RunOneParallelTyped(const SystemDesc& d)
        {
            const T thr = static_cast<T>(d.threshold);
            if (d.operandIsColumn && d.scaleOperand && !IsBanded(d))
                return RunSystemScaledColumn<T>(*d.store, d.targetCol, d.op, d.operandCol,
                    static_cast<T>(d.operandScale), d.hasPredicate, d.compareCol, d.cmp, thr);
            if (IsBanded(d))
            {
                if (d.operandIsColumn)
                    return RunSystemColumnInLodBand<T>(*d.store, d.targetCol, d.op, d.operandCol,
                        d.hasPredicate, d.compareCol, d.cmp, thr, d.minLod, d.maxLod);
                return RunSystemInLodBand<T>(*d.store, d.targetCol, d.op, static_cast<T>(d.operand),
                    d.hasPredicate, d.compareCol, d.cmp, thr, d.minLod, d.maxLod);
            }
            if (d.operandIsColumn)
                return RunSystemColumn<T>(*d.store, d.targetCol, d.op, d.operandCol,
                    d.hasPredicate, d.compareCol, d.cmp, thr);
            return RunSystem<T>(*d.store, d.targetCol, d.op, static_cast<T>(d.operand),
                d.hasPredicate, d.compareCol, d.cmp, thr);
        }

        std::size_t RunOneParallel(const SystemDesc& d)
        {
            if (d.store == nullptr) return 0;
            switch (d.elemType)
            {
            case DataLensValueType::Int32: return RunOneParallelTyped<int32_t>(d);
            case DataLensValueType::Float: return RunOneParallelTyped<float>(d);
            default: return 0;
            }
        }

        // Don't split below this many rows per chunk: keeps per-chunk overhead amortised and
        // limits false sharing at chunk boundaries.
        static constexpr std::size_t kMinChunk = 2048;

        ThreadPool mPool;

        // Tick scheduler state (A5).
        std::uint64_t mTick = 0;
        std::vector<ScheduledProgram> mSchedule;
        std::vector<ScheduledView> mScheduledViews;
    };
}

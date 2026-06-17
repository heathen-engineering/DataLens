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
#include "datalens/ThreadPool.h"

#include <atomic>
#include <cstddef>

namespace datalens
{
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

    private:
        // Don't split below this many rows per chunk: keeps per-chunk overhead amortised and
        // limits false sharing at chunk boundaries.
        static constexpr std::size_t kMinChunk = 2048;

        ThreadPool mPool;
    };
}

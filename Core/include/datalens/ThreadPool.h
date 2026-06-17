/******************************************************************************
 * ThreadPool.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * A persistent worker pool the Lens owns to run Systems in parallel. The only
 * primitive is a blocking ParallelFor: it splits [0,count) into chunks and runs
 * them across the workers (plus the calling thread), returning once all chunks
 * are done. Chunk ranges are disjoint, so a System that writes disjoint rows is
 * race-free by construction.
 ******************************************************************************/

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace datalens
{
    class ThreadPool
    {
    public:
        /// threadCount == 0 uses hardware_concurrency. The count includes the calling thread,
        /// so the pool spawns (threadCount - 1) workers.
        explicit ThreadPool(unsigned threadCount = 0);
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        unsigned ThreadCount() const { return mThreadCount; }

        /// Run body(begin,end) over disjoint chunks covering [0,count); blocks until all done.
        /// minChunk bounds how small a chunk can get (amortises per-chunk overhead).
        void ParallelFor(std::size_t count, std::size_t minChunk,
                         const std::function<void(std::size_t, std::size_t)>& body);

    private:
        void WorkerMain();
        void Drain();

        unsigned mThreadCount;
        std::vector<std::thread> mWorkers;

        std::mutex mMutex;                 // guards batch publication + mStop + mEpoch
        std::condition_variable mStartCv;  // workers wait for a new batch
        bool mStop = false;
        std::uint64_t mEpoch = 0;

        const std::function<void(std::size_t, std::size_t)>* mBody = nullptr;
        std::vector<std::pair<std::size_t, std::size_t>> mRanges;
        std::atomic<std::size_t> mNextRange{0};

        std::mutex mDoneMutex;             // guards mActive
        std::condition_variable mDoneCv;
        std::size_t mActive = 0;           // participants still draining the current batch
    };
}

/******************************************************************************
 * ThreadPool.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 ******************************************************************************/

#include "datalens/ThreadPool.h"

#include <algorithm>

namespace datalens
{
    ThreadPool::ThreadPool(unsigned threadCount)
    {
        if (threadCount == 0)
            threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0)
            threadCount = 1;
        mThreadCount = threadCount;

        // The calling thread participates, so spawn one fewer worker.
        for (unsigned i = 0; i + 1 < threadCount; ++i)
            mWorkers.emplace_back([this] { WorkerMain(); });
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mStop = true;
            ++mEpoch;
        }
        mStartCv.notify_all();
        for (auto& t : mWorkers)
            t.join();
    }

    void ThreadPool::WorkerMain()
    {
        std::uint64_t lastEpoch = 0;
        for (;;)
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mStartCv.wait(lk, [&] { return mStop || mEpoch != lastEpoch; });
            if (mStop)
                return;
            lastEpoch = mEpoch;
            lk.unlock();

            Drain();

            // This participant has fully stopped touching the batch state (mBody/mRanges).
            std::lock_guard<std::mutex> dlk(mDoneMutex);
            if (--mActive == 0)
                mDoneCv.notify_all();
        }
    }

    void ThreadPool::Drain()
    {
        for (;;)
        {
            const std::size_t i = mNextRange.fetch_add(1, std::memory_order_relaxed);
            if (i >= mRanges.size())
                break;
            (*mBody)(mRanges[i].first, mRanges[i].second);
        }
    }

    void ThreadPool::ParallelFor(std::size_t count, std::size_t minChunk,
                                 const std::function<void(std::size_t, std::size_t)>& body)
    {
        if (count == 0)
            return;
        if (minChunk == 0)
            minChunk = 1;

        std::size_t chunkSize = (count + mThreadCount - 1) / mThreadCount;
        chunkSize = std::max(chunkSize, minChunk);

        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        for (std::size_t b = 0; b < count; b += chunkSize)
            ranges.emplace_back(b, std::min(b + chunkSize, count));

        // Single chunk or no workers: run inline, no synchronisation needed.
        if (ranges.size() == 1 || mWorkers.empty())
        {
            for (const auto& r : ranges)
                body(r.first, r.second);
            return;
        }

        // Publish the batch and wake all workers. ParallelFor is not re-entrant; the caller waits
        // below until every participant has exited Drain, so the next batch never mutates mRanges
        // while a previous worker is still reading it.
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mBody = &body;
            mRanges = std::move(ranges);
            mNextRange.store(0, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> dlk(mDoneMutex);
                mActive = static_cast<std::size_t>(mWorkers.size()) + 1; // all workers + caller
            }
            ++mEpoch;
        }
        mStartCv.notify_all();

        // The calling thread participates too.
        Drain();

        std::unique_lock<std::mutex> lk(mDoneMutex);
        --mActive; // caller has finished draining
        mDoneCv.wait(lk, [this] { return mActive == 0; });
        mBody = nullptr;
    }
}

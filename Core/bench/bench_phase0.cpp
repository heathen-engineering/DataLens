// Standalone reproduction of the UE Phase-0 benchmark matrix, minus the engine harness.
// Emits a CSV (to stdout, or to the file given as argv[1]) in roughly the same shape as the
// committed Test Results/Phase0_*.csv, so the extracted core can be sanity-checked against the
// original numbers. Perf is informational (per-machine), not a hard gate.

#include "datalens/DataStore.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;

static double Ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static std::vector<DataStoreColumnSchema> Cols()
{
    return {
        {"ColFloat",  DataLensValueType::Float},
        {"ColInt32",  DataLensValueType::Int32},
        {"ColDouble", DataLensValueType::Double},
    };
}

static std::vector<uint8_t> RowMajor(size_t rows, size_t stride)
{
    std::vector<uint8_t> data(rows * stride, 0);
    for (size_t r = 0; r < rows; ++r)
    {
        float f = static_cast<float>(r);
        int32_t i = static_cast<int32_t>(r);
        double d = static_cast<double>(r);
        size_t off = r * stride;
        std::memcpy(data.data() + off, &f, sizeof(f)); off += sizeof(f);
        std::memcpy(data.data() + off, &i, sizeof(i)); off += sizeof(i);
        std::memcpy(data.data() + off, &d, sizeof(d));
    }
    return data;
}

int main(int argc, char** argv)
{
    std::FILE* out = stdout;
    if (argc > 1)
    {
        out = std::fopen(argv[1], "w");
        if (!out) { std::perror("fopen"); return 1; }
    }

    std::fprintf(out, "Test,Rows,Columns,RowSpanBytes,DurationMs\n");

    const std::vector<size_t> rowCounts = {100, 1000, 10000, 100000, 1000000, 10000000};
    const size_t stride = 16; // 4 + 4 + 8
    const int iters = 1000;

    for (size_t rows : rowCounts)
    {
        // Create (prealloc).
        {
            auto t0 = Clock::now();
            DataStore store(Cols(), rows);
            auto t1 = Clock::now();
            std::fprintf(out, "CreatePrealloc,%zu,3,%zu,%.3f\n", rows, stride, Ms(t0, t1));
            (void)store.GetRowCount();
        }

        // Create with data.
        auto data = RowMajor(rows, stride);
        {
            auto t0 = Clock::now();
            DataStore store(Cols(), data);
            auto t1 = Clock::now();
            std::fprintf(out, "CreateWithData,%zu,3,%zu,%.3f\n", rows, stride, Ms(t0, t1));
            (void)store.GetRowCount();
        }

        // Random GetRaw / SetRaw / TryGet / TrySet (iters accesses).
        DataStore store(Cols(), data);
        std::mt19937_64 rng(12345);
        std::uniform_int_distribution<size_t> rowDist(0, rows - 1);
        std::uniform_int_distribution<int> colDist(0, 2);
        std::vector<size_t> rr(iters); std::vector<int> rc(iters);
        for (int k = 0; k < iters; ++k) { rr[k] = rowDist(rng); rc[k] = colDist(rng); }

        volatile double sink = 0;
        {
            auto t0 = Clock::now();
            for (int k = 0; k < iters; ++k)
                switch (rc[k]) {
                    case 0: sink += store.GetRaw<float>(rr[k], 0); break;
                    case 1: sink += store.GetRaw<int32_t>(rr[k], 1); break;
                    default: sink += store.GetRaw<double>(rr[k], 2); break;
                }
            auto t1 = Clock::now();
            std::fprintf(out, "GetRaw_%dIters,%zu,3,%zu,%.3f\n", iters, rows, stride, Ms(t0, t1));
        }
        {
            auto t0 = Clock::now();
            for (int k = 0; k < iters; ++k)
                switch (rc[k]) {
                    case 0: store.SetRaw<float>(rr[k], 0, 1.0f); break;
                    case 1: store.SetRaw<int32_t>(rr[k], 1, 1); break;
                    default: store.SetRaw<double>(rr[k], 2, 1.0); break;
                }
            auto t1 = Clock::now();
            std::fprintf(out, "SetRaw_%dIters,%zu,3,%zu,%.3f\n", iters, rows, stride, Ms(t0, t1));
        }

        // Dump.
        {
            auto t0 = Clock::now();
            auto dumped = store.Dump();
            auto t1 = Clock::now();
            std::fprintf(out, "Dump,%zu,3,%zu,%.3f\n", rows, stride, Ms(t0, t1));
            (void)dumped.size();
        }
        (void)sink;
    }

    if (out != stdout) std::fclose(out);
    return 0;
}

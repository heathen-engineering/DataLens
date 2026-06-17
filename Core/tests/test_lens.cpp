// Lens (A3): running Systems in parallel across the worker pool. The key guarantee is that the
// parallel result is identical to the single-threaded result regardless of thread count.

#include <catch2/catch_test_macros.hpp>

#include "datalens/DataStore.h"
#include "datalens/Lens.h"

#include <cstdint>
#include <vector>

namespace
{
    // A store of `rows` live Int32 rows where HP[r] = r % 200.
    DataStore MakeBigStore(size_t rows)
    {
        std::vector<DataStoreColumnSchema> cols = {{"HP", DataLensValueType::Int32}};
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<int32_t>(row, 0, static_cast<int32_t>(r % 200));
        }
        return s;
    }
}

TEST_CASE("lens: reports a sane thread count", "[lens]")
{
    datalens::Lens lens(4);
    REQUIRE(lens.ThreadCount() == 4);

    datalens::Lens autoLens(0);
    REQUIRE(autoLens.ThreadCount() >= 1);
}

TEST_CASE("lens: parallel result matches single-threaded", "[lens]")
{
    const size_t rows = 100000;

    // Serial reference.
    DataStore serial = MakeBigStore(rows);
    size_t serialAffected = serial.RunColumnSystem<int32_t>(
        0, DataSystemOp::Add, 100, true, 0, DataCompareOp::Less, 50);

    // Parallel via the Lens (force several threads).
    DataStore parallel = MakeBigStore(rows);
    datalens::Lens lens(8);
    size_t parAffected = lens.RunSystem<int32_t>(
        parallel, 0, DataSystemOp::Add, 100, true, 0, DataCompareOp::Less, 50);

    REQUIRE(parAffected == serialAffected);
    for (size_t r = 0; r < rows; ++r)
        REQUIRE(parallel.GetRaw<int32_t>(r, 0) == serial.GetRaw<int32_t>(r, 0));
}

TEST_CASE("lens: result is stable across thread counts", "[lens]")
{
    const size_t rows = 50000;

    DataStore ref = MakeBigStore(rows);
    ref.RunColumnSystem<int32_t>(0, DataSystemOp::Max, 75, false, 0, DataCompareOp::Always, 0);

    for (unsigned threads : {1u, 2u, 3u, 8u})
    {
        DataStore s = MakeBigStore(rows);
        datalens::Lens lens(threads);
        size_t affected = lens.RunSystem<int32_t>(s, 0, DataSystemOp::Max, 75, false, 0, DataCompareOp::Always, 0);
        REQUIRE(affected == rows); // Max touches every live row
        for (size_t r = 0; r < rows; ++r)
            REQUIRE(s.GetRaw<int32_t>(r, 0) == ref.GetRaw<int32_t>(r, 0));
    }
}

TEST_CASE("lens: many small runs reuse the pool safely", "[lens]")
{
    datalens::Lens lens(4);
    DataStore s = MakeBigStore(10000);
    for (int i = 0; i < 50; ++i)
        lens.RunSystem<int32_t>(s, 0, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0);
    // 50 unconditional +1 over rows seeded with r%200.
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 50);
    REQUIRE(s.GetRaw<int32_t>(123, 0) == static_cast<int32_t>(123 % 200) + 50);
}

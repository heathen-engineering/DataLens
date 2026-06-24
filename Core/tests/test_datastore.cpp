// DataStore correctness — ported from the UE Phase-0 Blueprint harness, minus the
// engine plumbing. Locks the byte-stride baseline so later A-phases have a safety net.

#include <catch2/catch_test_macros.hpp>

#include "TestTags.h"

#include "datalens/DataStore.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    std::vector<DataStoreColumnSchema> ThreeCols()
    {
        return {
            {Tag("ColFloat"),  DataLensValueType::Float},
            {Tag("ColInt32"),  DataLensValueType::Int32},
            {Tag("ColDouble"), DataLensValueType::Double},
        };
    }

    // Row-major buffer where row r holds (float r, int32 r, double r).
    std::vector<uint8_t> MakeRowMajor(size_t rows, size_t stride)
    {
        std::vector<uint8_t> data(rows * stride, 0);
        for (size_t r = 0; r < rows; ++r)
        {
            float  f = static_cast<float>(r);
            int32_t i = static_cast<int32_t>(r);
            double d = static_cast<double>(r);
            size_t off = r * stride;
            std::memcpy(data.data() + off, &f, sizeof(f)); off += sizeof(f);
            std::memcpy(data.data() + off, &i, sizeof(i)); off += sizeof(i);
            std::memcpy(data.data() + off, &d, sizeof(d));
        }
        return data;
    }
}

TEST_CASE("default store is empty", "[datastore]")
{
    DataStore store;
    REQUIRE(store.GetRowCount() == 0);
    REQUIRE(store.GetColumnCount() == 0);
}

TEST_CASE("prealloc sets row/column/stride", "[datastore]")
{
    auto cols = ThreeCols();
    DataStore store(cols, 100);
    REQUIRE(store.GetRowCount() == 100);
    REQUIRE(store.GetColumnCount() == 3);
    REQUIRE(store.GetRowStride() == 16); // 4 + 4 + 8
}

TEST_CASE("construct with row-major data round-trips", "[datastore]")
{
    auto cols = ThreeCols();
    const size_t rows = 1000;
    auto data = MakeRowMajor(rows, 16);
    DataStore store(cols, data);

    REQUIRE(store.GetRowCount() == rows);
    for (size_t r : {size_t(0), rows / 2, rows - 1})
    {
        REQUIRE(store.GetRaw<float>(r, 0)  == static_cast<float>(r));
        REQUIRE(store.GetRaw<int32_t>(r, 1) == static_cast<int32_t>(r));
        REQUIRE(store.GetRaw<double>(r, 2) == static_cast<double>(r));
    }
}

TEST_CASE("padding rows are appended to the row count", "[datastore]")
{
    auto cols = ThreeCols();
    const size_t rows = 100;
    const size_t extra = 20;
    auto data = MakeRowMajor(rows, 16);
    DataStore store(cols, data, extra);
    REQUIRE(store.GetRowCount() == rows + extra);
}

TEST_CASE("SetRaw then GetRaw", "[datastore]")
{
    auto cols = ThreeCols();
    DataStore store(cols, 8);
    store.SetRaw<float>(3, 0, 1.5f);
    store.SetRaw<int32_t>(3, 1, 42);
    store.SetRaw<double>(3, 2, 2.25);
    REQUIRE(store.GetRaw<float>(3, 0)  == 1.5f);
    REQUIRE(store.GetRaw<int32_t>(3, 1) == 42);
    REQUIRE(store.GetRaw<double>(3, 2) == 2.25);
}

TEST_CASE("TryGet/TrySet are bounds-safe and type-mismatch-safe", "[datastore]")
{
    auto cols = ThreeCols();
    DataStore store(cols, 1);

    float f{}; int32_t i{}; double d{};

    // Out-of-bounds row / column -> false, no crash.
    REQUIRE_FALSE(store.TryGet<float>(1, 0, f));
    REQUIRE_FALSE(store.TryGet<int32_t>(2, 1, i));
    REQUIRE_FALSE(store.TryGet<float>(0, 5, f));

    // Type mismatch (reading a float out of the double column) clamps the copy, no UB.
    REQUIRE(store.TrySet<double>(0, 2, 3.5));
    REQUIRE(store.TryGet<float>(0, 2, f)); // narrower read is allowed and safe

    // Out-of-bounds writes rejected.
    REQUIRE_FALSE(store.TrySet<float>(1, 0, 1.0f));
    REQUIRE_FALSE(store.TrySet<int32_t>(0, 5, 1));
}

TEST_CASE("LoadRaw then Dump round-trips", "[datastore]")
{
    auto cols = ThreeCols();
    const size_t rows = 500;
    auto data = MakeRowMajor(rows, 16);

    DataStore store(cols, size_t(0));
    store.LoadRaw(data);
    REQUIRE(store.GetRowCount() == rows);

    auto dumped = store.Dump();
    REQUIRE(dumped.size() == data.size());
    REQUIRE(std::memcmp(dumped.data(), data.data(), data.size()) == 0);
}

TEST_CASE("row validity flag toggles", "[datastore]")
{
    auto cols = ThreeCols();
    DataStore store(cols, 4);
    store.SetValid(2, true);
    REQUIRE(store.IsValidRow(2));
    store.SetValid(2, false);
    REQUIRE_FALSE(store.IsValidRow(2));
}

TEST_CASE("explicit row allocation on the validity bitmask (A2)", "[datastore]")
{
    auto cols = ThreeCols();
    DataStore store(cols, 3); // capacity 3, all invalid/free
    REQUIRE(store.GetLiveCount() == 0);

    size_t a = store.AllocRow();
    size_t b = store.AllocRow();
    size_t c = store.AllocRow();
    REQUIRE(a != SIZE_MAX);
    REQUIRE(b != SIZE_MAX);
    REQUIRE(c != SIZE_MAX);
    REQUIRE(a != b);
    REQUIRE(b != c);
    REQUIRE(a != c);
    REQUIRE(store.GetLiveCount() == 3);
    REQUIRE(store.IsValidRow(a));

    REQUIRE(store.AllocRow() == SIZE_MAX); // fixed capacity: full

    store.FreeRow(b);
    REQUIRE(store.GetLiveCount() == 2);
    REQUIRE_FALSE(store.IsValidRow(b));

    REQUIRE(store.AllocRow() == b); // freed slot reused
    REQUIRE(store.GetLiveCount() == 3);
}

TEST_CASE("allocation respects capacity across multiple bitmask words (A2)", "[datastore]")
{
    auto cols = ThreeCols();
    DataStore store(cols, 130); // 3 words: 64 + 64 + 2 valid bits
    for (int i = 0; i < 130; ++i)
        REQUIRE(store.AllocRow() != SIZE_MAX);
    REQUIRE(store.GetLiveCount() == 130);
    REQUIRE(store.AllocRow() == SIZE_MAX); // never hands out rows past capacity
}

TEST_CASE("validity no longer overlaps column data (A2 wart fix)", "[datastore]")
{
    auto cols = ThreeCols();
    DataStore store(cols, 4);
    store.SetRaw<float>(1, 0, 7.5f); // column 0 is real Float data
    store.SetValid(1, true);          // previously OR'd into the float's low byte
    REQUIRE(store.GetRaw<float>(1, 0) == 7.5f);
    REQUIRE(store.IsValidRow(1));
}

TEST_CASE("column buffers are cache-line aligned (A3.6)", "[datastore]")
{
    REQUIRE(DataStore::CacheLineSize() == 64);

    // A mix of strides (4, 4, 8) plus a wide one — every column buffer must start 64-aligned so
    // concurrent Systems writing different columns never false-share a cache line.
    std::vector<DataStoreColumnSchema> cols = {
        {Tag("A"), DataLensValueType::Float},  // 4
        {Tag("B"), DataLensValueType::Int32},  // 4
        {Tag("C"), DataLensValueType::Double}, // 8
        {Tag("D"), DataLensValueType::Int64},  // 8
    };
    DataStore store(cols, 1000);
    for (size_t c = 0; c < store.GetColumnCount(); ++c)
        REQUIRE(store.IsColumnCacheAligned(c));

    // Still aligned after a schema conversion (rebuilds the column buffers).
    DataStoreSchema wider;
    wider.Columns = {
        {Tag("A"), DataLensValueType::Double}, // widen 4 -> 8
        {Tag("C"), DataLensValueType::Double},
        {Tag("E"), DataLensValueType::Int32},  // new column
    };
    store.ConvertToSchema(wider);
    for (size_t c = 0; c < store.GetColumnCount(); ++c)
        REQUIRE(store.IsColumnCacheAligned(c));

    // Out-of-range column reports false rather than reading past the end.
    REQUIRE_FALSE(store.IsColumnCacheAligned(99));
}

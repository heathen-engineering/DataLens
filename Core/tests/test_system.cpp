// Systems (A3): data-described conditional column transforms over live rows.

#include <catch2/catch_test_macros.hpp>

#include "datalens/DataStore.h"

#include <cstdint>
#include <vector>

namespace
{
    // One Int32 "HP" column, capacity 4, 3 live rows: HP = {100, 30, 80}; row 3 invalid.
    DataStore MakeHpStore()
    {
        std::vector<DataStoreColumnSchema> cols = {{"HP", DataLensValueType::Int32}};
        DataStore s(cols, 4);
        size_t r0 = s.AllocRow();
        size_t r1 = s.AllocRow();
        size_t r2 = s.AllocRow();
        s.SetRaw<int32_t>(r0, 0, 100);
        s.SetRaw<int32_t>(r1, 0, 30);
        s.SetRaw<int32_t>(r2, 0, 80);
        return s;
    }
}

TEST_CASE("system: unconditional add over live rows only", "[system]")
{
    auto s = MakeHpStore();
    size_t n = s.RunColumnSystem<int32_t>(0, DataSystemOp::Add, 5, false, 0, DataCompareOp::Always, 0);
    REQUIRE(n == 3);
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 105);
    REQUIRE(s.GetRaw<int32_t>(1, 0) == 35);
    REQUIRE(s.GetRaw<int32_t>(2, 0) == 85);
    REQUIRE(s.GetRaw<int32_t>(3, 0) == 0); // invalid row never touched
}

TEST_CASE("system: predicated update (regen where HP < 50)", "[system]")
{
    auto s = MakeHpStore();
    size_t n = s.RunColumnSystem<int32_t>(0, DataSystemOp::Add, 100, true, 0, DataCompareOp::Less, 50);
    REQUIRE(n == 1);
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 100); // 100 not < 50
    REQUIRE(s.GetRaw<int32_t>(1, 0) == 130); // 30 < 50 -> +100
    REQUIRE(s.GetRaw<int32_t>(2, 0) == 80);  // 80 not < 50
}

TEST_CASE("system: freed rows are skipped", "[system]")
{
    auto s = MakeHpStore();
    s.FreeRow(1);
    size_t n = s.RunColumnSystem<int32_t>(0, DataSystemOp::Set, 7, false, 0, DataCompareOp::Always, 0);
    REQUIRE(n == 2);
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 7);
    REQUIRE(s.GetRaw<int32_t>(1, 0) == 30); // freed row keeps its bytes
    REQUIRE(s.GetRaw<int32_t>(2, 0) == 7);
}

TEST_CASE("system: min/max ops", "[system]")
{
    auto s = MakeHpStore();
    s.RunColumnSystem<int32_t>(0, DataSystemOp::Max, 50, false, 0, DataCompareOp::Always, 0);
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 100);
    REQUIRE(s.GetRaw<int32_t>(1, 0) == 50); // max(30, 50)
    REQUIRE(s.GetRaw<int32_t>(2, 0) == 80);
}

TEST_CASE("system: float clamp via Min", "[system]")
{
    std::vector<DataStoreColumnSchema> cols = {{"Stamina", DataLensValueType::Float}};
    DataStore s(cols, 2);
    size_t a = s.AllocRow();
    size_t b = s.AllocRow();
    s.SetRaw<float>(a, 0, 120.0f);
    s.SetRaw<float>(b, 0, 40.0f);

    s.RunColumnSystem<float>(0, DataSystemOp::Min, 100.0f, false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE(s.GetRaw<float>(a, 0) == 100.0f); // clamped
    REQUIRE(s.GetRaw<float>(b, 0) == 40.0f);  // unchanged
}

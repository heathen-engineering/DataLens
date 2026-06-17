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

TEST_CASE("system: cross-column add (Current += Regen)", "[system]")
{
    // Two Int32 columns: Current and Regen. Add Regen into Current per row.
    std::vector<DataStoreColumnSchema> cols = {{"Current", DataLensValueType::Int32},
                                               {"Regen",   DataLensValueType::Int32}};
    DataStore s(cols, 4);
    size_t r0 = s.AllocRow();
    size_t r1 = s.AllocRow();
    size_t r2 = s.AllocRow();
    s.SetRaw<int32_t>(r0, 0, 100); s.SetRaw<int32_t>(r0, 1, 5);
    s.SetRaw<int32_t>(r1, 0, 30);  s.SetRaw<int32_t>(r1, 1, 10);
    s.SetRaw<int32_t>(r2, 0, 80);  s.SetRaw<int32_t>(r2, 1, 0);

    size_t n = s.RunColumnSystemColumn<int32_t>(0, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0);
    REQUIRE(n == 3);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 105);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 40);
    REQUIRE(s.GetRaw<int32_t>(r2, 0) == 80);
    REQUIRE(s.GetRaw<int32_t>(3, 0)  == 0); // invalid row never touched
    REQUIRE(s.GetRaw<int32_t>(r1, 1) == 10); // operand column itself untouched
}

TEST_CASE("system: cross-column per-row clamp (Current = min(Current, Max))", "[system]")
{
    // Classic HATE attribute clamp: clamp Current to a per-row Max column.
    std::vector<DataStoreColumnSchema> cols = {{"Current", DataLensValueType::Int32},
                                               {"Max",     DataLensValueType::Int32}};
    DataStore s(cols, 3);
    size_t r0 = s.AllocRow();
    size_t r1 = s.AllocRow();
    s.SetRaw<int32_t>(r0, 0, 150); s.SetRaw<int32_t>(r0, 1, 100); // over cap
    s.SetRaw<int32_t>(r1, 0, 40);  s.SetRaw<int32_t>(r1, 1, 100); // under cap

    s.RunColumnSystemColumn<int32_t>(0, DataSystemOp::Min, 1, false, 0, DataCompareOp::Always, 0);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 100); // clamped to Max
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 40);  // already under
}

TEST_CASE("system: cross-column with predicate (regen only where Alive != 0)", "[system]")
{
    // Operand from a column AND a predicate on a third column: add Regen only where Alive != 0.
    std::vector<DataStoreColumnSchema> cols = {{"Current", DataLensValueType::Int32},
                                               {"Regen",   DataLensValueType::Int32},
                                               {"Alive",   DataLensValueType::Int32}};
    DataStore s(cols, 3);
    size_t r0 = s.AllocRow();
    size_t r1 = s.AllocRow();
    s.SetRaw<int32_t>(r0, 0, 50); s.SetRaw<int32_t>(r0, 1, 10); s.SetRaw<int32_t>(r0, 2, 1);
    s.SetRaw<int32_t>(r1, 0, 50); s.SetRaw<int32_t>(r1, 1, 10); s.SetRaw<int32_t>(r1, 2, 0);

    size_t n = s.RunColumnSystemColumn<int32_t>(0, DataSystemOp::Add, 1, true, 2, DataCompareOp::NotEqual, 0);
    REQUIRE(n == 1);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 60); // Alive -> regened
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 50); // dead -> unchanged
}

TEST_CASE("system: bitwise ops set/clear/toggle bits (A3.8)", "[system][bitmask]")
{
    // Effect-flags column (HATE uses bitmask flags for effects). Bit layout per row varies.
    std::vector<DataStoreColumnSchema> cols = {{"Flags", DataLensValueType::Int32}};
    DataStore s(cols, 4);
    size_t r0 = s.AllocRow(); s.SetRaw<int32_t>(r0, 0, 0b0000);
    size_t r1 = s.AllocRow(); s.SetRaw<int32_t>(r1, 0, 0b1010);

    // Or: set bit 0 and bit 2 (0b0101) on every row.
    s.RunColumnSystem<int32_t>(0, DataSystemOp::Or, 0b0101, false, 0, DataCompareOp::Always, 0);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 0b0101);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 0b1111);

    // AndNot: clear bit 0 (0b0001).
    s.RunColumnSystem<int32_t>(0, DataSystemOp::AndNot, 0b0001, false, 0, DataCompareOp::Always, 0);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 0b0100);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 0b1110);

    // Xor: toggle bit 2 (0b0100).
    s.RunColumnSystem<int32_t>(0, DataSystemOp::Xor, 0b0100, false, 0, DataCompareOp::Always, 0);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 0b0000);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 0b1010);

    // And: mask to low two bits (0b0011).
    s.RunColumnSystem<int32_t>(0, DataSystemOp::And, 0b0011, false, 0, DataCompareOp::Always, 0);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 0b0000);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 0b0010);
}

TEST_CASE("system: bitmask predicates gate by flags (A3.8)", "[system][bitmask]")
{
    // Two columns: Flags + HP. "If row HAS the Stunned bit (0b0010), set HP = 0."
    std::vector<DataStoreColumnSchema> cols = {{"HP", DataLensValueType::Int32},
                                               {"Flags", DataLensValueType::Int32}};
    DataStore s(cols, 4);
    size_t r0 = s.AllocRow(); s.SetRaw<int32_t>(r0, 0, 100); s.SetRaw<int32_t>(r0, 1, 0b0010); // stunned
    size_t r1 = s.AllocRow(); s.SetRaw<int32_t>(r1, 0, 100); s.SetRaw<int32_t>(r1, 1, 0b0001); // not
    size_t r2 = s.AllocRow(); s.SetRaw<int32_t>(r2, 0, 100); s.SetRaw<int32_t>(r2, 1, 0b0110); // stunned+other

    size_t n = s.RunColumnSystem<int32_t>(0, DataSystemOp::Set, 0, true, 1, DataCompareOp::HasAnyBits, 0b0010);
    REQUIRE(n == 2);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 0);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 100);
    REQUIRE(s.GetRaw<int32_t>(r2, 0) == 0);

    // HasAllBits: only rows with BOTH bit1 and bit2 (0b0110).
    s.RunColumnSystem<int32_t>(0, DataSystemOp::Set, 7, true, 1, DataCompareOp::HasAllBits, 0b0110);
    REQUIRE(s.GetRaw<int32_t>(r2, 0) == 7); // 0b0110 has all of 0b0110
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 0); // 0b0010 lacks bit2

    // LacksBits: rows with none of 0b0010.
    size_t m = s.RunColumnSystem<int32_t>(0, DataSystemOp::Set, 9, true, 1, DataCompareOp::LacksBits, 0b0010);
    REQUIRE(m == 1); // only r1 (0b0001)
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 9);
}

TEST_CASE("system: bitwise op on a float column is a no-op (A3.8)", "[system][bitmask]")
{
    std::vector<DataStoreColumnSchema> cols = {{"V", DataLensValueType::Float}};
    DataStore s(cols, 2);
    size_t r0 = s.AllocRow(); s.SetRaw<float>(r0, 0, 3.5f);

    // And/Or/Xor are undefined on float -> kernel leaves the cell unchanged (and "affected" still
    // counts the live row, since the select write-back writes the unchanged value).
    s.RunColumnSystem<float>(0, DataSystemOp::Or, 1.0f, false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE(s.GetRaw<float>(r0, 0) == 3.5f);

    // Bitmask predicate never matches on float, so nothing is set.
    size_t n = s.RunColumnSystem<float>(0, DataSystemOp::Set, 99.0f, true, 0, DataCompareOp::HasAnyBits, 1.0f);
    REQUIRE(n == 0);
    REQUIRE(s.GetRaw<float>(r0, 0) == 3.5f);
}

TEST_CASE("system: scaled cross-column = Euler integration (pos += vel * dt)", "[system][scaled]")
{
    // Pos += Vel * dt, the canonical fused-multiply step.
    std::vector<DataStoreColumnSchema> cols = {{"Pos", DataLensValueType::Float},
                                               {"Vel", DataLensValueType::Float}};
    DataStore s(cols, 3);
    size_t r0 = s.AllocRow(); s.SetRaw<float>(r0, 0, 0.0f);  s.SetRaw<float>(r0, 1, 10.0f);
    size_t r1 = s.AllocRow(); s.SetRaw<float>(r1, 0, 5.0f);  s.SetRaw<float>(r1, 1, -2.0f);

    const float dt = 0.5f;
    size_t n = s.RunColumnSystemScaledColumn<float>(0, DataSystemOp::Add, 1, dt,
        false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE(n == 2);
    REQUIRE(s.GetRaw<float>(r0, 0) == 0.0f + 10.0f * dt); // 5
    REQUIRE(s.GetRaw<float>(r1, 0) == 5.0f + -2.0f * dt); // 4
    // Operand column untouched.
    REQUIRE(s.GetRaw<float>(r0, 1) == 10.0f);
}

TEST_CASE("system: scaled cross-column integer (effective += base * mult)", "[system][scaled]")
{
    std::vector<DataStoreColumnSchema> cols = {{"Eff", DataLensValueType::Int32},
                                               {"Base", DataLensValueType::Int32}};
    DataStore s(cols, 3);
    size_t r0 = s.AllocRow(); s.SetRaw<int32_t>(r0, 0, 0); s.SetRaw<int32_t>(r0, 1, 7);
    size_t r1 = s.AllocRow(); s.SetRaw<int32_t>(r1, 0, 1); s.SetRaw<int32_t>(r1, 1, 10);

    s.RunColumnSystemScaledColumn<int32_t>(0, DataSystemOp::Add, 1, /*scale*/3,
        false, 0, DataCompareOp::Always, 0);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 0 + 7 * 3);   // 21
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 1 + 10 * 3);  // 31

    // scale of 1 reproduces a plain cross-column add.
    DataStore s2(cols, 1);
    size_t a = s2.AllocRow(); s2.SetRaw<int32_t>(a, 0, 100); s2.SetRaw<int32_t>(a, 1, 5);
    s2.RunColumnSystemScaledColumn<int32_t>(0, DataSystemOp::Add, 1, 1, false, 0, DataCompareOp::Always, 0);
    REQUIRE(s2.GetRaw<int32_t>(a, 0) == 105);
}

TEST_CASE("system: cross-column out-of-range operand column is a no-op", "[system]")
{
    std::vector<DataStoreColumnSchema> cols = {{"Current", DataLensValueType::Int32}};
    DataStore s(cols, 2);
    size_t r0 = s.AllocRow();
    s.SetRaw<int32_t>(r0, 0, 7);
    size_t n = s.RunColumnSystemColumn<int32_t>(0, DataSystemOp::Add, 99, false, 0, DataCompareOp::Always, 0);
    REQUIRE(n == 0);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 7);
}

TEST_CASE("system: LOD defaults to 0 and round-trips", "[system][lod]")
{
    std::vector<DataStoreColumnSchema> cols = {{"V", DataLensValueType::Int32}};
    DataStore s(cols, 4);
    size_t r0 = s.AllocRow();
    REQUIRE(s.GetLod(r0) == 0); // freshly allocated row is full fidelity
    s.SetLod(r0, 2);
    REQUIRE(s.GetLod(r0) == 2);
    s.SetLod(99, 5);            // out of range: ignored, no crash
    REQUIRE(s.GetLod(99) == 0);
    // Re-allocating a freed slot resets its LOD to 0.
    s.FreeRow(r0);
    size_t again = s.AllocRow();
    REQUIRE(again == r0);
    REQUIRE(s.GetLod(again) == 0);
}

TEST_CASE("system: LOD band scopes which rows a System touches", "[system][lod]")
{
    // Five rows at LOD 0,1,2,0,1; a band [0,1] System should skip the LOD-2 row.
    std::vector<DataStoreColumnSchema> cols = {{"HP", DataLensValueType::Int32}};
    DataStore s(cols, 5);
    const uint8_t lods[5] = {0, 1, 2, 0, 1};
    for (int i = 0; i < 5; ++i)
    {
        size_t r = s.AllocRow();
        s.SetRaw<int32_t>(r, 0, 100);
        s.SetLod(r, lods[i]);
    }

    size_t n = s.RunColumnSystemInLodBand<int32_t>(0, DataSystemOp::Add, 10,
        false, 0, DataCompareOp::Always, 0, /*minLod*/0, /*maxLod*/1);
    REQUIRE(n == 4); // the four LOD-0/1 rows
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 110);
    REQUIRE(s.GetRaw<int32_t>(1, 0) == 110);
    REQUIRE(s.GetRaw<int32_t>(2, 0) == 100); // LOD 2 skipped
    REQUIRE(s.GetRaw<int32_t>(3, 0) == 110);
    REQUIRE(s.GetRaw<int32_t>(4, 0) == 110);

    // A coarser tick that runs ONLY the LOD-2 band.
    size_t n2 = s.RunColumnSystemInLodBand<int32_t>(0, DataSystemOp::Set, 7,
        false, 0, DataCompareOp::Always, 0, 2, 2);
    REQUIRE(n2 == 1);
    REQUIRE(s.GetRaw<int32_t>(2, 0) == 7);
}

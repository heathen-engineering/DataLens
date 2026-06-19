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

namespace
{
    // Two Int32 columns over `rows` live rows: Current[r] = r % 200, Regen[r] = r % 7.
    DataStore MakeTwoColStore(size_t rows)
    {
        std::vector<DataStoreColumnSchema> cols = {{"Current", DataLensValueType::Int32},
                                                   {"Regen",   DataLensValueType::Int32}};
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<int32_t>(row, 0, static_cast<int32_t>(r % 200));
            s.SetRaw<int32_t>(row, 1, static_cast<int32_t>(r % 7));
        }
        return s;
    }
}

TEST_CASE("lens: parallel cross-column result matches single-threaded", "[lens]")
{
    const size_t rows = 100000;

    // Serial reference: Current += Regen.
    DataStore serial = MakeTwoColStore(rows);
    size_t serialAffected = serial.RunColumnSystemColumn<int32_t>(
        0, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0);

    // Parallel via the Lens.
    DataStore parallel = MakeTwoColStore(rows);
    datalens::Lens lens(8);
    size_t parAffected = lens.RunSystemColumn<int32_t>(
        parallel, 0, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0);

    REQUIRE(parAffected == serialAffected);
    REQUIRE(parAffected == rows);
    for (size_t r = 0; r < rows; ++r)
        REQUIRE(parallel.GetRaw<int32_t>(r, 0) == serial.GetRaw<int32_t>(r, 0));
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

namespace
{
    datalens::SystemDesc ScalarSys(DataStore& s, size_t targetCol, DataSystemOp op, double operand)
    {
        datalens::SystemDesc d;
        d.store = &s; d.elemType = DataLensValueType::Int32;
        d.targetCol = targetCol; d.op = op; d.operand = operand;
        return d;
    }
}

TEST_CASE("lens: batch of independent Systems matches running them one by one", "[lens]")
{
    // Three independent columns; a System per column. Independent -> one concurrent wave.
    std::vector<DataStoreColumnSchema> cols = {{"A", DataLensValueType::Int32},
                                               {"B", DataLensValueType::Int32},
                                               {"C", DataLensValueType::Int32}};
    const size_t rows = 5000;
    auto build = [&]() {
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<int32_t>(row, 0, static_cast<int32_t>(r));
            s.SetRaw<int32_t>(row, 1, static_cast<int32_t>(r));
            s.SetRaw<int32_t>(row, 2, static_cast<int32_t>(r));
        }
        return s;
    };

    DataStore expected = build();
    expected.RunColumnSystem<int32_t>(0, DataSystemOp::Add, 10, false, 0, DataCompareOp::Always, 0);
    expected.RunColumnSystem<int32_t>(1, DataSystemOp::Mul, 3, false, 0, DataCompareOp::Always, 0);
    expected.RunColumnSystem<int32_t>(2, DataSystemOp::Set, 7, false, 0, DataCompareOp::Always, 0);

    DataStore s = build();
    datalens::Lens lens(8);
    std::vector<datalens::SystemDesc> batch = {
        ScalarSys(s, 0, DataSystemOp::Add, 10),
        ScalarSys(s, 1, DataSystemOp::Mul, 3),
        ScalarSys(s, 2, DataSystemOp::Set, 7),
    };
    size_t affected = lens.RunSystems(batch);
    REQUIRE(affected == rows * 3); // every System touches every live row

    for (size_t r = 0; r < rows; ++r)
    {
        REQUIRE(s.GetRaw<int32_t>(r, 0) == expected.GetRaw<int32_t>(r, 0));
        REQUIRE(s.GetRaw<int32_t>(r, 1) == expected.GetRaw<int32_t>(r, 1));
        REQUIRE(s.GetRaw<int32_t>(r, 2) == expected.GetRaw<int32_t>(r, 2));
    }
}

TEST_CASE("lens: batch preserves submission order for conflicting Systems", "[lens]")
{
    // Two Systems write the SAME column: x += 1 then x *= 2. They conflict, so they must run in
    // order (separate waves) -> (x+1)*2, never x*2+1.
    std::vector<DataStoreColumnSchema> cols = {{"X", DataLensValueType::Int32}};
    const size_t rows = 4000;
    DataStore s(cols, rows);
    for (size_t r = 0; r < rows; ++r)
        s.SetRaw<int32_t>(s.AllocRow(), 0, static_cast<int32_t>(r));

    datalens::Lens lens(8);
    std::vector<datalens::SystemDesc> batch = {
        ScalarSys(s, 0, DataSystemOp::Add, 1),
        ScalarSys(s, 0, DataSystemOp::Mul, 2),
    };
    lens.RunSystems(batch);

    for (size_t r = 0; r < rows; ++r)
        REQUIRE(s.GetRaw<int32_t>(r, 0) == static_cast<int32_t>((r + 1) * 2));
}

TEST_CASE("lens: batch spans multiple stores", "[lens]")
{
    std::vector<DataStoreColumnSchema> cols = {{"V", DataLensValueType::Int32}};
    DataStore s1(cols, 3);
    DataStore s2(cols, 3);
    s1.SetRaw<int32_t>(s1.AllocRow(), 0, 5);
    s2.SetRaw<int32_t>(s2.AllocRow(), 0, 5);

    datalens::Lens lens(4);
    std::vector<datalens::SystemDesc> batch = {
        ScalarSys(s1, 0, DataSystemOp::Add, 100),
        ScalarSys(s2, 0, DataSystemOp::Mul, 10),
    };
    size_t affected = lens.RunSystems(batch);
    REQUIRE(affected == 2);
    REQUIRE(s1.GetRaw<int32_t>(0, 0) == 105);
    REQUIRE(s2.GetRaw<int32_t>(0, 0) == 50);
}

TEST_CASE("lens: LOD-banded System parallel result matches single-threaded", "[lens][lod]")
{
    const size_t rows = 60000;
    auto build = [&]() {
        std::vector<DataStoreColumnSchema> cols = {{"HP", DataLensValueType::Int32}};
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<int32_t>(row, 0, 0);
            s.SetLod(row, static_cast<uint8_t>(r % 3)); // LOD 0,1,2 cycling
        }
        return s;
    };

    DataStore serial = build();
    size_t serialAffected = serial.RunColumnSystemInLodBand<int32_t>(
        0, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0, 0, 1);

    DataStore parallel = build();
    datalens::Lens lens(8);
    size_t parAffected = lens.RunSystemInLodBand<int32_t>(
        parallel, 0, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0, 0, 1);

    REQUIRE(parAffected == serialAffected);
    for (size_t r = 0; r < rows; ++r)
        REQUIRE(parallel.GetRaw<int32_t>(r, 0) == serial.GetRaw<int32_t>(r, 0));
}

TEST_CASE("lens: batch honours a per-batch LOD band via SystemDesc", "[lens][lod]")
{
    std::vector<DataStoreColumnSchema> cols = {{"A", DataLensValueType::Int32}};
    DataStore s(cols, 3);
    size_t r0 = s.AllocRow(); s.SetRaw<int32_t>(r0, 0, 0); s.SetLod(r0, 0);
    size_t r1 = s.AllocRow(); s.SetRaw<int32_t>(r1, 0, 0); s.SetLod(r1, 1);
    size_t r2 = s.AllocRow(); s.SetRaw<int32_t>(r2, 0, 0); s.SetLod(r2, 2);

    datalens::SystemDesc d = ScalarSys(s, 0, DataSystemOp::Add, 5);
    d.minLod = 0; d.maxLod = 1; // band on the descriptor

    datalens::Lens lens(4);
    std::vector<datalens::SystemDesc> batch = {d};
    size_t affected = lens.RunSystems(batch);
    REQUIRE(affected == 2);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 5);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 5);
    REQUIRE(s.GetRaw<int32_t>(r2, 0) == 0); // LOD 2 outside band
}

TEST_CASE("lens: interleaved-conflict batch still equals sequential order", "[lens]")
{
    // Two columns, ops interleaved: A+=1 (col0), B+=1 (col1), A*=2 (col0), B*=2 (col1).
    // Consecutive packing would make 3 waves; level scheduling packs {0:[op0,op1],1:[op2,op3]} (2),
    // but either way the result must equal applying the ops in submission order.
    std::vector<DataStoreColumnSchema> cols = {{"A", DataLensValueType::Int32},
                                               {"B", DataLensValueType::Int32}};
    const size_t rows = 4000;
    auto build = [&]() {
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<int32_t>(row, 0, static_cast<int32_t>(r));
            s.SetRaw<int32_t>(row, 1, static_cast<int32_t>(r) + 1);
        }
        return s;
    };

    // Sequential reference: apply the four ops one at a time, in order.
    DataStore expected = build();
    expected.RunColumnSystem<int32_t>(0, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0);
    expected.RunColumnSystem<int32_t>(1, DataSystemOp::Add, 1, false, 0, DataCompareOp::Always, 0);
    expected.RunColumnSystem<int32_t>(0, DataSystemOp::Mul, 2, false, 0, DataCompareOp::Always, 0);
    expected.RunColumnSystem<int32_t>(1, DataSystemOp::Mul, 2, false, 0, DataCompareOp::Always, 0);

    DataStore s = build();
    datalens::Lens lens(8);
    std::vector<datalens::SystemDesc> batch = {
        ScalarSys(s, 0, DataSystemOp::Add, 1),
        ScalarSys(s, 1, DataSystemOp::Add, 1),
        ScalarSys(s, 0, DataSystemOp::Mul, 2),
        ScalarSys(s, 1, DataSystemOp::Mul, 2),
    };
    size_t affected = lens.RunSystems(batch);
    REQUIRE(affected == rows * 4);

    for (size_t r = 0; r < rows; ++r)
    {
        REQUIRE(s.GetRaw<int32_t>(r, 0) == expected.GetRaw<int32_t>(r, 0)); // (r+1)*2
        REQUIRE(s.GetRaw<int32_t>(r, 1) == expected.GetRaw<int32_t>(r, 1)); // (r+2)*2
    }
}

TEST_CASE("lens: long conflicting chain on one column stays ordered", "[lens]")
{
    // A chain of conflicting ops on a single column must serialise into N levels in order.
    std::vector<DataStoreColumnSchema> cols = {{"X", DataLensValueType::Int32}};
    const size_t rows = 1000;
    DataStore s(cols, rows);
    for (size_t r = 0; r < rows; ++r)
        s.SetRaw<int32_t>(s.AllocRow(), 0, 0);

    datalens::Lens lens(8);
    std::vector<datalens::SystemDesc> batch;
    for (int k = 0; k < 10; ++k)
        batch.push_back(ScalarSys(s, 0, DataSystemOp::Add, 1)); // 10x +1, all conflict

    lens.RunSystems(batch);
    for (size_t r = 0; r < rows; ++r)
        REQUIRE(s.GetRaw<int32_t>(r, 0) == 10);
}

TEST_CASE("lens: scaled cross-column parallel matches single-threaded", "[lens][scaled]")
{
    const size_t rows = 80000;
    auto build = [&]() {
        std::vector<DataStoreColumnSchema> cols = {{"Pos", DataLensValueType::Float},
                                                   {"Vel", DataLensValueType::Float}};
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<float>(row, 0, static_cast<float>(r % 13));
            s.SetRaw<float>(row, 1, static_cast<float>(r % 5) - 2.0f);
        }
        return s;
    };

    const float dt = 0.25f;
    DataStore serial = build();
    serial.RunColumnSystemScaledColumn<float>(0, DataSystemOp::Add, 1, dt, false, 0, DataCompareOp::Always, 0.0f);

    DataStore parallel = build();
    datalens::Lens lens(8);
    lens.RunSystemScaledColumn<float>(parallel, 0, DataSystemOp::Add, 1, dt, false, 0, DataCompareOp::Always, 0.0f);

    for (size_t r = 0; r < rows; ++r)
        REQUIRE(parallel.GetRaw<float>(r, 0) == serial.GetRaw<float>(r, 0));
}

TEST_CASE("lens: scaled cross-column via SystemDesc in a batch", "[lens][scaled]")
{
    std::vector<DataStoreColumnSchema> cols = {{"Pos", DataLensValueType::Float},
                                               {"Vel", DataLensValueType::Float}};
    DataStore s(cols, 2);
    size_t r0 = s.AllocRow(); s.SetRaw<float>(r0, 0, 0.0f); s.SetRaw<float>(r0, 1, 8.0f);

    datalens::SystemDesc integrate;
    integrate.store = &s; integrate.elemType = DataLensValueType::Float;
    integrate.targetCol = 0; integrate.op = DataSystemOp::Add;
    integrate.operandIsColumn = true; integrate.operandCol = 1;
    integrate.scaleOperand = true; integrate.operandScale = 0.5;

    datalens::Lens lens(4);
    std::vector<datalens::SystemDesc> batch = {integrate};
    size_t affected = lens.RunSystems(batch);
    REQUIRE(affected == 1);
    REQUIRE(s.GetRaw<float>(r0, 0) == 0.0f + 8.0f * 0.5f); // 4
}

TEST_CASE("lens: cross-column System in a batch (Current = min(Current, Max))", "[lens]")
{
    std::vector<DataStoreColumnSchema> cols = {{"Current", DataLensValueType::Int32},
                                               {"Max",     DataLensValueType::Int32}};
    DataStore s(cols, 2);
    size_t r0 = s.AllocRow();
    size_t r1 = s.AllocRow();
    s.SetRaw<int32_t>(r0, 0, 150); s.SetRaw<int32_t>(r0, 1, 100);
    s.SetRaw<int32_t>(r1, 0, 40);  s.SetRaw<int32_t>(r1, 1, 100);

    datalens::SystemDesc clamp;
    clamp.store = &s; clamp.elemType = DataLensValueType::Int32;
    clamp.targetCol = 0; clamp.op = DataSystemOp::Min;
    clamp.operandIsColumn = true; clamp.operandCol = 1;

    datalens::Lens lens(4);
    std::vector<datalens::SystemDesc> batch = {clamp};
    lens.RunSystems(batch);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 100);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 40);
}

// A8: the SystemDesc/RunSystems dispatch must cover every column width, not just Int32/Float, so HATE can
// pack integral attributes to their narrowest stride and still run effects/aggregation/clamp on them.
TEST_CASE("lens: SystemDesc dispatches all column widths (A8 width-complete ops)", "[lens][a8][width]")
{
    datalens::Lens lens(4);

    SECTION("UInt8 scalar Add")
    {
        std::vector<DataStoreColumnSchema> cols = {{"V", DataLensValueType::UInt8}};
        DataStore s(cols, 4);
        for (int i = 0; i < 4; ++i) { size_t r = s.AllocRow(); s.SetRaw<uint8_t>(r, 0, static_cast<uint8_t>(i * 10)); }
        datalens::SystemDesc d;
        d.store = &s; d.elemType = DataLensValueType::UInt8; d.targetCol = 0; d.op = DataSystemOp::Add; d.operand = 5;
        std::vector<datalens::SystemDesc> batch = {d};
        REQUIRE(lens.RunSystems(batch) == 4);
        for (int i = 0; i < 4; ++i) REQUIRE(s.GetRaw<uint8_t>(i, 0) == static_cast<uint8_t>(i * 10 + 5));
    }

    SECTION("UInt16 cross-column Min clamp (HATE clamp primitive on a narrow column)")
    {
        std::vector<DataStoreColumnSchema> cols = {{"Cur", DataLensValueType::UInt16}, {"Max", DataLensValueType::UInt16}};
        DataStore s(cols, 2);
        size_t r0 = s.AllocRow(); size_t r1 = s.AllocRow();
        s.SetRaw<uint16_t>(r0, 0, 5000); s.SetRaw<uint16_t>(r0, 1, 1000);
        s.SetRaw<uint16_t>(r1, 0, 200);  s.SetRaw<uint16_t>(r1, 1, 1000);
        datalens::SystemDesc d;
        d.store = &s; d.elemType = DataLensValueType::UInt16; d.targetCol = 0;
        d.op = DataSystemOp::Min; d.operandIsColumn = true; d.operandCol = 1;
        std::vector<datalens::SystemDesc> batch = {d};
        lens.RunSystems(batch);
        REQUIRE(s.GetRaw<uint16_t>(r0, 0) == 1000);
        REQUIRE(s.GetRaw<uint16_t>(r1, 0) == 200);
    }

    SECTION("UInt64 Mul past the 32-bit range")
    {
        std::vector<DataStoreColumnSchema> cols = {{"V", DataLensValueType::UInt64}};
        DataStore s(cols, 2);
        size_t r0 = s.AllocRow(); size_t r1 = s.AllocRow();
        s.SetRaw<uint64_t>(r0, 0, 5'000'000'000ULL); s.SetRaw<uint64_t>(r1, 0, 1ULL);
        datalens::SystemDesc d;
        d.store = &s; d.elemType = DataLensValueType::UInt64; d.targetCol = 0; d.op = DataSystemOp::Mul; d.operand = 2;
        std::vector<datalens::SystemDesc> batch = {d};
        lens.RunSystems(batch);
        REQUIRE(s.GetRaw<uint64_t>(r0, 0) == 10'000'000'000ULL);
        REQUIRE(s.GetRaw<uint64_t>(r1, 0) == 2ULL);
    }

    SECTION("Double scalar Add")
    {
        std::vector<DataStoreColumnSchema> cols = {{"V", DataLensValueType::Double}};
        DataStore s(cols, 1);
        size_t r0 = s.AllocRow(); s.SetRaw<double>(r0, 0, 1.5);
        datalens::SystemDesc d;
        d.store = &s; d.elemType = DataLensValueType::Double; d.targetCol = 0; d.op = DataSystemOp::Add; d.operand = 0.25;
        std::vector<datalens::SystemDesc> batch = {d};
        lens.RunSystems(batch);
        REQUIRE(s.GetRaw<double>(r0, 0) == 1.75);
    }
}

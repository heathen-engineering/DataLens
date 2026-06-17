// Tick / cadence scheduler (A5): the Lens advances a tick counter and runs scheduled IR programs on
// their cadence and LOD band, turning Simulation LOD into execution frequency.

#include <catch2/catch_test_macros.hpp>

#include "datalens/DataStore.h"
#include "datalens/DataView.h"
#include "datalens/Ir.h"
#include "datalens/Lens.h"

#include <cstdint>
#include <vector>

using datalens::IrProgram;
using datalens::IrSystemOp;

namespace
{
    // Store with `cols` Int32 counter columns, `rows` live rows, all zero.
    DataStore MakeCounters(size_t cols, size_t rows)
    {
        std::vector<DataStoreColumnSchema> schema;
        for (size_t c = 0; c < cols; ++c)
            schema.push_back({"c" + std::to_string(c), DataLensValueType::Int32});
        DataStore s(schema, rows);
        for (size_t r = 0; r < rows; ++r)
            s.AllocRow();
        return s;
    }

    // A program that adds 1 to column `col` of store 0.
    IrProgram Increment(uint32_t col)
    {
        IrProgram p;
        p.Add(IrSystemOp::Scalar(0, DataLensValueType::Int32, col, DataSystemOp::Add, 1));
        return p;
    }
}

TEST_CASE("schedule: cadence runs programs at their period", "[schedule]")
{
    DataStore s = MakeCounters(2, 100);
    DataStore* stores[] = {&s};

    datalens::Lens lens(4);
    lens.AddScheduledProgram(Increment(0), 1);  // every tick
    lens.AddScheduledProgram(Increment(1), 4);  // every 4th tick (4, 8)
    REQUIRE(lens.ScheduledProgramCount() == 2);

    for (int t = 0; t < 8; ++t)
        lens.Tick(stores, 1);

    REQUIRE(lens.CurrentTick() == 8);
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 8); // ran every tick
    REQUIRE(s.GetRaw<int32_t>(0, 1) == 2); // ran on ticks 4 and 8
}

TEST_CASE("schedule: phase offsets which tick a program fires on", "[schedule]")
{
    DataStore s = MakeCounters(2, 10);
    DataStore* stores[] = {&s};

    datalens::Lens lens(2);
    lens.AddScheduledProgram(Increment(0), 4, 0, 255, /*phase*/0); // ticks 4, 8
    lens.AddScheduledProgram(Increment(1), 4, 0, 255, /*phase*/1); // ticks 1, 5

    for (int t = 0; t < 8; ++t)
        lens.Tick(stores, 1);

    REQUIRE(s.GetRaw<int32_t>(0, 0) == 2); // 4, 8
    REQUIRE(s.GetRaw<int32_t>(0, 1) == 2); // 1, 5
}

TEST_CASE("schedule: LOD band scopes a tiered tick", "[schedule][lod]")
{
    // 6 rows cycling LOD 0,1,2. A LOD-0 program every tick; a LOD-2 program every 4 ticks.
    DataStore s = MakeCounters(1, 6);
    for (size_t r = 0; r < 6; ++r)
        s.SetLod(r, static_cast<uint8_t>(r % 3));
    DataStore* stores[] = {&s};

    datalens::Lens lens(4);
    lens.AddScheduledProgram(Increment(0), 1, /*minLod*/0, /*maxLod*/0); // LOD 0 only, every tick
    lens.AddScheduledProgram(Increment(0), 4, /*minLod*/2, /*maxLod*/2); // LOD 2 only, every 4th

    for (int t = 0; t < 4; ++t)
        lens.Tick(stores, 1);

    // Rows: index 0->LOD0, 1->LOD1, 2->LOD2, 3->LOD0, 4->LOD1, 5->LOD2.
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 4); // LOD0: +1 each of 4 ticks
    REQUIRE(s.GetRaw<int32_t>(1, 0) == 0); // LOD1: never scheduled
    REQUIRE(s.GetRaw<int32_t>(2, 0) == 1); // LOD2: +1 on tick 4 only
    REQUIRE(s.GetRaw<int32_t>(3, 0) == 4);
    REQUIRE(s.GetRaw<int32_t>(5, 0) == 1);
}

TEST_CASE("schedule: period clamps to 1; empty schedule is a no-op", "[schedule]")
{
    DataStore s = MakeCounters(1, 4);
    DataStore* stores[] = {&s};

    datalens::Lens lens(2);
    REQUIRE(lens.Tick(stores, 1) == 0); // empty schedule
    REQUIRE(lens.CurrentTick() == 1);

    lens.AddScheduledProgram(Increment(0), 0); // period 0 -> clamped to 1
    lens.ResetTick();
    for (int t = 0; t < 3; ++t)
        lens.Tick(stores, 1);
    REQUIRE(s.GetRaw<int32_t>(0, 0) == 3);

    lens.ClearSchedule();
    REQUIRE(lens.ScheduledProgramCount() == 0);
    int32_t before = s.GetRaw<int32_t>(0, 0);
    lens.Tick(stores, 1);
    REQUIRE(s.GetRaw<int32_t>(0, 0) == before); // nothing scheduled
}

TEST_CASE("schedule: a tick's affected count sums the due programs", "[schedule]")
{
    DataStore s = MakeCounters(2, 50);
    DataStore* stores[] = {&s};

    datalens::Lens lens(4);
    lens.AddScheduledProgram(Increment(0), 1); // 50 rows every tick
    lens.AddScheduledProgram(Increment(1), 2); // 50 rows every 2nd tick

    REQUIRE(lens.Tick(stores, 1) == 50);  // tick 1: only program 0 due
    REQUIRE(lens.Tick(stores, 1) == 100); // tick 2: both due
}

TEST_CASE("schedule: Tick refreshes scheduled views AFTER running Systems", "[schedule][dataview]")
{
    DataStore s = MakeCounters(1, 4);
    DataStore* stores[] = {&s};

    datalens::DataView view({0});
    datalens::Lens lens(2);
    lens.AddScheduledProgram(Increment(0), 1);        // +1 to col0 every tick
    lens.AddScheduledView(&view, 0, 1);               // refresh view every tick
    REQUIRE(lens.ScheduledViewCount() == 1);

    lens.Tick(stores, 1);
    // The view must reflect THIS tick's write (Systems run before view refresh).
    REQUIRE(view.RowCount() == 4);
    REQUIRE(view.Get<int32_t>(0, 0) == 1);

    lens.Tick(stores, 1);
    REQUIRE(view.Get<int32_t>(0, 0) == 2);
}

TEST_CASE("schedule: scheduled view refreshes only on its cadence", "[schedule][dataview]")
{
    DataStore s = MakeCounters(1, 2);
    DataStore* stores[] = {&s};

    datalens::DataView view({0});
    datalens::Lens lens(2);
    lens.AddScheduledProgram(Increment(0), 1); // store advances every tick
    lens.AddScheduledView(&view, 0, 3);        // but the view only refreshes every 3rd tick

    lens.Tick(stores, 1); // store col0 = 1, view NOT refreshed yet (empty)
    REQUIRE(view.RowCount() == 0);

    lens.Tick(stores, 1); // store col0 = 2, still not refreshed
    lens.Tick(stores, 1); // store col0 = 3, refreshed now
    REQUIRE(view.RowCount() == 2);
    REQUIRE(view.Get<int32_t>(0, 0) == 3); // snapshot at tick 3
}

TEST_CASE("schedule: LOD-banded scheduled view only materialises that tier", "[schedule][dataview][lod]")
{
    DataStore s = MakeCounters(1, 6);
    for (size_t r = 0; r < 6; ++r)
        s.SetLod(r, static_cast<uint8_t>(r % 3));
    DataStore* stores[] = {&s};

    datalens::DataView view({0});
    datalens::Lens lens(2);
    lens.AddScheduledViewInLodBand(&view, 0, 1, /*minLod*/2, /*maxLod*/2);

    lens.Tick(stores, 1);
    REQUIRE(view.RowCount() == 2);       // only LOD-2 rows (indices 2 and 5)
    REQUIRE(view.SourceRow(0) == 2);
    REQUIRE(view.SourceRow(1) == 5);
}

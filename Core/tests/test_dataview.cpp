// Read-only DataView (A5): a row-major snapshot of selected columns of a store's live rows. A copy,
// not an alias — it stays valid (and unchanged) while the store is mutated after a Refresh.

#include <catch2/catch_test_macros.hpp>

#include "TestTags.h"

#include "datalens/DataStore.h"
#include "datalens/DataView.h"
#include "datalens/Lens.h"

#include <cstdint>
#include <vector>

namespace
{
    // Store: [Health(float), Team(int32), Mana(float)], `rows` live rows seeded by index.
    DataStore MakeActors(size_t rows)
    {
        std::vector<DataStoreColumnSchema> cols = {{Tag("Health"), DataLensValueType::Float},
                                                   {Tag("Team"),   DataLensValueType::Int32},
                                                   {Tag("Mana"),   DataLensValueType::Float}};
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<float>(row, 0, static_cast<float>(r));
            s.SetRaw<int32_t>(row, 1, static_cast<int32_t>(r % 2));
            s.SetRaw<float>(row, 2, static_cast<float>(r) * 0.5f);
        }
        return s;
    }
}

TEST_CASE("dataview: materialises selected columns row-major", "[dataview]")
{
    DataStore s = MakeActors(5);

    // View of Health (col 0) and Mana (col 2) only — note column 1 (Team) is excluded.
    datalens::DataView view({0, 2});
    view.Refresh(s);

    REQUIRE(view.RowCount() == 5);
    REQUIRE(view.ColumnCount() == 2);
    REQUIRE(view.RowStride() == 8); // 4 (Health) + 4 (Mana)
    REQUIRE(view.SourceColumn(0) == 0);
    REQUIRE(view.SourceColumn(1) == 2);

    for (size_t r = 0; r < 5; ++r)
    {
        REQUIRE(view.SourceRow(r) == r);
        REQUIRE(view.Get<float>(r, 0) == static_cast<float>(r));        // Health
        REQUIRE(view.Get<float>(r, 1) == static_cast<float>(r) * 0.5f); // Mana
    }
}

TEST_CASE("dataview: is a snapshot — store mutations after Refresh don't leak in", "[dataview]")
{
    DataStore s = MakeActors(3);
    datalens::DataView view({0});
    view.Refresh(s);
    REQUIRE(view.Get<float>(1, 0) == 1.0f);

    // Mutate the store after the snapshot was taken.
    s.SetRaw<float>(1, 0, 999.0f);
    REQUIRE(view.Get<float>(1, 0) == 1.0f); // view still shows the snapshot value

    view.Refresh(s);
    REQUIRE(view.Get<float>(1, 0) == 999.0f); // now it sees the update
}

TEST_CASE("dataview: only live rows are included, and mapping tracks the source row", "[dataview]")
{
    DataStore s = MakeActors(5);
    s.FreeRow(1);
    s.FreeRow(3);

    datalens::DataView view({1}); // Team column
    view.Refresh(s);

    REQUIRE(view.RowCount() == 3);            // rows 0, 2, 4
    REQUIRE(view.SourceRow(0) == 0);
    REQUIRE(view.SourceRow(1) == 2);
    REQUIRE(view.SourceRow(2) == 4);
    REQUIRE(view.Get<int32_t>(0, 0) == 0);    // Team of row 0 = 0%2
    REQUIRE(view.Get<int32_t>(1, 0) == 0);    // Team of row 2 = 2%2
    REQUIRE(view.Get<int32_t>(2, 0) == 0);    // Team of row 4 = 4%2
}

TEST_CASE("dataview: RefreshInLodBand restricts to a relevance band", "[dataview][lod]")
{
    DataStore s = MakeActors(6);
    for (size_t r = 0; r < 6; ++r)
        s.SetLod(r, static_cast<uint8_t>(r % 3)); // 0,1,2,0,1,2

    datalens::DataView view({0});
    view.RefreshInLodBand(s, 0, 0); // only LOD-0 rows (indices 0 and 3)

    REQUIRE(view.RowCount() == 2);
    REQUIRE(view.SourceRow(0) == 0);
    REQUIRE(view.SourceRow(1) == 3);
}

TEST_CASE("dataview: RowData exposes the contiguous row bytes", "[dataview]")
{
    DataStore s = MakeActors(4);
    datalens::DataView view({1, 0}); // Team(int32) then Health(float) — reordered vs the store
    view.Refresh(s);

    REQUIRE(view.RowStride() == 8);
    for (size_t r = 0; r < 4; ++r)
    {
        const uint8_t* row = view.RowData(r);
        int32_t team; float health;
        std::memcpy(&team, row + 0, sizeof(int32_t));
        std::memcpy(&health, row + 4, sizeof(float));
        REQUIRE(team == static_cast<int32_t>(r % 2));
        REQUIRE(health == static_cast<float>(r));
    }
}

TEST_CASE("dataview: Data()/ByteSize() expose the contiguous snapshot for bulk reads", "[dataview]")
{
    DataStore s = MakeActors(4);
    datalens::DataView view({0, 2}); // Health, Mana (both float)
    view.Refresh(s);

    REQUIRE(view.ByteSize() == view.RowCount() * view.RowStride()); // 4 * 8
    const float* f = reinterpret_cast<const float*>(view.Data());
    // Row-major [health0, mana0, health1, mana1, ...].
    REQUIRE(f[0] == 0.0f);        // health row 0
    REQUIRE(f[1] == 0.0f);        // mana   row 0
    REQUIRE(f[2] == 1.0f);        // health row 1
    REQUIRE(f[3] == 0.5f);        // mana   row 1
    REQUIRE(f[6] == 3.0f);        // health row 3
}

TEST_CASE("dataview: parallel Lens::RefreshView matches serial Refresh", "[dataview][lens]")
{
    const size_t rows = 40000;
    auto build = [&]() {
        std::vector<DataStoreColumnSchema> cols = {{Tag("X"), DataLensValueType::Float},
                                                   {Tag("Y"), DataLensValueType::Float}};
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<float>(row, 0, static_cast<float>(r));
            s.SetRaw<float>(row, 1, static_cast<float>(r) * 2.0f);
        }
        if (rows > 10) s.FreeRow(7); // a hole, so live-row compaction is exercised
        return s;
    };

    DataStore s1 = build();
    DataStore s2 = build();

    datalens::DataView serial({0, 1});
    serial.Refresh(s1);

    datalens::DataView parallel({0, 1});
    datalens::Lens lens(8);
    lens.RefreshView(parallel, s2);

    REQUIRE(parallel.RowCount() == serial.RowCount());
    for (size_t vr = 0; vr < serial.RowCount(); ++vr)
    {
        REQUIRE(parallel.SourceRow(vr) == serial.SourceRow(vr));
        REQUIRE(parallel.Get<float>(vr, 0) == serial.Get<float>(vr, 0));
        REQUIRE(parallel.Get<float>(vr, 1) == serial.Get<float>(vr, 1));
    }
}

TEST_CASE("dataview: out-of-range source column degrades to zero width", "[dataview]")
{
    DataStore s = MakeActors(3);
    datalens::DataView view({0, 99}); // 99 doesn't exist
    view.Refresh(s);

    REQUIRE(view.RowCount() == 3);
    REQUIRE(view.RowStride() == 4); // only the valid Health column contributes width
    REQUIRE(view.Get<float>(0, 0) == 0.0f);
    REQUIRE(view.Get<float>(2, 0) == 2.0f);
}

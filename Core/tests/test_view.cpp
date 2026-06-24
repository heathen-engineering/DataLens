// View Core read (DataLens-Spec.md §6.4): projection + index joins + scope -> raw payload + change flags.

#include <catch2/catch_test_macros.hpp>

#include "datalens/DataStore.h"
#include "datalens/Lens.h"
#include "datalens/View.h"

#include <vector>

using namespace datalens;

namespace
{
    // Build a store with the given column types and `rows` allocated (valid) rows. Column tags are
    // irrelevant to the Core View (it is index-addressed); use 1-based placeholders.
    DataStore MakeStore(const std::vector<DataLensValueType>& types, std::size_t rows)
    {
        std::vector<DataStoreColumnSchema> cols;
        for (std::size_t i = 0; i < types.size(); ++i)
            cols.push_back({static_cast<DataLensId>(i + 1), types[i]});
        DataStore s(cols, rows);
        for (std::size_t r = 0; r < rows; ++r)
            s.AllocRow();
        return s;
    }

    // Predicate-program builders (DataLens-Spec §6.4.1 RPN).
    ViewPredicate Leaf(std::size_t source, std::size_t column, DataLensValueType type,
                       DataCompareOp op, std::int64_t iv)
    {
        ViewPredicate p;
        p.Kind = ViewPredicateKind::Leaf;
        p.Source = source; p.Column = column; p.Type = type; p.Op = op; p.IValue = iv;
        return p;
    }
    ViewPredicate RangeLeaf(std::size_t source, std::size_t column, DataLensValueType type,
                            std::int64_t lo, std::int64_t hi)
    {
        ViewPredicate p;
        p.Kind = ViewPredicateKind::Leaf; p.Range = true;
        p.Source = source; p.Column = column; p.Type = type; p.IValue = lo; p.IHi = hi;
        return p;
    }
    ViewPredicate Conn(ViewPredicateKind k)
    {
        ViewPredicate p; p.Kind = k; return p;
    }
}

TEST_CASE("view read: projection + scope over a single store", "[view]")
{
    DataStore actors = MakeStore({DataLensValueType::Int32}, 3); // col0 = Health
    actors.SetRaw<std::int32_t>(0, 0, 100);
    actors.SetRaw<std::int32_t>(1, 0, 30);
    actors.SetRaw<std::int32_t>(2, 0, 80);

    // SELECT Health WHERE Health >= 50.
    View v(/*base*/ 0, /*joins*/ {}, /*columns*/ {{0, 0}},
           /*scope*/ {{0, DataLensValueType::Int32, DataCompareOp::GreaterEqual, 50, 0.0}});
    v.Refresh({&actors});

    REQUIRE(v.RowCount() == 2);          // rows 0 and 2 pass
    REQUIRE(v.ColumnCount() == 1);
    REQUIRE(v.RowStride() == 4);
    REQUIRE(v.Get<std::int32_t>(0, 0) == 100);
    REQUIRE(v.Get<std::int32_t>(1, 0) == 80);
    REQUIRE(v.State(0) == ViewRowState::Unchanged);
    REQUIRE(v.SourceBaseRow(0) == 0);
    REQUIRE(v.SourceBaseRow(1) == 2);    // the filtered row 1 is skipped
}

TEST_CASE("view read: predicate program (And/Or/Not + fused Range)", "[view]")
{
    // col0 = Health, col1 = Faction.
    DataStore actors = MakeStore({DataLensValueType::Int32, DataLensValueType::Int32}, 4);
    const std::int32_t health[]  = {100, 30, 80, 10};
    const std::int32_t faction[] = {1,   2,  2,  1};
    for (std::size_t r = 0; r < 4; ++r)
    {
        actors.SetRaw<std::int32_t>(r, 0, health[r]);
        actors.SetRaw<std::int32_t>(r, 1, faction[r]);
    }
    const auto I = DataLensValueType::Int32;

    SECTION("OR of two base leaves")
    {
        // (Health >= 90) OR (Faction == 2)  ->  rows 0,1,2
        View v(0, {}, {{0, 0}});
        v.SetScopeProgram({Leaf(0, 0, I, DataCompareOp::GreaterEqual, 90),
                           Leaf(0, 1, I, DataCompareOp::Equal, 2),
                           Conn(ViewPredicateKind::Or)});
        v.Refresh({&actors});
        REQUIRE(v.RowCount() == 3);
        REQUIRE(v.SourceBaseRow(0) == 0);
        REQUIRE(v.SourceBaseRow(1) == 1);
        REQUIRE(v.SourceBaseRow(2) == 2);
    }

    SECTION("NOT of a leaf")
    {
        // NOT(Faction == 2)  ->  rows 0,3
        View v(0, {}, {{0, 0}});
        v.SetScopeProgram({Leaf(0, 1, I, DataCompareOp::Equal, 2), Conn(ViewPredicateKind::Not)});
        v.Refresh({&actors});
        REQUIRE(v.RowCount() == 2);
        REQUIRE(v.SourceBaseRow(0) == 0);
        REQUIRE(v.SourceBaseRow(1) == 3);
    }

    SECTION("fused Range leaf")
    {
        // Health in [20, 90]  ->  rows 1,2
        View v(0, {}, {{0, 0}});
        v.SetScopeProgram({RangeLeaf(0, 0, I, 20, 90)});
        v.Refresh({&actors});
        REQUIRE(v.RowCount() == 2);
        REQUIRE(v.Get<std::int32_t>(0, 0) == 30);
        REQUIRE(v.Get<std::int32_t>(1, 0) == 80);
    }

    SECTION("nested: (Health >= 90) OR (Faction == 2 AND Health >= 50)")
    {
        View v(0, {}, {{0, 0}});
        v.SetScopeProgram({Leaf(0, 0, I, DataCompareOp::GreaterEqual, 90),
                           Leaf(0, 1, I, DataCompareOp::Equal, 2),
                           Leaf(0, 0, I, DataCompareOp::GreaterEqual, 50),
                           Conn(ViewPredicateKind::And),
                           Conn(ViewPredicateKind::Or)});
        v.Refresh({&actors});
        REQUIRE(v.RowCount() == 2);    // rows 0 and 2
        REQUIRE(v.SourceBaseRow(0) == 0);
        REQUIRE(v.SourceBaseRow(1) == 2);
    }
}

TEST_CASE("view read: predicate program on a dereferenced (post-join) column", "[view]")
{
    // Catalogue -> Trait dereference; filter on the joined column, absent join row -> leaf false.
    DataStore cat   = MakeStore({DataLensValueType::Int32}, 3);
    DataStore trait = MakeStore({DataLensValueType::Int32}, 2); // col0 = Power
    cat.SetRaw<std::int32_t>(0, 0, 0);          // -> trait row 0
    cat.SetRaw<std::int32_t>(1, 0, 0x7FFFFFFF); // -> absent
    cat.SetRaw<std::int32_t>(2, 0, 1);          // -> trait row 1
    trait.SetRaw<std::int32_t>(0, 0, 5);
    trait.SetRaw<std::int32_t>(1, 0, 20);

    ViewJoin deref;
    deref.TargetStore = 1;
    deref.Aligned = false;
    deref.IndexColumn = 0;
    View v(0, {deref}, {{1, 0}}); // project trait.Power
    // WHERE trait.Power > 10  (source 1 = the join target)
    v.SetScopeProgram({Leaf(1, 0, DataLensValueType::Int32, DataCompareOp::Greater, 10)});
    v.Refresh({&cat, &trait});

    REQUIRE(v.RowCount() == 1);                  // only entity 2 (Power 20); entity 0 fails, entity 1 absent
    REQUIRE(v.Get<std::int32_t>(0, 0) == 20);
    REQUIRE(v.SourceBaseRow(0) == 2);
}

TEST_CASE("view read: index-aligned join across two stores", "[view]")
{
    DataStore core = MakeStore({DataLensValueType::Int32}, 3); // col0 = Health
    DataStore mart = MakeStore({DataLensValueType::Float}, 3); // col0 = Stamina, row-aligned to core
    for (std::int32_t i = 0; i < 3; ++i)
    {
        core.SetRaw<std::int32_t>(i, 0, 100 + i);
        mart.SetRaw<float>(i, 0, 1.0f * i);
    }

    // SELECT core.Health, mart.Stamina  (mart aligned to core row).
    ViewJoin aligned;
    aligned.TargetStore = 1;
    aligned.Aligned = true;
    View v(0, {aligned}, {{0, 0}, {1, 0}});
    v.Refresh({&core, &mart});

    REQUIRE(v.RowCount() == 3);
    REQUIRE(v.RowStride() == 8); // Int32 + Float
    REQUIRE(v.Get<std::int32_t>(1, 0) == 101);
    REQUIRE(v.Get<float>(1, 1) == 1.0f); // mart stamina row 1 = 1.0f*1
    REQUIRE(v.SourceJoinRow(2, 0) == 2);
}

TEST_CASE("view read: index-dereference join (the catalogue) with absent sentinel", "[view]")
{
    // Catalogue: one row per entity; col0 holds the row index into the Magic trait store (or int32.Max).
    DataStore cat   = MakeStore({DataLensValueType::Int32}, 3);
    DataStore magic = MakeStore({DataLensValueType::Float}, 2); // col0 = Mana, only two entities have it
    cat.SetRaw<std::int32_t>(0, 0, 0);            // entity 0 -> magic row 0
    cat.SetRaw<std::int32_t>(1, 0, 0x7FFFFFFF);   // entity 1 -> absent
    cat.SetRaw<std::int32_t>(2, 0, 1);            // entity 2 -> magic row 1
    magic.SetRaw<float>(0, 0, 50.0f);
    magic.SetRaw<float>(1, 0, 75.0f);

    // SELECT magic.Mana  (dereference cat.col0 into the magic store; default sentinel = int32.Max).
    ViewJoin deref;
    deref.TargetStore = 1;
    deref.Aligned = false;
    deref.IndexColumn = 0;
    View v(0, {deref}, {{1, 0}});
    v.Refresh({&cat, &magic});

    REQUIRE(v.RowCount() == 3);
    REQUIRE(v.Get<float>(0, 0) == 50.0f);
    REQUIRE(v.Get<float>(1, 0) == 0.0f);   // absent -> default (zero)
    REQUIRE(v.Get<float>(2, 0) == 75.0f);
    REQUIRE(v.SourceJoinRow(0, 0) == 0);
    REQUIRE(v.SourceJoinRow(1, 0) == View::NoRow); // absent
    REQUIRE(v.SourceJoinRow(2, 0) == 1);
}

TEST_CASE("view write-back: Update commits to a different column (read != write)", "[view]")
{
    DataStore actors = MakeStore({DataLensValueType::Int32, DataLensValueType::Int32}, 3); // Health, Shield
    actors.SetRaw<std::int32_t>(0, 0, 100);
    actors.SetRaw<std::int32_t>(1, 0, 50);
    actors.SetRaw<std::int32_t>(2, 0, 80);

    View v(0, {}, {{0, 0}}); // project Health (col0)
    v.Refresh({&actors});

    // The consumer edits row 1's projected value and marks it Modified.
    v.Set<std::int32_t>(1, 0, 999);
    v.SetState(1, ViewRowState::Modified);

    // Write-back: view col0 -> store0 col1 (Shield) — read Health, write Shield.
    ViewWriteBack wb;
    wb.Update.push_back({/*viewCol*/ 0, /*store*/ 0, /*col*/ 1});
    v.SetWriteBack(wb);

    std::vector<DataStore*> stores{&actors};
    REQUIRE(v.Commit(stores) == 1); // only the one Modified row

    REQUIRE(actors.GetRaw<std::int32_t>(1, 1) == 999); // Shield got the edited value
    REQUIRE(actors.GetRaw<std::int32_t>(1, 0) == 50);  // Health (read source) untouched
    REQUIRE(actors.GetRaw<std::int32_t>(0, 1) == 0);   // unchanged rows not written
}

TEST_CASE("view write-back: Insert allocates and writes a fresh record", "[view]")
{
    // A store with capacity 4 but no live rows yet (prealloc without AllocRow).
    DataStore store(std::vector<DataStoreColumnSchema>{{1, DataLensValueType::Int32}}, /*prealloc*/ 4);

    View v(0, {}, {{0, 0}});
    v.Refresh({&store});
    REQUIRE(v.RowCount() == 0); // no live rows yet

    const std::size_t vr = v.AddRow();
    v.Set<std::int32_t>(vr, 0, 42);

    ViewWriteBack wb;
    wb.Insert.push_back({0, 0, 0});
    v.SetWriteBack(wb);

    std::vector<DataStore*> stores{&store};
    REQUIRE(v.Commit(stores) == 1);
    REQUIRE(store.GetLiveCount() == 1);
    REQUIRE(store.GetRaw<std::int32_t>(0, 0) == 42);
}

TEST_CASE("view write-back: Delete frees the sourced record", "[view]")
{
    DataStore actors = MakeStore({DataLensValueType::Int32}, 3);
    View v(0, {}, {{0, 0}});
    v.Refresh({&actors});
    REQUIRE(actors.GetLiveCount() == 3);

    v.SetState(1, ViewRowState::Removed);
    ViewWriteBack wb;
    wb.Delete.push_back(0);
    v.SetWriteBack(wb);

    std::vector<DataStore*> stores{&actors};
    REQUIRE(v.Commit(stores) == 1);
    REQUIRE(actors.GetLiveCount() == 2);
    REQUIRE_FALSE(actors.IsValidRow(1)); // the sourced base row freed
    REQUIRE(actors.IsValidRow(0));
}

TEST_CASE("view write-back: linked insert wires the catalogue index to the new trait row", "[view]")
{
    // Catalogue (base) col0 = trait index; Trait store col0 = Power. Prealloc capacity, no live rows yet.
    DataStore cat(std::vector<DataStoreColumnSchema>{{1, DataLensValueType::Int32}}, /*prealloc*/ 4);
    DataStore trait(std::vector<DataStoreColumnSchema>{{2, DataLensValueType::Int32}}, /*prealloc*/ 4);
    // pre-occupy trait row 0 so the new trait row is row 1 (proves the index is the real allocated row).
    trait.AllocRow();
    trait.SetRaw<std::int32_t>(0, 0, 999);

    ViewJoin deref;
    deref.TargetStore = 1;
    deref.Aligned = false;
    deref.IndexColumn = 0; // base col0 holds the trait row index
    View v(0, {deref}, {{0, 0}, {1, 0}}); // project cat.index (col0) + trait.Power (col0)

    ViewWriteBack wb;
    wb.Insert.push_back({0, 0, 0}); // viewCol0 -> cat (store0) col0
    wb.Insert.push_back({1, 1, 0}); // viewCol1 -> trait (store1) col0
    v.SetWriteBack(wb);
    v.Refresh({&cat, &trait});

    const std::size_t nr = v.AddRow();
    v.Set<std::int32_t>(nr, 1, 42); // new trait Power = 42 (index col left as default/garbage)

    std::vector<DataStore*> stores{&cat, &trait};
    REQUIRE(v.Commit(stores) >= 2);

    REQUIRE(cat.GetLiveCount() == 1);
    REQUIRE(trait.GetLiveCount() == 2);                 // pre-occupied row 0 + new row 1
    const std::int32_t newTraitRow = cat.GetRaw<std::int32_t>(0, 0);
    REQUIRE(newTraitRow == 1);                          // linked: catalogue points at the allocated trait row
    REQUIRE(trait.GetRaw<std::int32_t>(1, 0) == 42);    // the new trait record's Power
    REQUIRE(v.SourceBaseRow(nr) == 0);                  // source map updated post-insert: new catalogue row
    REQUIRE(v.SourceJoinRow(nr, 0) == 1);               // and the new trait row
}

TEST_CASE("view tick: the Lens commits then re-hydrates a scheduled read/write view", "[view]")
{
    DataStore actors = MakeStore({DataLensValueType::Int32}, 2); // col0 = Health
    actors.SetRaw<std::int32_t>(0, 0, 10);
    actors.SetRaw<std::int32_t>(1, 0, 20);

    View v(0, {}, {{0, 0}});
    ViewWriteBack wb;
    wb.Update.push_back({0, 0, 0}); // viewCol0 -> store0 col0 (write Health back)
    v.SetWriteBack(wb);
    v.Refresh({&actors});

    // The consumer edits the snapshot between ticks.
    v.Set<std::int32_t>(0, 0, 100);
    v.SetState(0, ViewRowState::Modified);

    datalens::Lens lens(1);
    SECTION("period 1 commits + re-hydrates each tick")
    {
        lens.AddScheduledRwView(&v, /*period*/ 1);
        DataStore* table[] = {&actors};
        lens.Tick(table, 1);

        REQUIRE(actors.GetRaw<std::int32_t>(0, 0) == 100); // committed
        REQUIRE(v.Get<std::int32_t>(0, 0) == 100);          // re-hydrated
        REQUIRE(v.State(0) == ViewRowState::Unchanged);     // refresh resets the change flag
    }
    SECTION("period 2 skips the off-cadence tick")
    {
        lens.AddScheduledRwView(&v, /*period*/ 2);
        DataStore* table[] = {&actors};
        lens.Tick(table, 1);                                // tick 1: not due (1 % 2 != 0)
        REQUIRE(actors.GetRaw<std::int32_t>(0, 0) == 10);   // not committed yet
        lens.Tick(table, 1);                                // tick 2: due
        REQUIRE(actors.GetRaw<std::int32_t>(0, 0) == 100);
    }
}

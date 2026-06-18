// Argmax-across-columns (A3.13): the HATE-Spec §8.5 selection "pick". For each live row, reduce K score
// columns to the INDEX of the largest score and write it into a Choice column. Ties resolve to the lowest
// index (determinism); a winning score below a floor writes a noChoice sentinel. One branchless pass.

#include <catch2/catch_test_macros.hpp>

#include "datalens/DataStore.h"
#include "datalens/Lens.h"

#include <vector>
#include <limits>

namespace
{
    // n live rows, three Score columns (0,1,2) and an Int32 Choice column (3).
    DataStore MakeChoiceStore(size_t n)
    {
        std::vector<DataStoreColumnSchema> cols = {
            {"S0",     DataLensValueType::Float},
            {"S1",     DataLensValueType::Float},
            {"S2",     DataLensValueType::Float},
            {"Choice", DataLensValueType::Int32},
        };
        DataStore s(cols, n);
        for (size_t r = 0; r < n; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<float>(row, 0, 0.0f);
            s.SetRaw<float>(row, 1, 0.0f);
            s.SetRaw<float>(row, 2, 0.0f);
            s.SetRaw<int32_t>(row, 3, -99);
        }
        return s;
    }

    const size_t kScores[3] = {0, 1, 2};
    constexpr size_t ChoiceCol = 3;
    constexpr float kNoFloor = -std::numeric_limits<float>::infinity();
}

TEST_CASE("argmax: picks the index of the largest score per row", "[argmax]")
{
    DataStore s = MakeChoiceStore(3);
    s.SetRaw<float>(0, 0, 0.9f); s.SetRaw<float>(0, 1, 0.1f); s.SetRaw<float>(0, 2, 0.3f); // -> 0
    s.SetRaw<float>(1, 0, 0.1f); s.SetRaw<float>(1, 1, 0.1f); s.SetRaw<float>(1, 2, 0.8f); // -> 2
    s.SetRaw<float>(2, 0, 0.2f); s.SetRaw<float>(2, 1, 0.7f); s.SetRaw<float>(2, 2, 0.5f); // -> 1

    const size_t w = s.RunArgmaxColumns<float>(ChoiceCol, kScores, 3, kNoFloor, -1);
    REQUIRE(w == 3);
    REQUIRE(s.GetRaw<int32_t>(0, ChoiceCol) == 0);
    REQUIRE(s.GetRaw<int32_t>(1, ChoiceCol) == 2);
    REQUIRE(s.GetRaw<int32_t>(2, ChoiceCol) == 1);
}

TEST_CASE("argmax: ties resolve to the lowest index", "[argmax]")
{
    DataStore s = MakeChoiceStore(2);
    s.SetRaw<float>(0, 0, 0.5f); s.SetRaw<float>(0, 1, 0.5f); s.SetRaw<float>(0, 2, 0.5f); // all equal -> 0
    s.SetRaw<float>(1, 0, 0.2f); s.SetRaw<float>(1, 1, 0.9f); s.SetRaw<float>(1, 2, 0.9f); // tie 1&2 -> 1

    s.RunArgmaxColumns<float>(ChoiceCol, kScores, 3, kNoFloor, -1);
    REQUIRE(s.GetRaw<int32_t>(0, ChoiceCol) == 0);
    REQUIRE(s.GetRaw<int32_t>(1, ChoiceCol) == 1);
}

TEST_CASE("argmax: a score floor below the best writes the noChoice sentinel", "[argmax]")
{
    DataStore s = MakeChoiceStore(2);
    s.SetRaw<float>(0, 0, 0.05f); s.SetRaw<float>(0, 1, 0.02f); s.SetRaw<float>(0, 2, 0.01f); // best 0.05 < 0.1 -> -1
    s.SetRaw<float>(1, 0, 0.05f); s.SetRaw<float>(1, 1, 0.40f); s.SetRaw<float>(1, 2, 0.01f); // best 0.40 >= 0.1 -> 1

    s.RunArgmaxColumns<float>(ChoiceCol, kScores, 3, /*minScore*/ 0.1f, /*noChoice*/ -1);
    REQUIRE(s.GetRaw<int32_t>(0, ChoiceCol) == -1);
    REQUIRE(s.GetRaw<int32_t>(1, ChoiceCol) == 1);
}

TEST_CASE("argmax: only live rows are written; K=0 is a no-op sentinel fill", "[argmax]")
{
    DataStore s = MakeChoiceStore(4);
    for (size_t r = 0; r < 4; ++r) { s.SetRaw<float>(r, 1, 1.0f); } // S1 wins for everyone
    s.FreeRow(2); // a hole, Choice left at -99

    const size_t w = s.RunArgmaxColumns<float>(ChoiceCol, kScores, 3, kNoFloor, -1);
    REQUIRE(w == 3);
    REQUIRE(s.GetRaw<int32_t>(0, ChoiceCol) == 1);
    REQUIRE(s.GetRaw<int32_t>(1, ChoiceCol) == 1);
    REQUIRE(s.GetRaw<int32_t>(2, ChoiceCol) == -99); // freed row untouched
    REQUIRE(s.GetRaw<int32_t>(3, ChoiceCol) == 1);

    // K=0: every live row gets noChoice.
    DataStore z = MakeChoiceStore(2);
    z.RunArgmaxColumns<float>(ChoiceCol, kScores, 0, kNoFloor, -1);
    REQUIRE(z.GetRaw<int32_t>(0, ChoiceCol) == -1);
    REQUIRE(z.GetRaw<int32_t>(1, ChoiceCol) == -1);
}

TEST_CASE("argmax: parallel (Lens) result is identical to serial", "[argmax]")
{
    constexpr size_t N = 5000;
    DataStore serial   = MakeChoiceStore(N);
    DataStore parallel = MakeChoiceStore(N);
    for (size_t r = 0; r < N; ++r)
    {
        // A deterministic, varied score pattern so the winner rotates across columns.
        const float a = static_cast<float>((r * 7) % 11);
        const float b = static_cast<float>((r * 5) % 11);
        const float c = static_cast<float>((r * 3) % 11);
        serial.SetRaw<float>(r, 0, a);   parallel.SetRaw<float>(r, 0, a);
        serial.SetRaw<float>(r, 1, b);   parallel.SetRaw<float>(r, 1, b);
        serial.SetRaw<float>(r, 2, c);   parallel.SetRaw<float>(r, 2, c);
    }

    serial.RunArgmaxColumns<float>(ChoiceCol, kScores, 3, kNoFloor, -1);

    datalens::Lens lens(8);
    lens.RunSystemArgmaxColumns<float>(parallel, ChoiceCol, kScores, 3, kNoFloor, -1);

    for (size_t r = 0; r < N; ++r)
        REQUIRE(serial.GetRaw<int32_t>(r, ChoiceCol) == parallel.GetRaw<int32_t>(r, ChoiceCol));
}

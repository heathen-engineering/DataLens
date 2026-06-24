// Counter-based noise (A3.12): a stateless PRNG keyed on (row, tick, seed) fills or perturbs a column.
// No global RNG state, so the values are reproducible across runs/machines/replay and are identical
// whether produced serially or chunked across the Lens worker pool. This is the perturb term of
// HATE-Spec §8.4 (`Score' = Score + Variance * Noise`): determinism is the default, variance is opt-in.

#include <catch2/catch_test_macros.hpp>

#include "TestTags.h"
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "datalens/DataStore.h"
#include "datalens/Lens.h"

#include <vector>

using Catch::Matchers::WithinAbs;

namespace
{
    // A store with a "Score" accumulator column (0) and a "Variance" column (1), n live rows.
    DataStore MakeNoiseStore(size_t n, float scoreInit, float varianceInit)
    {
        std::vector<DataStoreColumnSchema> cols = {
            {Tag("Score"),    DataLensValueType::Float},
            {Tag("Variance"), DataLensValueType::Float},
        };
        DataStore s(cols, n);
        for (size_t r = 0; r < n; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<float>(row, 0, scoreInit);
            s.SetRaw<float>(row, 1, varianceInit);
        }
        return s;
    }

    constexpr size_t ScoreCol = 0;
    constexpr size_t VarCol   = 1;
}

TEST_CASE("noise: fill stays within [lo,hi] and only touches live rows", "[noise]")
{
    DataStore s = MakeNoiseStore(8, -1.0f, 0.0f);
    s.FreeRow(3); // a hole

    const size_t n = s.RunNoiseColumn<float>(ScoreCol, DataSystemOp::Set, 0.25f, 0.75f,
                                             /*seed*/ 1234, /*tick*/ 0, false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE(n == 7); // 8 rows minus the freed one

    for (size_t r = 0; r < 8; ++r)
    {
        const float v = s.GetRaw<float>(r, ScoreCol);
        if (r == 3)
            REQUIRE_THAT(v, WithinAbs(-1.0f, 1e-6f)); // freed row untouched
        else
        {
            REQUIRE(v >= 0.25f);
            REQUIRE(v < 0.75f);
        }
    }
}

TEST_CASE("noise: same (seed,tick) is reproducible; tick and seed change the stream", "[noise]")
{
    DataStore a = MakeNoiseStore(64, 0.0f, 0.0f);
    DataStore b = MakeNoiseStore(64, 0.0f, 0.0f);

    a.RunNoiseColumn<float>(ScoreCol, DataSystemOp::Set, 0.0f, 1.0f, 99, 7, false, 0, DataCompareOp::Always, 0.0f);
    b.RunNoiseColumn<float>(ScoreCol, DataSystemOp::Set, 0.0f, 1.0f, 99, 7, false, 0, DataCompareOp::Always, 0.0f);
    for (size_t r = 0; r < 64; ++r) // identical key -> identical values
        REQUIRE_THAT(a.GetRaw<float>(r, ScoreCol), WithinAbs(b.GetRaw<float>(r, ScoreCol), 0.0f));

    // A different tick must produce a different stream (at least one row differs).
    DataStore c = MakeNoiseStore(64, 0.0f, 0.0f);
    c.RunNoiseColumn<float>(ScoreCol, DataSystemOp::Set, 0.0f, 1.0f, 99, 8, false, 0, DataCompareOp::Always, 0.0f);
    bool tickDiffers = false;
    for (size_t r = 0; r < 64 && !tickDiffers; ++r)
        tickDiffers = a.GetRaw<float>(r, ScoreCol) != c.GetRaw<float>(r, ScoreCol);
    REQUIRE(tickDiffers);

    // A different seed must produce a different stream too.
    DataStore d = MakeNoiseStore(64, 0.0f, 0.0f);
    d.RunNoiseColumn<float>(ScoreCol, DataSystemOp::Set, 0.0f, 1.0f, 100, 7, false, 0, DataCompareOp::Always, 0.0f);
    bool seedDiffers = false;
    for (size_t r = 0; r < 64 && !seedDiffers; ++r)
        seedDiffers = a.GetRaw<float>(r, ScoreCol) != d.GetRaw<float>(r, ScoreCol);
    REQUIRE(seedDiffers);
}

TEST_CASE("noise: parallel (Lens) result is byte-identical to serial", "[noise]")
{
    constexpr size_t N = 5000;
    DataStore serial   = MakeNoiseStore(N, 0.0f, 0.0f);
    DataStore parallel = MakeNoiseStore(N, 0.0f, 0.0f);
    serial.FreeRow(17); parallel.FreeRow(17); // identical holes

    serial.RunNoiseColumn<float>(ScoreCol, DataSystemOp::Set, -0.5f, 0.5f, 555, 3, false, 0, DataCompareOp::Always, 0.0f);

    datalens::Lens lens(8);
    lens.RunSystemNoiseColumn<float>(parallel, ScoreCol, DataSystemOp::Set, -0.5f, 0.5f, 555, 3,
                                     false, 0, DataCompareOp::Always, 0.0f);

    for (size_t r = 0; r < N; ++r) // exact equality: the PRNG is keyed on the global row index
        REQUIRE(serial.GetRaw<float>(r, ScoreCol) == parallel.GetRaw<float>(r, ScoreCol));
}

TEST_CASE("noise: perturb Score += Variance * Noise leaves zero-variance rows unchanged", "[noise]")
{
    DataStore s = MakeNoiseStore(4, 10.0f, 0.0f);
    s.SetRaw<float>(0, VarCol, 0.0f); // perfect play: no jitter
    s.SetRaw<float>(1, VarCol, 2.0f);
    s.SetRaw<float>(2, VarCol, 0.0f);
    s.SetRaw<float>(3, VarCol, 5.0f);

    const size_t n = s.RunNoisePerturbColumn<float>(ScoreCol, DataSystemOp::Add, VarCol, 0.0f, 1.0f,
                                                    /*seed*/ 42, /*tick*/ 1, false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE(n == 4);

    // Variance 0 -> Score unchanged exactly.
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(10.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(10.0f, 1e-6f));

    // Variance > 0 -> Score shifted by Variance * noise, and noise in [0,1) so the shift is in [0, Variance).
    const float s1 = s.GetRaw<float>(1, ScoreCol);
    const float s3 = s.GetRaw<float>(3, ScoreCol);
    REQUIRE(s1 >= 10.0f);
    REQUIRE(s1 <  12.0f); // 10 + [0,2)
    REQUIRE(s3 >= 10.0f);
    REQUIRE(s3 <  15.0f); // 10 + [0,5)

    // Cross-check row 1 against the documented formula directly.
    const float expected = 10.0f + 2.0f * DataStore::Noise01(1, 1, 42);
    REQUIRE_THAT(s1, WithinAbs(expected, 1e-5f));
}

TEST_CASE("noise: a predicate gates which rows are perturbed", "[noise]")
{
    DataStore s = MakeNoiseStore(4, 0.0f, 1.0f);
    s.SetRaw<float>(0, VarCol, 0.0f); // predicate Variance > 0.5 selects rows 1..3
    s.SetRaw<float>(1, VarCol, 1.0f);
    s.SetRaw<float>(2, VarCol, 1.0f);
    s.SetRaw<float>(3, VarCol, 1.0f);

    const size_t n = s.RunNoiseColumn<float>(ScoreCol, DataSystemOp::Set, 1.0f, 2.0f, 7, 0,
                                             /*hasPredicate*/ true, VarCol, DataCompareOp::Greater, 0.5f);
    REQUIRE(n == 3);

    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f)); // not selected
    for (size_t r = 1; r < 4; ++r)
    {
        const float v = s.GetRaw<float>(r, ScoreCol);
        REQUIRE(v >= 1.0f);
        REQUIRE(v <  2.0f);
    }
}

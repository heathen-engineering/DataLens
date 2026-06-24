// Response-curve transform (A3.11): a System's per-row operand (a metric column) is passed through a
// pass-level response curve (normalise -> curve -> [0,1], optional invert) before the combine. This is
// the HATE-Spec §8 considerations primitive. Curves are uniform per pass and transcendental-free.

#include <catch2/catch_test_macros.hpp>

#include "TestTags.h"
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "datalens/DataStore.h"
#include "datalens/Lens.h"

#include <vector>

using Catch::Matchers::WithinAbs;

namespace
{
    // A store with a "Metric" float column (the input) and a "Score" float column (the accumulator).
    // n live rows; Metric[r] given by gen(r), Score[r] preset to scoreInit.
    DataStore MakeScoreStore(size_t n, float scoreInit, float (*gen)(size_t))
    {
        std::vector<DataStoreColumnSchema> cols = {
            {Tag("Metric"), DataLensValueType::Float},
            {Tag("Score"),  DataLensValueType::Float},
        };
        DataStore s(cols, n);
        for (size_t r = 0; r < n; ++r)
        {
            size_t row = s.AllocRow();
            s.SetRaw<float>(row, 0, gen(r));
            s.SetRaw<float>(row, 1, scoreInit);
        }
        return s;
    }

    constexpr size_t MetricCol = 0;
    constexpr size_t ScoreCol  = 1;
}

TEST_CASE("curve: identity (default Linear y=x) normalises the operand", "[curve]")
{
    // Metric in {0, 50, 100}; normalise over [0,100] -> {0.0, 0.5, 1.0}; Set Score = curve(Metric).
    auto s = MakeScoreStore(3, 0.0f, [](size_t r) { return r * 50.0f; });
    CurveSpec c; c.min = 0.0f; c.max = 100.0f; // Linear p0=1,p1=0 (identity)

    size_t n = s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                                    false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE(n == 3);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: normalise clamps out-of-range inputs to [0,1]", "[curve]")
{
    // Metric in {-20, 50, 250}; normalise over [0,100] -> clamp -> {0, 0.5, 1}.
    auto s = MakeScoreStore(3, 0.0f, [](size_t r) {
        const float v[3] = {-20.0f, 50.0f, 250.0f}; return v[r];
    });
    CurveSpec c; c.min = 0.0f; c.max = 100.0f;
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: degenerate range (min==max) yields x=0", "[curve]")
{
    auto s = MakeScoreStore(2, 9.0f, [](size_t r) { return 42.0f + r; });
    CurveSpec c; c.min = 5.0f; c.max = 5.0f; // denom 0 -> x=0 -> Linear identity -> 0
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("curve: invert flips the response (falling curve over a rising metric)", "[curve]")
{
    // "Execute favours low health": utility = 1 - normalise(Health). Health {0,25,100} over [0,100].
    auto s = MakeScoreStore(3, 0.0f, [](size_t r) {
        const float v[3] = {0.0f, 25.0f, 100.0f}; return v[r];
    });
    CurveSpec c; c.min = 0.0f; c.max = 100.0f; c.invert = true;
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.75f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("curve: Power (integer exponent via repeated multiply)", "[curve]")
{
    // x in {0, 0.5, 1.0}; Power k=2 -> {0, 0.25, 1.0}.
    auto s = MakeScoreStore(3, 0.0f, [](size_t r) { return r * 0.5f; }); // metric 0,0.5,1
    CurveSpec c; c.type = DataCurveType::Power; c.min = 0.0f; c.max = 1.0f; c.p0 = 2.0f;
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.25f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: Power k=0 is the constant-1 curve", "[curve]")
{
    auto s = MakeScoreStore(2, 0.0f, [](size_t r) { return r * 0.5f; });
    CurveSpec c; c.type = DataCurveType::Power; c.p0 = 0.0f;
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: Smoothstep 3x^2 - 2x^3", "[curve]")
{
    auto s = MakeScoreStore(3, 0.0f, [](size_t r) { return r * 0.5f; }); // x 0,0.5,1
    CurveSpec c; c.type = DataCurveType::Smoothstep; c.min = 0.0f; c.max = 1.0f;
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.5f, 1e-6f));  // 3*.25 - 2*.125 = .5
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: Threshold step at p0", "[curve]")
{
    // x 0,0.5,1; threshold p0=0.5 -> {0,1,1} (x >= 0.5).
    auto s = MakeScoreStore(3, 0.0f, [](size_t r) { return r * 0.5f; });
    CurveSpec c; c.type = DataCurveType::Threshold; c.min = 0.0f; c.max = 1.0f; c.p0 = 0.5f;
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: combine into an accumulator (Mul = product aggregation)", "[curve]")
{
    // Score starts at 1.0; multiply in curve(Metric). Metric {0,50,100} over [0,100] identity -> {0,.5,1}.
    auto s = MakeScoreStore(3, 1.0f, [](size_t r) { return r * 50.0f; });
    CurveSpec c; c.min = 0.0f; c.max = 100.0f;
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Mul, MetricCol, c,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: two considerations compose by product (Set then Mul)", "[curve]")
{
    // Consideration A: rising over Metric (health proxy). Consideration B: a second metric.
    // Use the same Metric column twice with different curves to verify the fold is sequential & correct.
    auto s = MakeScoreStore(2, 0.0f, [](size_t r) {
        const float v[2] = {100.0f, 50.0f}; return v[r];
    });
    CurveSpec a; a.min = 0.0f; a.max = 100.0f;                 // normalise -> {1.0, 0.5}
    CurveSpec b; b.min = 0.0f; b.max = 100.0f; b.invert = true; // 1-norm -> {0.0, 0.5}

    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, a,
                                         false, 0, DataCompareOp::Always, 0.0f);
    s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Mul, MetricCol, b,
                                         false, 0, DataCompareOp::Always, 0.0f);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f));   // 1.0 * 0.0
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.25f, 1e-6f));  // 0.5 * 0.5
}

TEST_CASE("curve: predicate gates which rows the consideration touches", "[curve]")
{
    // Only fold the curve where Metric >= 50 (same-type float predicate on the metric column).
    auto s = MakeScoreStore(3, 0.0f, [](size_t r) { return r * 50.0f; }); // 0,50,100
    CurveSpec c; c.min = 0.0f; c.max = 100.0f;
    size_t n = s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, MetricCol, c,
                                                    true, MetricCol, DataCompareOp::GreaterEqual, 50.0f);
    REQUIRE(n == 2);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(0.0f, 1e-6f)); // gated out: untouched (init 0)
    REQUIRE_THAT(s.GetRaw<float>(1, ScoreCol), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(s.GetRaw<float>(2, ScoreCol), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("curve: invalid columns are a safe no-op", "[curve]")
{
    auto s = MakeScoreStore(2, 7.0f, [](size_t r) { return 1.0f; });
    CurveSpec c;
    REQUIRE(s.RunColumnSystemCurvedColumn<float>(99, DataSystemOp::Set, MetricCol, c,
                                                 false, 0, DataCompareOp::Always, 0.0f) == 0);
    REQUIRE(s.RunColumnSystemCurvedColumn<float>(ScoreCol, DataSystemOp::Set, 99, c,
                                                 false, 0, DataCompareOp::Always, 0.0f) == 0);
    REQUIRE_THAT(s.GetRaw<float>(0, ScoreCol), WithinAbs(7.0f, 1e-6f)); // unchanged
}

TEST_CASE("curve: Lens parallel result matches single-threaded", "[curve]")
{
    const size_t rows = 100000;
    auto gen = [](size_t r) { return static_cast<float>(r % 137); };

    DataStore serial = MakeScoreStore(rows, 1.0f, +gen);
    DataStore parallel = MakeScoreStore(rows, 1.0f, +gen);

    CurveSpec c; c.type = DataCurveType::Power; c.min = 0.0f; c.max = 136.0f; c.p0 = 3.0f; c.invert = true;

    size_t serialAffected = serial.RunColumnSystemCurvedColumn<float>(
        ScoreCol, DataSystemOp::Mul, MetricCol, c, true, MetricCol, DataCompareOp::Greater, 10.0f);

    datalens::Lens lens(8);
    size_t parAffected = lens.RunSystemCurvedColumn<float>(
        parallel, ScoreCol, DataSystemOp::Mul, MetricCol, c, true, MetricCol, DataCompareOp::Greater, 10.0f);

    REQUIRE(parAffected == serialAffected);
    for (size_t r = 0; r < rows; ++r)
        REQUIRE_THAT(parallel.GetRaw<float>(r, ScoreCol),
                     WithinAbs(serial.GetRaw<float>(r, ScoreCol), 1e-6f));
}

// Query/Update IR (A4): a flat, pointer-free, serialisable op set the Lens ingests.

#include <catch2/catch_test_macros.hpp>

#include "datalens/DataStore.h"
#include "datalens/Ir.h"
#include "datalens/Lens.h"

#include <cstdint>
#include <vector>

using datalens::IrProgram;
using datalens::IrSystemOp;

namespace
{
    // One Int32 column store with `rows` live rows, V[r] = r.
    DataStore MakeStore(size_t rows)
    {
        std::vector<DataStoreColumnSchema> cols = {{"V", DataLensValueType::Int32}};
        DataStore s(cols, rows);
        for (size_t r = 0; r < rows; ++r)
            s.SetRaw<int32_t>(s.AllocRow(), 0, static_cast<int32_t>(r));
        return s;
    }
}

TEST_CASE("ir: execute matches direct Systems", "[ir]")
{
    DataStore s = MakeStore(1000);

    IrProgram prog;
    prog.Add(IrSystemOp::Scalar(0, DataLensValueType::Int32, 0, DataSystemOp::Add, 10));
    prog.Add(IrSystemOp::Scalar(0, DataLensValueType::Int32, 0, DataSystemOp::Mul, 2));

    DataStore* stores[] = {&s};
    datalens::Lens lens(4);
    size_t affected = lens.Execute(prog, stores, 1);

    REQUIRE(affected == 2000); // both ops touch all 1000 live rows
    // Same column -> ordered: (r + 10) * 2
    for (size_t r = 0; r < 1000; ++r)
        REQUIRE(s.GetRaw<int32_t>(r, 0) == static_cast<int32_t>((r + 10) * 2));
}

TEST_CASE("ir: cross-column + predicate + lod ops round through the IR", "[ir]")
{
    std::vector<DataStoreColumnSchema> cols = {{"Current", DataLensValueType::Int32},
                                               {"Max",     DataLensValueType::Int32}};
    DataStore s(cols, 3);
    size_t r0 = s.AllocRow(); s.SetRaw<int32_t>(r0, 0, 150); s.SetRaw<int32_t>(r0, 1, 100); s.SetLod(r0, 0);
    size_t r1 = s.AllocRow(); s.SetRaw<int32_t>(r1, 0, 40);  s.SetRaw<int32_t>(r1, 1, 100); s.SetLod(r1, 0);
    size_t r2 = s.AllocRow(); s.SetRaw<int32_t>(r2, 0, 999); s.SetRaw<int32_t>(r2, 1, 100); s.SetLod(r2, 2);

    IrProgram prog;
    // Current = min(Current, Max), but only LOD band [0,0] -> r2 (LOD 2) untouched.
    prog.Add(IrSystemOp::Column(0, DataLensValueType::Int32, 0, DataSystemOp::Min, 1).WithLodBand(0, 0));

    DataStore* stores[] = {&s};
    datalens::Lens lens(2);
    size_t affected = lens.Execute(prog, stores, 1);
    REQUIRE(affected == 2);
    REQUIRE(s.GetRaw<int32_t>(r0, 0) == 100);
    REQUIRE(s.GetRaw<int32_t>(r1, 0) == 40);
    REQUIRE(s.GetRaw<int32_t>(r2, 0) == 999); // outside band
}

TEST_CASE("ir: serialize/deserialize round-trips and executes identically", "[ir]")
{
    IrProgram prog;
    prog.Add(IrSystemOp::Scalar(0, DataLensValueType::Int32, 0, DataSystemOp::Add, 5));
    prog.Add(IrSystemOp::Column(0, DataLensValueType::Int32, 0, DataSystemOp::Max, 0)
                 .WithPredicate(0, DataCompareOp::Greater, 3));

    std::vector<uint8_t> bytes = prog.Serialize();

    IrProgram restored;
    REQUIRE(IrProgram::Deserialize(bytes.data(), bytes.size(), restored));
    REQUIRE(restored.Count() == prog.Count());

    // Execute both against identical stores; results must match.
    DataStore a = MakeStore(500);
    DataStore b = MakeStore(500);
    DataStore* sa[] = {&a};
    DataStore* sb[] = {&b};
    datalens::Lens lens(4);
    lens.Execute(prog, sa, 1);
    lens.Execute(restored, sb, 1);

    for (size_t r = 0; r < 500; ++r)
        REQUIRE(a.GetRaw<int32_t>(r, 0) == b.GetRaw<int32_t>(r, 0));
}

TEST_CASE("ir: empty program serialises and runs as a no-op", "[ir]")
{
    IrProgram prog;
    std::vector<uint8_t> bytes = prog.Serialize();
    IrProgram restored;
    REQUIRE(IrProgram::Deserialize(bytes.data(), bytes.size(), restored));
    REQUIRE(restored.Count() == 0);

    datalens::Lens lens(2);
    DataStore s = MakeStore(4);
    REQUIRE(lens.Execute(restored, nullptr, 0) == 0);
}

TEST_CASE("ir: deserialize rejects bad magic / truncation / trailing garbage", "[ir]")
{
    IrProgram prog;
    prog.Add(IrSystemOp::Scalar(0, DataLensValueType::Int32, 0, DataSystemOp::Add, 1));
    std::vector<uint8_t> bytes = prog.Serialize();

    IrProgram out;
    REQUIRE_FALSE(IrProgram::Deserialize(nullptr, 0, out));
    REQUIRE_FALSE(IrProgram::Deserialize(bytes.data(), 3, out)); // smaller than header

    auto corrupt = bytes;
    corrupt[0] ^= 0xFF; // break the magic
    REQUIRE_FALSE(IrProgram::Deserialize(corrupt.data(), corrupt.size(), out));

    auto trailing = bytes;
    trailing.push_back(0); // size no longer matches header.opCount
    REQUIRE_FALSE(IrProgram::Deserialize(trailing.data(), trailing.size(), out));
}

TEST_CASE("ir: out-of-range store index is a safe no-op", "[ir]")
{
    DataStore s = MakeStore(10);
    IrProgram prog;
    prog.Add(IrSystemOp::Scalar(5, DataLensValueType::Int32, 0, DataSystemOp::Set, 1)); // storeIndex 5

    DataStore* stores[] = {&s};
    datalens::Lens lens(2);
    REQUIRE(lens.Execute(prog, stores, 1) == 0); // index 5 >= 1 -> skipped
    for (size_t r = 0; r < 10; ++r)
        REQUIRE(s.GetRaw<int32_t>(r, 0) == static_cast<int32_t>(r)); // untouched
}

TEST_CASE("ir: a program spanning two stores executes against the table", "[ir]")
{
    DataStore s0 = MakeStore(3);
    DataStore s1 = MakeStore(3);

    IrProgram prog;
    prog.Add(IrSystemOp::Scalar(0, DataLensValueType::Int32, 0, DataSystemOp::Add, 100));
    prog.Add(IrSystemOp::Scalar(1, DataLensValueType::Int32, 0, DataSystemOp::Mul, 10));

    DataStore* stores[] = {&s0, &s1};
    datalens::Lens lens(4);
    REQUIRE(lens.Execute(prog, stores, 2) == 6);

    REQUIRE(s0.GetRaw<int32_t>(1, 0) == 101); // 1 + 100
    REQUIRE(s1.GetRaw<int32_t>(1, 0) == 10);  // 1 * 10
}

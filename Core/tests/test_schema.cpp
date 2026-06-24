// Schema correctness: the synthetic RowFlags column, lookups, validation, stride.

#include <catch2/catch_test_macros.hpp>

#include "datalens/DataLensSchema.h"
#include "TestTags.h"

#include <vector>

namespace
{
    std::vector<DataStoreColumnSchema> ThreeCols()
    {
        return {
            {Tag("ColFloat"),  DataLensValueType::Float},
            {Tag("ColInt32"),  DataLensValueType::Int32},
            {Tag("ColDouble"), DataLensValueType::Double},
        };
    }
}

TEST_CASE("schema auto-inserts the RowFlags column at index 0", "[schema]")
{
    DataStoreSchema s(Tag("Actors"), ThreeCols(), 10);

    REQUIRE(s.Columns.size() == 4); // RowFlags + the three declared columns
    REQUIRE(s.Columns[0].Tag == DataLensRowFlagsTag);
    REQUIRE(s.Columns[0].GetStride() == 1);
}

TEST_CASE("FindColumn accounts for the RowFlags column", "[schema]")
{
    DataStoreSchema s(Tag("Actors"), ThreeCols(), 10);
    REQUIRE(s.FindColumn(Tag("ColFloat"))  == 1);
    REQUIRE(s.FindColumn(Tag("ColInt32"))  == 2);
    REQUIRE(s.FindColumn(Tag("ColDouble")) == 3);
}

TEST_CASE("schema validates and reports stride", "[schema]")
{
    DataStoreSchema s(Tag("Actors"), ThreeCols(), 10);
    REQUIRE(s.Validate());
    REQUIRE(s.GetStride() == 17); // 1 (RowFlags) + 4 + 4 + 8
}

TEST_CASE("range-narrowing picks the smallest byte-aligned type (A2)", "[schema]")
{
    using namespace DataLensValueTypeUtils;
    REQUIRE(SmallestUnsignedForMax(100)        == DataLensValueType::UInt8);
    REQUIRE(SmallestUnsignedForMax(255)        == DataLensValueType::UInt8);
    REQUIRE(SmallestUnsignedForMax(256)        == DataLensValueType::UInt16);
    REQUIRE(SmallestUnsignedForMax(65535)      == DataLensValueType::UInt16);
    REQUIRE(SmallestUnsignedForMax(65536)      == DataLensValueType::UInt32);
    REQUIRE(SmallestUnsignedForMax(5000000000) == DataLensValueType::UInt64);

    REQUIRE(SmallestSignedForRange(-100, 100)        == DataLensValueType::Int8);
    REQUIRE(SmallestSignedForRange(-200, 100)        == DataLensValueType::Int16);
    REQUIRE(SmallestSignedForRange(0, 40000)         == DataLensValueType::Int32); // 40000 > Int16 max 32767
    REQUIRE(SmallestSignedForRange(-100000, 100000)  == DataLensValueType::Int32);
    REQUIRE(SmallestSignedForRange(-5000000000, 0)   == DataLensValueType::Int64);
}

TEST_CASE("DataLensSchema stores and columns are addressable by tag", "[schema]")
{
    DataLensSchema lens;
    lens.AddStore(DataStoreSchema(Tag("Actors"), ThreeCols(), 10));

    REQUIRE(lens.Count() == 1);
    REQUIRE(lens.HasStore(Tag("Actors")));
    REQUIRE(lens.FindStore(Tag("Actors")) != SIZE_MAX);
    REQUIRE(lens.FindStore(Tag("DoesNotExist")) == SIZE_MAX);

    // Column ids are globally unique -> resolve to (store, column).
    size_t store = SIZE_MAX, col = SIZE_MAX;
    REQUIRE(lens.ResolveColumn(Tag("ColInt32"), store, col));
    REQUIRE(store == 0);
    REQUIRE(col == 2);
    REQUIRE_FALSE(lens.ResolveColumn(Tag("Nope"), store, col));
}

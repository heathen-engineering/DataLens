// Exercises the C ABI the Unity binding will P/Invoke, so the boundary contract is
// locked natively before crossing into managed code.

#include <catch2/catch_test_macros.hpp>

#include "datalens/c_api.h"

#include <cstdint>

namespace
{
    // Value type codes (mirror DataLensValueType): Int32=5, Float=9, Double=10.
    constexpr int32_t kInt32 = 5, kFloat = 9, kDouble = 10;
}

TEST_CASE("c_api: abi version", "[c_api]")
{
    REQUIRE(dl_abi_version() == 1);
}

TEST_CASE("c_api: create / shape / typed round-trip / validity", "[c_api]")
{
    const char* names[] = {"Health", "Team", "Stamina"};
    const int32_t types[] = {kFloat, kInt32, kDouble};

    dl_store* s = dl_store_create(names, types, 3, 8);
    REQUIRE(s != nullptr);
    REQUIRE(dl_store_row_count(s) == 8);
    REQUIRE(dl_store_column_count(s) == 3);
    REQUIRE(dl_store_row_stride(s) == 16); // 4 + 4 + 8

    REQUIRE(dl_store_set_f32(s, 2, 0, 12.5f) == 1);
    REQUIRE(dl_store_set_i32(s, 2, 1, 7) == 1);
    REQUIRE(dl_store_set_f64(s, 2, 2, 3.25) == 1);

    float f = 0; int32_t i = 0; double d = 0;
    REQUIRE(dl_store_get_f32(s, 2, 0, &f) == 1);
    REQUIRE(dl_store_get_i32(s, 2, 1, &i) == 1);
    REQUIRE(dl_store_get_f64(s, 2, 2, &d) == 1);
    REQUIRE(f == 12.5f);
    REQUIRE(i == 7);
    REQUIRE(d == 3.25);

    // Out-of-range rejected, not crashing.
    REQUIRE(dl_store_set_f32(s, 99, 0, 1.0f) == 0);
    REQUIRE(dl_store_get_f32(s, 99, 0, &f) == 0);

    // Validity flag.
    REQUIRE(dl_store_is_valid(s, 2) == 0);
    dl_store_set_valid(s, 2, 1);
    REQUIRE(dl_store_is_valid(s, 2) == 1);

    dl_store_destroy(s);
}

TEST_CASE("c_api: alloc / free / live count (A2)", "[c_api]")
{
    const char* names[] = {"V"};
    const int32_t types[] = {kFloat};
    dl_store* s = dl_store_create(names, types, 1, 2);
    REQUIRE(s != nullptr);
    REQUIRE(dl_store_live_count(s) == 0);

    uint64_t a = dl_store_alloc_row(s);
    uint64_t b = dl_store_alloc_row(s);
    REQUIRE(a != UINT64_MAX);
    REQUIRE(b != UINT64_MAX);
    REQUIRE(dl_store_live_count(s) == 2);
    REQUIRE(dl_store_alloc_row(s) == UINT64_MAX); // capacity 2: full

    dl_store_free_row(s, a);
    REQUIRE(dl_store_live_count(s) == 1);
    REQUIRE(dl_store_alloc_row(s) == a); // reused
    REQUIRE(dl_store_alloc_row(nullptr) == UINT64_MAX);

    dl_store_destroy(s);
}

TEST_CASE("c_api: run system (A3)", "[c_api]")
{
    const char* names[] = {"HP"};
    const int32_t types[] = {kInt32};
    dl_store* s = dl_store_create(names, types, 1, 4);
    REQUIRE(s != nullptr);

    uint64_t r0 = dl_store_alloc_row(s);
    uint64_t r1 = dl_store_alloc_row(s);
    dl_store_set_i32(s, r0, 0, 100);
    dl_store_set_i32(s, r1, 0, 30);

    // HP += 100 where HP < 50  (op=1 Add, cmp=3 Less)
    uint64_t n = dl_store_run_i32(s, 0, 1, 100, 1, 0, 3, 50);
    REQUIRE(n == 1);

    int32_t v = 0;
    dl_store_get_i32(s, r1, 0, &v);
    REQUIRE(v == 130);
    dl_store_get_i32(s, r0, 0, &v);
    REQUIRE(v == 100);

    dl_store_destroy(s);
}

TEST_CASE("c_api: null/invalid args are safe", "[c_api]")
{
    REQUIRE(dl_store_create(nullptr, nullptr, 0, 0) == nullptr);
    REQUIRE(dl_store_row_count(nullptr) == 0);
    float f = 0;
    REQUIRE(dl_store_get_f32(nullptr, 0, 0, &f) == 0);
    dl_store_destroy(nullptr); // no-op, no crash
}

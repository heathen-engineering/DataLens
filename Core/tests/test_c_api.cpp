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

TEST_CASE("c_api: run cross-column system (A3.3)", "[c_api]")
{
    const char* names[] = {"Current", "Max"};
    const int32_t types[] = {kInt32, kInt32};
    dl_store* s = dl_store_create(names, types, 2, 4);
    REQUIRE(s != nullptr);

    uint64_t r0 = dl_store_alloc_row(s);
    uint64_t r1 = dl_store_alloc_row(s);
    dl_store_set_i32(s, r0, 0, 150); dl_store_set_i32(s, r0, 1, 100);
    dl_store_set_i32(s, r1, 0, 40);  dl_store_set_i32(s, r1, 1, 100);

    // Current = min(Current, Max)  (op=4 Min, operandCol=1, no predicate)
    uint64_t n = dl_store_run_col_i32(s, 0, 4, 1, 0, 0, 0, 0);
    REQUIRE(n == 2);

    int32_t v = 0;
    dl_store_get_i32(s, r0, 0, &v); REQUIRE(v == 100); // clamped
    dl_store_get_i32(s, r1, 0, &v); REQUIRE(v == 40);  // already under

    // Parallel form via the Lens: Current += Max.
    dl_lens* lens = dl_lens_create(4);
    REQUIRE(lens != nullptr);
    uint64_t pn = dl_lens_run_col_i32(lens, s, 0, 1, 1, 0, 0, 0, 0); // op=1 Add
    REQUIRE(pn == 2);
    dl_store_get_i32(s, r0, 0, &v); REQUIRE(v == 200); // 100 + 100
    dl_store_get_i32(s, r1, 0, &v); REQUIRE(v == 140); // 40 + 100

    dl_lens_destroy(lens);
    dl_store_destroy(s);
}

TEST_CASE("c_api: run batched Systems via the Lens (A3.4)", "[c_api]")
{
    const char* names[] = {"A", "B"};
    const int32_t types[] = {kInt32, kInt32};
    dl_store* s = dl_store_create(names, types, 2, 4);
    REQUIRE(s != nullptr);

    uint64_t r0 = dl_store_alloc_row(s);
    dl_store_set_i32(s, r0, 0, 5);
    dl_store_set_i32(s, r0, 1, 5);

    // Two independent Systems (different columns): A += 10, B *= 3.
    dl_system_desc descs[2] = {};
    descs[0].store = s; descs[0].elem_type = kInt32; descs[0].target_col = 0;
    descs[0].op = 1; /* Add */ descs[0].operand = 10.0;
    descs[1].store = s; descs[1].elem_type = kInt32; descs[1].target_col = 1;
    descs[1].op = 3; /* Mul */ descs[1].operand = 3.0;

    dl_lens* lens = dl_lens_create(4);
    REQUIRE(lens != nullptr);
    uint64_t affected = dl_lens_run_batch(lens, descs, 2);
    REQUIRE(affected == 2);

    int32_t v = 0;
    dl_store_get_i32(s, r0, 0, &v); REQUIRE(v == 15);
    dl_store_get_i32(s, r0, 1, &v); REQUIRE(v == 15);

    // Empty/odd args are safe.
    REQUIRE(dl_lens_run_batch(lens, nullptr, 0) == 0);
    REQUIRE(dl_lens_run_batch(nullptr, descs, 2) == 0);

    dl_lens_destroy(lens);
    dl_store_destroy(s);
}

TEST_CASE("c_api: per-row LOD + batch LOD band (A3.5)", "[c_api]")
{
    const char* names[] = {"HP"};
    const int32_t types[] = {kInt32};
    dl_store* s = dl_store_create(names, types, 1, 4);
    REQUIRE(s != nullptr);

    uint64_t r0 = dl_store_alloc_row(s); // LOD 0
    uint64_t r1 = dl_store_alloc_row(s); // LOD 2
    dl_store_set_i32(s, r0, 0, 100);
    dl_store_set_i32(s, r1, 0, 100);
    REQUIRE(dl_store_get_lod(s, r0) == 0); // default
    dl_store_set_lod(s, r1, 2);
    REQUIRE(dl_store_get_lod(s, r1) == 2);
    dl_store_set_lod(s, r0, 999); // clamps to 255
    REQUIRE(dl_store_get_lod(s, r0) == 255);
    dl_store_set_lod(s, r0, 0);

    // Batch: HP += 10, but only over LOD band [0,1] -> r1 (LOD 2) skipped.
    dl_system_desc d = {};
    d.store = s; d.elem_type = kInt32; d.target_col = 0; d.op = 1; d.operand = 10.0;

    dl_lens* lens = dl_lens_create(4);
    uint64_t n = dl_lens_run_batch_lod(lens, &d, 1, 0, 1);
    REQUIRE(n == 1);

    int32_t v = 0;
    dl_store_get_i32(s, r0, 0, &v); REQUIRE(v == 110);
    dl_store_get_i32(s, r1, 0, &v); REQUIRE(v == 100); // LOD 2 outside band

    dl_lens_destroy(lens);
    dl_store_destroy(s);
}

TEST_CASE("c_api: IR build + execute + serialize round-trip (A4.2)", "[c_api]")
{
    const char* names[] = {"V"};
    const int32_t types[] = {kInt32};
    dl_store* s = dl_store_create(names, types, 1, 4);
    for (int i = 0; i < 4; ++i) { uint64_t r = dl_store_alloc_row(s); dl_store_set_i32(s, r, 0, i); }

    dl_ir_program* prog = dl_ir_create();
    dl_ir_op op = {};
    op.store_index = 0; op.elem_type = kInt32; op.target_col = 0; op.op = 1 /*Add*/; op.operand = 10.0;
    op.max_lod = 255;
    dl_ir_add_system(prog, &op);
    REQUIRE(dl_ir_count(prog) == 1);

    // Serialize, then deserialize into a fresh program.
    uint64_t size = dl_ir_serialize(prog, nullptr, 0);
    REQUIRE(size > 0);
    std::vector<uint8_t> buf(size);
    REQUIRE(dl_ir_serialize(prog, buf.data(), size) == size);
    dl_ir_program* restored = dl_ir_deserialize(buf.data(), size);
    REQUIRE(restored != nullptr);
    REQUIRE(dl_ir_count(restored) == 1);
    REQUIRE(dl_ir_deserialize(buf.data(), 2) == nullptr); // truncated

    dl_lens* lens = dl_lens_create(2);
    dl_store* stores[] = {s};
    REQUIRE(dl_lens_execute(lens, restored, stores, 1) == 4);
    int32_t v = 0; dl_store_get_i32(s, 2, 0, &v); REQUIRE(v == 12); // 2 + 10

    dl_ir_destroy(prog);
    dl_ir_destroy(restored);
    dl_lens_destroy(lens);
    dl_store_destroy(s);
}

TEST_CASE("c_api: tick loop runs scheduled program then refreshes scheduled view (A4.2)", "[c_api]")
{
    const char* names[] = {"V"};
    const int32_t types[] = {kInt32};
    dl_store* s = dl_store_create(names, types, 1, 3);
    for (int i = 0; i < 3; ++i) dl_store_alloc_row(s); // all V = 0

    dl_ir_program* prog = dl_ir_create();
    dl_ir_op op = {};
    op.store_index = 0; op.elem_type = kInt32; op.target_col = 0; op.op = 1 /*Add*/; op.operand = 1.0;
    op.max_lod = 255;
    dl_ir_add_system(prog, &op);

    uint64_t cols[] = {0};
    dl_view* view = dl_view_create(cols, 1);

    dl_lens* lens = dl_lens_create(2);
    dl_lens_add_scheduled_program(lens, prog, 1, 0, 255, 0); // +1 every tick
    dl_lens_add_scheduled_view(lens, view, 0, 1, 0);         // refresh view every tick
    REQUIRE(dl_lens_scheduled_program_count(lens) == 1);
    REQUIRE(dl_lens_scheduled_view_count(lens) == 1);

    dl_store* stores[] = {s};
    dl_lens_tick(lens, stores, 1);
    dl_lens_tick(lens, stores, 1);
    REQUIRE(dl_lens_current_tick(lens) == 2);

    // View reflects post-tick state (Systems run before view refresh).
    REQUIRE(dl_view_row_count(view) == 3);
    int32_t v = 0;
    REQUIRE(dl_view_get_i32(view, 0, 0, &v) == 1);
    REQUIRE(v == 2);
    REQUIRE(dl_view_source_row(view, 0) == 0);

    dl_view_destroy(view);
    dl_ir_destroy(prog);
    dl_lens_destroy(lens);
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

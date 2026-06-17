# DataLens Core

The engine-agnostic, C++17 core of DataLens: a cache-aware, column-oriented in-memory
simulation store. Linux-first; no engine dependencies. This is the single source of truth that
the per-engine Foundations (Unity, O3DE, Unreal, Godot) bind to.

> Status: **A1 (extraction) — byte-stride Phase-0 baseline.** Extracted from the original UE
> prototype, de-coupled from Unreal, and put under CMake + Catch2 + CI. The bit-packed store,
> Systems, the Lens orchestrator, the IR builder, deltas, and the C ABI are later phases (A2-A7).
> See `../../SourceRepo/Unity/ToolkitSource/Assets/Toolkits/DesignSpecs/` for the specs and plan.

## Layout
```
Core/
  include/datalens/   public headers (DataStore, DataLensSchema, DataLens, log)
  src/                implementation
  tests/              Catch2 unit + correctness tests (ported from the UE Phase-0 harness)
  bench/              Phase-0 benchmark + committed baseline CSV
```

## Build (Linux)
```sh
cmake -S Core -B Core/build -DCMAKE_BUILD_TYPE=Release
cmake --build Core/build -j
ctest --test-dir Core/build --output-on-failure
```
Produces `libdatalens.a` (static) and `libdatalens.so` (shared). The shared library is what the
Unity Foundation will load via P/Invoke once the C ABI lands (A7).

### Options
| Option | Default | Meaning |
|--------|---------|---------|
| `DATALENS_BUILD_TESTS` | ON | Build the Catch2 suite (fetches Catch2 v3 via FetchContent; needs network on first configure) |
| `DATALENS_BUILD_BENCH` | ON | Build the Phase-0 benchmark |
| `DATALENS_SANITIZERS`  | OFF | Build with ASan + UBSan |

### Benchmark
```sh
./Core/build/bench/datalens_bench            # CSV to stdout
./Core/build/bench/datalens_bench out.csv    # CSV to file
```
`bench/baseline_phase0.csv` is a committed reference run (per-machine; informational, not a gate).

## Logging
The core has no engine logging. By default messages go to `stderr`. A host routes them with:
```cpp
datalens::SetLogCallback([](datalens::LogLevel lvl, const char* msg){ /* host logger */ });
```

## Notes from the extraction (A1)
- De-coupled from Unreal: UE logging replaced by `datalens/log.h`; no UE symbols remain.
- Fixed two latent issues the UE unity build had masked: missing standard includes
  (`<stdexcept>`, `<algorithm>`, `<array>`) and `DataLensValueTypeUtils` helpers that were
  declared `static` (internal linkage) but defined in one translation unit.
- **Known issue:** `DataLens::Serialize`/`Deserialize` round-trip throws "Invalid payload"
  (unverified Phase-0 logic, never covered by the original tests). Tracked for A6
  (delta/serialise). The test for it is marked `[!mayfail]`.

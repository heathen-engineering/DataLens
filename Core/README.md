# DataLens Core

The engine-agnostic, C++17 core of DataLens: a cache-aware, column-oriented in-memory
simulation store. Linux-first; no engine dependencies. This is the single source of truth that
the per-engine Foundations (Unity, O3DE, Unreal, Godot) bind to.

> Status (2026-06-18): **the compute substrate is built and proven.** Byte-aligned columnar store
> (A2), the Systems framework + parallel `Lens` scheduler with Simulation LOD and the
> bitwise/curve/noise/argmax kernels (A3.1–A3.13), the update IR (A4.1/A4.2), and the tick scheduler +
> read-only `DataView` (A5.1–A5.3) are all done, behind a stable C ABI (76 `dl_` exports). 114 Catch2
> cases pass clean under ASan+UBSan AND ThreadSanitizer (1 expected xfail — the A6 serialize round-trip,
> below). Still open: A6 (delta + whole-world serialise + schema versioning) and the A4 read-side /
> A5 caches. See `../../SourceRepo/Unity/ToolkitSource/Assets/Toolkits/DesignSpecs/` for the specs, the
> master plan, and `Performance-Log.md` for measured numbers.

## Layout
```
Core/
  include/datalens/   public headers (DataStore, Lens, DataView, Ir, ThreadPool, AlignedAllocator,
                      c_api [the C ABI], DataLensSchema, DataLens, log)
  src/                implementation
  cmake/              toolchain files (x86_64-w64-mingw32.cmake — Windows cross-build)
  tests/              Catch2 unit + correctness tests
  bench/              Phase-0 benchmark + committed baseline CSV
```

## Build (Linux)
```sh
cmake -S Core -B Core/build -DCMAKE_BUILD_TYPE=Release
cmake --build Core/build -j
ctest --test-dir Core/build --output-on-failure
```
Produces `libdatalens.a` (static) and `libdatalens.so` (shared). The shared library is what the
Unity Foundation loads via P/Invoke (`[DllImport("datalens")]`); it is vendored at
`Unity-DataLens-Foundation/com.heathen.datalensfoundation/Runtime/Plugins/Linux/x86_64/libdatalens.so`
(use the package's `build-native.sh` to rebuild + re-vendor it).

## Build (Windows Cross-Compilation)
To compile for Windows from Linux, use the provided MinGW-w64 toolchain file. You will need the `mingw-w64` package installed.

```sh
cmake -S Core -B Core/build-win \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-w64-mingw32.cmake \
      -DDATALENS_BUILD_TESTS=OFF \
      -DDATALENS_BUILD_BENCH=OFF
cmake --build Core/build-win -j
```
Produces **`datalens.dll`** (no `lib` prefix, so Unity's `[DllImport("datalens")]` resolves it). The
toolchain statically links libgcc/libstdc++/libwinpthread, so the DLL is **self-contained** — it depends
only on `KERNEL32` + the Windows UCRT (`api-ms-win-crt-*`, present on Win10+), with no mingw runtime DLLs
to ship. Vendored into the Unity Foundation at `…/Runtime/Plugins/Windows/x86_64/datalens.dll`. Verify
self-containment with `x86_64-w64-mingw32-objdump -p Core/build-win/datalens.dll | grep "DLL Name"`.

## Build (Windows Native - MSVC)
For native Windows builds, use CMake with the Visual Studio generator.
```cmd
cmake -S Core -B Core/build-msvc
cmake --build Core/build-msvc --config Release
```
> Note: the MinGW cross-build above is the verified, vendored path. An MSVC `datalens.dll` links the
> MSVC/UCRT runtime; for a self-contained MSVC build set the static runtime
> (`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`). Untested here.

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

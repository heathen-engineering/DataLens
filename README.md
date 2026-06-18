# DataLens

**Engine-agnostic, real-time simulation database for large-scale, emergent actor systems.**
A phase-based R&D project by Heathen Engineering.

DataLens is a cache-aware, column-oriented in-memory store and parallel compute substrate designed
to simulate hundreds of thousands to millions of actors at sub-millisecond cost. It is the
foundation layer that per-engine bindings (Unity first; O3DE, Unreal, Godot to follow) and the
higher simulation layers — **HATE** (the Heathen Attribute / gameplay-Tag Engine) and the
design-stage **Wyrd** mass-emergent-simulation system — are built on.

## Why it exists

Game simulation traditionally stores per-actor state as scattered heap objects and walks them with
branchy, cache-hostile loops. DataLens inverts that: actor state lives in **range-narrowed,
cache-line-aligned columns**, and behaviour is expressed as **data-described Systems** — branchless
column kernels the **Lens** runs in parallel across an owned thread pool. The result is a substrate
where the simulation itself is a tiny fraction of the frame (measured at **~1.2 ms for 1,000,000
agents** running a 6-op integration step), and the rest of the budget is left for the game.

## The stack

```
GameplayTags (interval-encoded hierarchy)        — source of truth for tags
        │ projected into
        ▼
DataLens Core  (native C/C++17, column store + Lens + IR)   ← THIS REPO
        │ stable C ABI
        ▼
DataLens Foundation per engine (Unity first)     — managed binding
        │
        ▼
HATE  (attributes / effects / abilities / utility AI)
        │
        ▼
Wyrd  (mass emergent simulation — design-stage)
        │
        ▼
Toolkits  (visual editors, debuggers, samples)
```

## What's here

This repository is **DataLens Core** — the engine-agnostic C++17 heart of the system.

```
Core/
  include/datalens/   public headers (DataStore, schema, Lens, IR, DataView, C ABI, log)
  src/                implementation
  tests/              Catch2 unit + correctness tests
  bench/              benchmark + committed baseline CSV
Test Results/         recorded benchmark / validation runs (Phase0… Phase3…)
```

Core is **Linux-first** with **no engine dependencies**. It builds to `libdatalens.a` (static) and
`libdatalens.so` (shared); the shared library is what the Unity Foundation loads via P/Invoke over
the stable C ABI. See [`Core/README.md`](Core/README.md) for build instructions and options.

## Capabilities (current)

- **Columnar store** — range-narrowed widths, 64B cache-line-aligned columns, dense
  `Validity`/`Locked` bitmasks with O(1) bit-scan row allocation, per-row Simulation LOD.
- **Systems + Lens** — branchless column kernels (set/add/sub/mul/min/max, bitwise and/or/xor/andnot,
  scaled fused-multiply, response-curve, counter-based noise, argmax) with same-type and mixed-type
  predicates; the Lens schedules data-described Systems into dependency levels and runs each
  row-parallel across its thread pool. Deterministic: parallel results are bit-identical to serial.
- **Update IR** — a flat, pointer-free, serialisable operation set (store-by-index) the core
  ingests; no parser in the core (authoring is per-platform). Programs serialise/deserialise for the
  networking / distributed-topology seam.
- **Tick scheduler + DataView** — the Lens owns a tick counter and runs scheduled programs on a
  cadence (Simulation LOD as execution frequency); read-only `DataView` snapshots let gameplay read
  on any thread while the Lens mutates the store (snapshot isolation).
- **Stable C ABI** — opaque handles + bulk byte-span transfer (76 `dl_*` symbols), the binding seam
  for every engine Foundation.

See the design specs and the master plan under
`SourceRepo/Unity/ToolkitSource/Assets/Toolkits/DesignSpecs/` for the full architecture, decisions,
and roadmap.

## Validation & performance

- **Correctness:** 114 native Catch2 cases, clean under **AddressSanitizer + UBSan AND
  ThreadSanitizer** (one expected serialise xfail tracked for the persistence phase). Parallel paths
  (Lens pool, view refresh, noise, argmax) are TSan-verified race-free and serial-equals-parallel.
- **Performance:** the 1,000,000-agent swarm simulation runs in **~1.2 ms**; a 100,000-actor
  utility-AI decision (two abilities, response-curve scoring + per-actor noise perturb + argmax) runs
  in **~0.95 ms**. In both demos the frame is bound by single-threaded visualisation, not the
  substrate. Recorded runs live in the `Test Results/` folder; methodology and the full
  measurement history are in the design specs' `Performance-Log.md`.

## License & packaging

House model: **`<Name> Foundation`** (open) / **`<Name> Toolkit`** (paid). DataLens Core and the
per-engine Foundations are the open layer.

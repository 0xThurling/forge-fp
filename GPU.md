# SYCL / GPU integration plan

ForgeFP has three execution tiers today: the eager CPU combinators, the
zero-cost in-place kernels, and the opt-in SIMD path (`simd.hpp`). This
document plans the fourth: **GPU kernels via SYCL**, opt-in and with a CPU
fallback so the library keeps its "compiles and runs everywhere" property.

**Status:** Phases 0 and 1 landed — `fp/gpu.hpp` (detection, `usable()`,
USM `Buffer<T>` device + shared, elementwise kernels, `axpy_inplace`,
`softmax_rows`, chunked device reductions) with a CPU fallback, gtest coverage,
a CPU **SYCL stub** that compiles and exercises the SYCL branch without a
toolchain, and both header trees. Phase 1's benchmark is still open; phases
2–4 below.

## Why SYCL

| Option | Verdict |
|---|---|
| **SYCL** | Single-source C++20, vendor-portable (Intel DPC++, AdaptiveCpp for NVIDIA/AMD/Intel, triSYCL CPU), template-friendly, no separate `.cu`/`.hip` compilation step. **Chosen.** |
| CUDA | Best tooling on NVIDIA, but vendor-locked and forces a separate compilation model that does not fit a header-only template library. |
| HIP | Portable across AMD/NVIDIA but still a separate dialect; no clean CPU fallback. |
| OpenMP target offload | Compiler-dependent, weak support for the C++20 range/template style fp uses. |

SYCL's single-source model matters most: a kernel is a lambda in the same
header as the CPU fallback, so the API stays uniform and the fallback is
literally the code next to it.

## Choosing the implementation ("which SYCL?")

Target **SYCL 2020** (`<sycl/sycl.hpp>`, `default_selector_v`-style APIs,
`aspect`, USM). Do not target SYCL 1.2.1 (`<CL/sycl.hpp>`, `cl::sycl`): it is
legacy, lacks the USM/reduction/aspect surface this layer uses, and every
current implementation ships a 2020 header.

| Implementation | Best for | Notes |
|---|---|---|
| **AdaptiveCpp** (formerly hipSYCL) | portability + performance across NVIDIA/AMD/Intel | single-pass compilation, good C++20, generic targets (`--acpp-targets=generic`); some SYCL 2020 corners lag (e.g. parts of the reduction/library surface) |
| **oneAPI DPC++** (`icpx -fsycl`) | the most complete SYCL 2020, best CPU/USM support, Intel GPUs | full USM, `sycl::reduction`, sub-groups, `bfloat16`; NVIDIA/AMD via plugins |
| **SimSYCL** | correctness testing on CPU with *spec-conformant* semantics | header-only simulator with real async/buffer/USM semantics — a much stronger test backend than the sequential stub below; a candidate to replace the stub when vendoring a dependency is acceptable |
| **triSYCL** | reference/spec experiments | incomplete for production kernels |
| **ForgeFP CPU stub** (`test/support`) | zero-dependency CI | sequential host kernels; verifies compilation, API use, buffer lifetimes, and values — not concurrency or performance |

The recommended setup is **AdaptiveCpp or DPC++ for the real build, and the
stub (or SimSYCL) for CI**: `scripts/run_gpu_build.sh` detects whichever real
toolchain is installed and runs `bench/gpu_smoke.cpp`; `scripts/run_gpu_stub_test.sh`
runs the SYCL branch on CPU with no toolchain at all.

### Device selection

The default queue prefers a **GPU, then a CPU**, using a custom selector rather
than `default_selector_v` (which may pick a CPU even when a GPU is present).
Override at compile time:

```bash
# force the CPU backend of DPC++ (useful for testing on a GPU machine)
icpx -std=c++20 -fsycl -DFP_GPU_DEVICE_CPU ...
# prefer an accelerator over a GPU
acpp -std=c++20 --acpp-targets=generic -DFP_GPU_DEVICE_ACCELERATOR ...
```

`fp::gpu::implementation_name`, `fp::gpu::backend_info()`, and
`fp::gpu::usable()` report what was selected at runtime, so a program can log
the backend and choose its own thresholds.

### Better SYCL features to adopt next

The implementation is deliberately conservative (USM + `parallel_for` +
`wait_and_throw`) so it runs on every implementation. The next upgrades, in
order of value:

| Feature | Where it helps |
|---|---|
| `sycl::reduction` / `local_accessor` + `group_barrier` + `reduce_over_group` | replace the chunked two-stage reduction with a hierarchical one |
| `sub_group` primitives (`reduce_over_group(sg, …)`) | fast small reductions and row-wise ops |
| `sycl::vec<T, N>` loads/stores | vectorized elementwise kernels (`map`, `axpy`) |
| `marray` / `sycl::span` | clean row/column views inside kernels |
| `sycl::half` / `bfloat16` | memory-bound kernels (phase 5) |
| `queue::submit` + events | overlap transfers with compute (async API, phase 4) |
| `device.has(aspect::usm_shared_allocations)` | use shared USM automatically for small buffers |

Each is gated by a feature check or an implementation macro so the portable
path remains the default.

## Design constraints (non-negotiable)

1. **Opt-in.** `gpu.hpp` is not in `all.hpp`; including it is a deliberate act
   (the SYCL headers are heavy).
2. **CPU fallback.** Without `<sycl/sycl.hpp>`, every function compiles and
   runs on the CPU. The full test suite runs on machines with no GPU.
3. **Zero cost when unused.** No SYCL include, no runtime, no globals in a
   CPU-only build.
4. **Domain-agnostic.** Numeric kernels only — no ML semantics (the same
   boundary rule as the rest of fp).
5. **Error as value.** `sycl::exception` never escapes; every boundary returns
   `fp::Result`.
6. **Deterministic sequencing.** An in-order queue and synchronous wrappers, so
   the functional API keeps its "same input, same output" reading (reductions
   are the documented exception).

## Architecture

```text
fp/gpu.hpp                     (opt-in umbrella; not in all.hpp)
├── detection                  __has_include(<sycl/sycl.hpp>) -> FP_GPU_SYCL
│                              (or pre-define FP_GPU_SYCL for the stub test)
├── available                  constexpr bool: was SYCL compiled in?
├── implementation_name        compile-time backend name (AdaptiveCpp/DPC++/stub/…)
├── usable()                   does the selected device support USM device allocs?
├── default_device_info()      Result<DeviceInfo> (name, vendor, cpu/gpu, memory)
├── backend_info()             Result<BackendInfo> (implementation, platform, device, driver)
├── device selection           GPU-first custom selector; FP_GPU_DEVICE_CPU /
│                              FP_GPU_DEVICE_ACCELERATOR compile-time overrides
├── Buffer<T>                  USM device block (host vector in fallback)
│     alloc / alloc_shared / from_host / to_host / fill / clone / move-only
│     host_accessible()        true for shared USM (and the fallback)
├── detail::launch_1d          nd_range launch, fixed 256 work-group size
├── map_to(dst, src, f)        dst[i] = f(src[i])
├── transform_inplace(buf, f)  buf[i] = f(buf[i])
├── map(range, f)              host in, host out
├── axpy_inplace(y, a, x)      y[i] += a * x[i]        (optimizer kernel)
├── softmax_rows(buf, r, c)    stable row-wise softmax (attention kernel)
└── reduce(buf, init) / dot(a, b)
      device: one work-item per 4096-element chunk -> partials -> host finalize
      fallback: fp::fold_left / fp::dot
```

### What phase 1 changed over phase 0

| Area | Phase 0 | Phase 1 |
|---|---|---|
| Device suitability | `malloc_device` blindly | `usable()` checks `aspect::usm_device_allocations`; unusable devices take the host path |
| Allocation | `malloc_device` only | `aligned_alloc_device` for over-aligned `T`; `alloc_shared` for host-visible buffers |
| Launch shape | `range<1>` (implementation-chosen work-group) | `nd_range<1>` with an explicit 256-item work-group and masked tail |
| Reductions | host round-trip | chunked device partial sums + tiny host finalize (portable: no local memory or sub-groups) |
| Kernels | map/transform only | `+ axpy_inplace`, `+ softmax_rows` |
| Device selection | `default_selector_v` | GPU-first custom selector, compile-time overridable |
| Backend reporting | none | `implementation_name`, `backend_info()` (implementation/platform/device/driver) |
| Testing | fallback only | fallback **and** the SYCL branch via a CPU stub (`scripts/run_gpu_stub_test.sh`); real toolchains via `scripts/run_gpu_build.sh` |

The header stays flat (fp's convention) until it passes ~500 lines; if phase 2
adds enough, it splits into `gpu.hpp` + `gpu_linalg.hpp` with the same
namespace.

## CPU fallback semantics

| Build | `available` | `usable()` | Behaviour |
|---|---|---|---|
| No `<sycl/sycl.hpp>` | `false` | `false` | Every function forwards to `fp::map_to`/`fp::transform_inplace`/`fp::fold_left`/`fp::dot`; `default_device_info()` returns `err("no SYCL device...")`. |
| SYCL, device supports USM | `true` | `true` | Kernels run on the default device; buffers are device memory. |
| SYCL, device without USM | `true` | `false` | Reductions take the host path; `Buffer::alloc` still returns `err` (device memory is unavailable) — use `alloc_shared` or the CPU. |

`available` answers "was SYCL compiled in"; `usable()` answers "is the selected
device actually suitable for device buffers".

## Memory model

- **USM device allocations**, not `sycl::buffer`. USM gives plain pointers, so
  kernels look like fp's span-based CPU kernels and `Buffer<T>` mirrors
  `fp::Buffer<T>` (`alloc`/`fill`/`clone`/move-only/`Result` factories).
- **Explicit transfers.** `from_host`/`to_host` are the only boundaries; there
  is no implicit host access to device memory (a device pointer is not
  dereferenceable on the host — documented).
- **Synchronous calls.** Each operation ends in `wait_and_throw()`, matching
  fp's value semantics. Phase 4 adds an event/async variant for overlap.
- **In-order queue.** Kernels and copies observe each other in submission
  order, which keeps the synchronous wrappers correct without extra
  dependencies.
- Later: shared-USM buffers for small data, pinned staging for big transfers,
  and a device-side `Buffer` pool.

## Error model

Every SYCL boundary is wrapped:

```cpp
try {
  detail::queue().parallel_for(...);
  detail::queue().wait_and_throw();     // surfaces async errors here
  return ok<void>();
} catch (sycl::exception const &e) {
  return err<void>(std::string("sycl: ") + e.what());
}
```

Common failures and their `Result` messages:

| Failure | Message |
|---|---|
| No SYCL in the build | `"no SYCL device: ForgeFP was built without SYCL"` |
| Device allocation fails | `"sycl: device allocation failed"` |
| Kernel/runtime error | `"sycl: <e.what()>"` |
| Size mismatch (`map_to`, `dot`) | `"gpu::map_to: destination too small"`, `"gpu::dot: size mismatch"` |

## Determinism

- Elementwise kernels (`map_to`, `transform_inplace`, `map`) are bit-identical
  to the CPU: same operations, same order per element.
- Reductions reorder (tree reduction across work-items). Phase 2's device
  `reduce`/`dot` will differ from the CPU in the last bits — compare with
  `fp::approx_equal`, exactly as `fp::simd::reduce` is already documented.
- The in-order queue plus one wait per call means two runs of the same program
  produce the same sequence of operations.

## What to accelerate (and what not to)

| Kernel | Expected win | Notes |
|---|---|---|
| `matmul` (large) | high | compute-bound above ~512²; transfer-bound below ~256² |
| `softmax_rows` | high | attention; one work-group per row |
| `reduce` / `dot` | medium | transfer dominates for small `n` |
| elementwise `map` | medium | only pays for large buffers; a single fused kernel beats map+map |
| optimizer `axpy` | low | tiny vectors unless the model is huge |
| per-frame / per-token tiny kernels | none | launch + transfer overhead dominates |

Rule of thumb: move work to the GPU when the buffer is large enough that
transfer is amortized (benchmark per kernel; `bench/gpu_bench.cpp` in phase 1),
and keep small or latency-sensitive work on the CPU/SIMD path.

## Build & packaging

The CPU build is unchanged (no flags, no dependency). GPU builds:

```bash
# Intel DPC++ (oneAPI)
icpx -std=c++20 -fsycl -I src app.cpp

# AdaptiveCpp (NVIDIA/AMD/Intel)
acpp -std=c++20 --acpp-targets=generic -I src app.cpp

# triSYCL (CPU, for testing the SYCL path without a GPU)
clang++ -std=c++20 -fsycl -I src app.cpp
```

CMake: `find_package(IntelSYCL)` or `find_package(AdaptiveCpp)` and link the
runtime; Forge: a `sycl` preset adding the flags. The library itself stays
header-only — only the application links a runtime.

## Testing

- **Fallback tests run everywhere**: `test/gpu_test.cpp` covers `available`,
  `usable`, device-info errors, buffer round-trips, shared buffers,
  `map_to`/`transform_inplace`/`map`, `axpy_inplace`, `softmax_rows`,
  `reduce`/`dot`, move semantics, and error paths. 9 tests, part of the normal
  suite (263 tests total today).
- **The SYCL branch is compiled and exercised on CPU by a stub**:
  `test/support/sycl/sycl.hpp` implements the subset of SYCL 2020 fp uses
  (`queue`, `range`/`nd_range`/`id`, USM allocation, `parallel_for`,
  `fill`/`memcpy`/`wait_and_throw`, device info, `exp`/`fmax`) with sequential
  host execution. `bench/gpu_stub_test.cpp` runs 20 checks through it; run
  `scripts/run_gpu_stub_test.sh`. This catches type errors, API misuse, buffer
  lifetime bugs, and value drift in the SYCL path without a GPU.
- **What the stub cannot verify**: parallelism, real device semantics, async
  error timing, and performance. On a GPU machine, compile with DPC++/
  AdaptiveCpp and run the same checks plus the gated tests.
- **Benchmarks**: `bench/gpu_bench.cpp` (phase 1, pending) measures transfer,
  `map`, `reduce`, `axpy`, and `matmul` against `fp::simd`/`fp::linalg`.

## Phases

| Phase | Contents | Gate |
|---|---|---|
| **0 (landed)** | detection, `available`, `default_device_info`, `Buffer<T>`, `map_to`, `transform_inplace`, `map`, host-side `reduce`/`dot`, fallback, tests | fallback suite green |
| **1 (landed except bench)** | `usable()` + USM-aspect fallback, over-aligned/shared allocation, `nd_range` launches, chunked device `reduce`/`dot`, `axpy_inplace`, `softmax_rows`, backend/device reporting, GPU-first selector, CPU SYCL stub test | fallback suite 264/264; stub harness all checks pass |
| 1b | `bench/gpu_bench.cpp`: transfer, `map`, `reduce`, `axpy`, `matmul` vs `fp::simd`/`fp::linalg` | a documented size threshold where the GPU wins |
| 2 | row/column reductions (`row_sums`, `col_means`, …) and `add_row_broadcast` on device; `softmax_rows` work-group-per-row variant | matches `fp::linalg`/`fp::numerics` |
| 3 | device `matmul` (work-group tiles, local memory, `nd_range`, vectorized loads) | beats `fp::linalg` CPU at ≥512² and matches it numerically |
| 4 | fusion of `fp::views` pipelines into one kernel, async/event API, explicit `Context`, multi-queue, device buffer pool | a `map`+`filter`+`map` pipeline compiles to one kernel; overlap measurable |
| 5 (optional) | `half`/`bfloat16`, sub-group reductions, device-side `serialize` | — |

## Risks and open questions

- **Header compile time.** `<sycl/sycl.hpp>` is heavy; opt-in is the mitigation.
  If it becomes a problem, a `FP_GPU_IMPLEMENTATION` split (declarations in the
  header, kernels in one `.cpp`) is the fallback plan — it breaks strict
  header-only, so it is a last resort.
- **Implementation differences.** USM reduction support, sub-group sizes,
  `default_selector_v`, and C++20 support vary between DPC++ and AdaptiveCpp.
  Phase 1 must feature-test (`device.has(aspect::...)`) and document the
  matrix.
- **No GPU in CI.** Correctness on device is verified only when a GPU machine
  is available; the fallback keeps the API honest.
- **Numerical drift.** Documented tolerance, `fp::approx_equal` in tests.
- **Thread safety.** The static in-order queue is initialized once
  (thread-safe), but concurrent callers would serialize on it; phase 4 adds
  per-thread queues.
- **Memory pressure.** Device allocations are explicit and RAII-managed by
  `Buffer`; a model that allocates per step must reuse buffers (the same rule
  as the CPU path).

## Non-goals

- CUDA/HIP-specific features, distributed or multi-GPU execution.
- Autotuning / kernel selection heuristics.
- Replacing `fp::simd` on the CPU (the fallback is scalar by design; SIMD
  remains the CPU fast path).
- ML semantics (the GPU layer accelerates the numeric kernels; losses, layers
  and training loops stay in the domain project).

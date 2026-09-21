# SYCL / GPU integration plan

ForgeFP has three execution tiers today: the eager CPU combinators, the
zero-cost in-place kernels, and the opt-in SIMD path (`simd.hpp`). This
document plans the fourth: **GPU kernels via SYCL**, opt-in and with a CPU
fallback so the library keeps its "compiles and runs everywhere" property.

**Consumers:** ForgeML uses this tier for dense layers, attention, optimizers
and metrics. `forge-gl` stays a CPU software renderer by decision (see
[`forge-gl/PERFORMANCE.md`](../forge-gl/PERFORMANCE.md)); its frame sizes are
below the elementwise crossover, and the measured numbers there are the
argument.

**Status:** Phases 0, 1, 1b, 1c, 2 and 3 are landed and verified on a real
device — `fp/gpu.hpp` (detection, `usable()`, USM `Buffer<T>` device + shared,
pinned `HostBuffer<T>` staging, elementwise kernels, `axpy_inplace`,
`softmax_rows` plus the work-group variant, row/column kernels, chunked device
reductions with a reusable `Scratch<T>`, tiled `matmul`/`batched_matmul`) with
a CPU fallback, gtest coverage, a CPU **SYCL stub** that compiles and exercises
the SYCL branch without a toolchain, and both header trees. The real-device run
is AdaptiveCpp `--acpp-targets=generic` on an RTX 3060 under WSL2 (see
[Verified toolchain](#verified-toolchain-adaptivecpp-on-wsl2)). Phases 4
(async, fusion, explicit context) and 5 (half/bfloat16) remain.

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
| **ForgeFP CPU stub** (`test/support`) | zero-dependency CI | sequential host kernels; verifies compilation, API use, buffer lifetimes, values, and kernel signatures (`id` vs `nd_item`) — not concurrency or performance |

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
| `sub_group` primitives (`reduce_over_group(sg, …)`) | replace the local-memory tree in `softmax_rows_wg`; faster small reductions |
| `sycl::vec<T, N>` loads/stores | vectorized elementwise kernels (`map`, `axpy`) and matmul tiles |
| `marray` / `sycl::span` | clean row/column views inside kernels |
| `sycl::half` / `bfloat16` | memory-bound kernels (phase 5) |
| `queue::submit` + events | overlap transfers with compute (async API, phase 4) |
| `device.has(aspect::usm_shared_allocations)` | use shared USM automatically for small buffers |

`local_accessor` + `group_barrier` are already in use (tiled `matmul`,
`softmax_rows_wg`), gated by `FP_GPU_GROUP_ALGORITHMS`: they are enabled
automatically for a real implementation and disabled for the CPU stub, which
cannot emulate barrier semantics. `FP_GPU_NO_GROUP_ALGORITHMS` forces the
portable path. Each remaining feature is gated the same way.

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
├── HostBuffer<T>              pinned host staging (reusable `malloc_host`)
├── Buffer<T>                  USM device block (host vector in fallback)
│     alloc / alloc_shared / from_host / to_host / fill / clone / move-only
│     copy_from(staging)       H2D through pinned staging (one DMA)
│     to_host(staging)         D2H into pinned staging (no allocation)
│     host_accessible()        true for shared USM (and the fallback)
├── Scratch<T>                 reusable reduce/dot workspace (device + pinned)
├── detail::launch_1d          nd_range launch, fixed 256 work-group size
├── map_to(dst, src, f)        dst[i] = f(src[i])
├── transform_inplace(buf, f)  buf[i] = f(buf[i])
├── map(range, f)              host in, host out
├── axpy_inplace(y, a, x)      y[i] += a * x[i]        (optimizer kernel)
├── transform_inplace_indexed  buf[i] = f(i, buf[i])   (masks, positions)
├── zip_transform_inplace      a[i] = f(a[i], b[i])
├── zip3_transform_inplace     a[i] = f(a[i], b[i], c[i])
├── softmax_rows(buf, r, c)    stable row-wise softmax (attention kernel)
├── softmax_rows_wg(…)         work-group-per-row variant (local memory)
├── row_sums / row_means / col_sums / add_row_broadcast
├── transpose                  tiled 16x16 (attention's Q @ Kᵀ)
├── matmul / batched_matmul    tiled 16x16 through local memory
└── reduce(buf, init[, scratch]) / dot(a, b[, scratch])
      device: one work-item per 1024-element chunk -> partials -> pinned
              read-back -> host finalize (reusable Scratch avoids the alloc)
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

The header stays flat (fp's convention: one header per module, as with
`concurrent.hpp`). It passed the planned ~500-line split point with phases 2–3
(1211 lines today); the `gpu.hpp` + `gpu_linalg.hpp` split was dropped because
it would force every consumer to include two headers and double the mirroring
surface, with no compile-time win worth that on this project.

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
- **Explicit transfers.** `from_host` / `copy_from` / `to_host` are the only
  boundaries; there is no implicit host access to device memory (a device
  pointer is not dereferenceable on the host — documented).
- **Pinned staging.** `HostBuffer<T>` (`sycl::malloc_host`) is DMA-able memory
  that roughly doubles transfer bandwidth (12.4 vs 4.5 GB/s H2D at 16 MiB).
  Allocate it once and reuse it: write into `staging.span()`, then
  `copy_from(staging)` for H2D; `to_host(staging)` for D2H.
- **Reusable scratch.** `Scratch<T>` owns the partials buffer and the pinned
  read-back that `reduce`/`dot` need; passing it is the difference between
  beating the CPU and losing to it (12× at 4M elements).
- **Synchronous calls.** Each operation ends in `wait_and_throw()`, matching
  fp's value semantics. Phase 4 adds an event/async variant for overlap.
- **In-order queue.** Kernels and copies observe each other in submission
  order, which keeps the synchronous wrappers correct without extra
  dependencies.
- Later: shared-USM buffers for small data and a device-side `Buffer` pool.

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

| Kernel | Expected win | Measured (RTX 3060 / Ryzen 5 5500) |
|---|---|---|
| `matmul` (large) | high | 22× at 1024²; wins from 128² up |
| `softmax_rows_wg` | high | attention; 62× at 4M elements, wins from 1 MiB |
| `row_sums` / `row_means` / `col_sums` | medium | wins from 1 MiB |
| `reduce` / `dot` | medium | win from 16 MiB **with a reused `Scratch`**; lose without one |
| elementwise `map` / `transform_inplace` | medium | 7–13× at 16 MiB; below ~1 MiB the CPU/SIMD path wins |
| optimizer `axpy` | low | 10× at 16 MiB; tiny vectors stay on the CPU |
| per-frame / per-token tiny kernels | none | launch + transfer overhead dominates |

Rule of thumb: move work to the GPU when the buffer is large enough that
transfer is amortized (benchmark per kernel; `bench/gpu_bench.cpp` in phase 1),
and keep small or latency-sensitive work on the CPU/SIMD path.

### Measured crossovers (RTX 3060, WSL2)

`bench/gpu_bench.cpp` (run via `scripts/run_gpu_build.sh --bench`) compares each
kernel against the fastest CPU path (`fp::inplace` / `fp::simd` / `fp::linalg` /
`fp::numerics`) at 1K, 16K, 256K and 4M `float` elements — best of three
samples, kernel cache warm. `fp::gpu` calls are synchronous, so the GPU numbers
include launch + `wait_and_throw()`. Throughputs are the bench's convention:
bytes *read* per second (an in-place elementwise kernel moves twice that).

| Kernel | GPU wins from | At 4M elements | GPU throughput |
|---|---|---|---|
| `transform_inplace` | 4M (16 MiB) | 216 µs vs 1.44 ms CPU (7×) | 78 GB/s |
| `map_to` | 4M | 219 µs vs 2.78 ms (13×) | 76 GB/s |
| `axpy_inplace` | 4M | 293 µs vs 2.81 ms (10×) | 57 GB/s |
| `softmax_rows` (portable) | 256K (1 MiB) | 2.04 ms vs 23.5 ms (12×) | 8 GB/s |
| `softmax_rows_wg` | 256K | 378 µs vs 23.5 ms (62×) | 44 GB/s |
| `row_sums` | 256K | 951 µs vs 2.84 ms (3×) | 18 GB/s |
| `reduce` (with `Scratch`) | 4M | 309 µs vs 1.01 ms (3×) | 54 GB/s |
| `dot` (with `Scratch`) | 4M | 476 µs vs 1.62 ms (3×) | 35 GB/s |
| `matmul` (tiled) | 128² | 4.04 ms vs 89.2 ms at 1024² (22×) | 532 GFLOP/s |

`matmul` is the clearest win: 52 / 244 / 227 / 532 GFLOP/s at 128²/256²/512²/
1024² against ~24 GFLOP/s for `fp::linalg` on this CPU.

Two variants exist because the first version of each kernel lost:

- **`reduce`/`dot` without a `Scratch`** (allocating the partials buffer and
  read-back per call) cost **3.9 ms at 4M** — 12× slower than with a reused
  scratch, and slower than the CPU. The allocation, not the reduction, was the
  problem.
- **`softmax_rows` without the work-group kernel** is 5.4× slower than
  `softmax_rows_wg` at 4M and only crosses over at 1 MiB.

Transfer (16 MiB, reusable pinned staging):

| Path | Throughput |
|---|---|
| H2D pageable | 4.5 GB/s |
| H2D pinned (`copy_from`, one DMA) | 12.4 GB/s |
| D2H pageable | 5.3 GB/s |
| D2H pinned (`to_host(staging)`) | 12.1 GB/s |

The per-call costs that explain the thresholds (same machine):

| Operation | Cost |
|---|---|
| Kernel submission + `wait()` | 0.14–0.36 ms (occasional multi-ms outliers) |
| `sycl::malloc_device` | ~10 µs |
| `sycl::free` | ~0.6 ms |
| `sycl::malloc_host` + `free` | ~2.6 ms |

What the numbers mean for the design:

1. **Pinned staging only pays when it is reused and written directly.** The
   allocation costs ~2.6 ms per call, so `HostBuffer<T>` is allocated once; and
   copying an existing `std::vector` *through* staging (an extra host copy) is
   slower than the pageable path — hence `copy_from`/`to_host(staging)` and no
   `from_host(src, staging)` overload.
2. **Reusable scratch is not an optimization, it is the difference between
   winning and losing** for `reduce`/`dot` (12× at 4M). The two-argument
   overloads still allocate, so hot loops should pass a `Scratch<T>`.
3. **For elementwise work the crossover is size-driven, not kernel-driven:**
   below ~1 MiB the CPU/SIMD path wins outright; at 16 MiB the GPU wins by
   7–13×. Row-oriented kernels (`softmax_rows_wg`, `row_sums`) cross earlier
   (1 MiB) because their CPU counterparts are scalar loops.
4. **Launch variance is high on WSL2** (0.14 ms typical, multi-ms outliers), so
   per-frame and per-token kernels stay on the CPU/SIMD path.
5. **Synchronous calls dominate at small sizes.** 50 small kernels cost
   0.056–0.076 ms each when each call waits, but 0.016–0.017 ms each when
   submitted as a batch and waited once — so phase 4's async API is what makes
   the tier worthwhile for small models, not just an optimization. Until it
   lands, a consumer with many small kernels should keep them on the CPU or
   batch the work into fewer, larger kernels.
6. **`sycl::half` works** on the reference setup (verified: a `half` kernel
   runs and produces the right values), so phase 5 is unblocked when mixed
   precision is wanted. `bfloat16` is a oneAPI extension, not SYCL 2020 core,
   and would need a feature-gated fallback.

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

Concretely, with AdaptiveCpp (the verified setup), a consumer enables the tier
on its own target:

```cmake
set(ACPP_EXTRA_ARGS "--acpp-targets=generic")  # before find_package
find_package(AdaptiveCpp CONFIG REQUIRED)      # provides add_sycl_to_target
add_sycl_to_target(TARGET my_app)              # compile this target through acpp
target_link_libraries(my_app PRIVATE forgefp AdaptiveCpp::acpp-rt)
```

`add_sycl_to_target` installs a compiler *launcher*: the target keeps building
with the host compiler and the launcher rewrites the command line, so no flags
need to be sprinkled on the headers. Do not call `target_sources` on the target
after it (AdaptiveCpp's own warning — it breaks flag dependency tracking). For
DPC++, the equivalent is `find_package(IntelSYCL)` plus `-fsycl`.

### Verified toolchain: AdaptiveCpp on WSL2

The reference setup used while landing phase 1: Windows 11 + WSL2 (Arch Linux),
an RTX 3060 (12 GiB), and AdaptiveCpp built from source into `/opt/adaptivecpp`
with the CUDA backend, using the LLVM 20 toolchain. The working invocation:

```bash
export PATH=/opt/adaptivecpp/bin:$PATH
export LD_LIBRARY_PATH=/usr/lib/wsl/lib:$LD_LIBRARY_PATH  # WSL driver shim (libcuda.so.1)

acpp -std=c++20 -O2 -Isrc --acpp-targets=generic app.cpp
# or: scripts/run_gpu_build.sh [--bench] [--info]
```

`--acpp-targets=generic` is the portable SSCP flow: it compiles to LLVM IR and
JITs to the device at runtime through the CUDA driver API. Do **not** use an
explicit `cuda:sm_86` target in this setup: clang 20's CUDA frontend does not
accept the CUDA 13.x headers (NVIDIA restructured/removed legacy headers clang
still expects), so the explicit target fails to compile. If AOT compilation is
ever needed, the durable fix is a pinned CUDA 12.x toolkit in a separate
prefix.

Measured on that machine (warm cache, 16 MiB buffers):

| Operation | Result |
|---|---|
| Elementwise kernel (`x *= k`, read + write) | 173 GB/s |
| Device-to-device copy | 50–100 GB/s |
| Host→device, pageable | 6.4 GB/s |
| Host→device, pinned (`sycl::malloc_host`) | 12.5 GB/s |
| Kernel submission | 0.12 ms |
| Submission + `wait()` | 0.17 ms |

Two consequences the design has to respect:

- **JIT cold start.** AdaptiveCpp compiles each kernel on first use
  (100–300 ms, cached across runs in the kernel cache). Benchmarks must warm up
  and discard the first iterations; an interactive application should pre-warm
  its kernels at startup.
- **Transfers dominate small work.** At 6–12 GB/s, moving data costs more than
  the arithmetic for small buffers, so the crossover per kernel is phase 1b's
  benchmark.

Two environment quirks that are *not* errors: `acpp-info` prints
`ocl_hardware_manager: Could not obtain platform list (CL:-1001)` because no
OpenCL ICD is registered (harmless for the CUDA/CPU backends), and the
`kernel_cache: ... JIT-compiled` warning on a first run is the cold start above.

## Testing

- **Fallback tests run everywhere**: `test/gpu_test.cpp` covers `available`,
  `usable`, device-info errors, buffer round-trips, shared buffers,
  `map_to`/`transform_inplace`/`map`, `axpy_inplace`, `softmax_rows` (both
  variants), pinned staging, row/column kernels, `reduce`/`dot` with and
  without `Scratch`, `matmul`/`batched_matmul` (including non-tile-multiple
  sizes), move semantics, and error paths. 17 tests, part of the normal suite
  (271 tests total today).
- **The SYCL branch is compiled and exercised on CPU by a stub**:
  `test/support/sycl/sycl.hpp` implements the subset of SYCL 2020 fp uses
  (`queue`, `range`/`nd_range`/`id`/`nd_item`, USM allocation, `parallel_for`,
  `fill`/`memcpy`/`wait_and_throw`, device info, `exp`/`fmax`) with sequential
  host execution. It models the item types exactly — a `range` launch passes an
  `id`, an `nd_range` launch an `nd_item` — so a kernel signature a real
  implementation would reject fails to compile here too. (The first version
  passed `id` to `nd_range` kernels and only the real toolchain caught it.)
  `bench/gpu_stub_test.cpp` runs the whole API through it (33 checks,
  including the row/column, scratch, and matmul kernels); run
  `scripts/run_gpu_stub_test.sh`. This catches type errors, API misuse, buffer
  lifetime bugs, and value drift in the SYCL path without a GPU.
- **What the stub cannot verify**: parallelism, work-group barrier semantics
  (so the tiled kernels take their portable path there), async error timing,
  and performance. On a GPU machine, `bench/gpu_smoke.cpp` runs the whole API
  against the device — including the tiled `matmul` (checked against
  `fp::matmul` on a 33×20×17 case) and `softmax_rows_wg` (checked against the
  portable kernel) — via `scripts/run_gpu_build.sh`.
- **Benchmarks**: `bench/gpu_bench.cpp` measures transfer (pageable vs pinned
  staging), `transform_inplace`, `map_to`, `reduce`/`dot` (with and without
  `Scratch`), `axpy`, `softmax_rows` (both variants), `row_sums`, and `matmul`
  against `fp::inplace`/`fp::simd`/`fp::linalg`/`fp::numerics`; run
  `scripts/run_gpu_build.sh --bench`.

## Phases

| Phase | Contents | Gate |
|---|---|---|
| **0 (landed)** | detection, `available`, `default_device_info`, `Buffer<T>`, `map_to`, `transform_inplace`, `map`, host-side `reduce`/`dot`, fallback, tests | fallback suite green |
| **1 (landed)** | `usable()` + USM-aspect fallback, over-aligned/shared allocation, `nd_range` launches, chunked device `reduce`/`dot`, `axpy_inplace`, `softmax_rows`, backend/device reporting, GPU-first selector, CPU SYCL stub test | fallback suite 264/264; stub harness all checks pass; `gpu_smoke` green on an RTX 3060 (AdaptiveCpp `generic`) |
| **1b (landed)** | `bench/gpu_bench.cpp`: transfer (pageable vs pinned), `transform_inplace`, `map_to`, `reduce`, `dot`, `axpy`, `softmax_rows` vs `fp::inplace`/`fp::simd`/`fp::numerics` | crossovers documented in [Phase 1b](#phase-1b-measured-crossovers-rtx-3060-wsl2) |
| **1c (landed)** | pinned staging: reusable `HostBuffer<T>` (`sycl::malloc_host`), `copy_from` / `to_host(staging)` | 2.8× / 2.3× pageable transfer at 16 MiB, no per-call allocation |
| **2 (landed)** | `row_sums` / `row_means` / `col_sums` / `add_row_broadcast`; `softmax_rows_wg` (local memory + barriers); reusable `Scratch<T>` for `reduce`/`dot` | matches `fp::linalg`/`fp::numerics`; GPU wins from 256K (rows) and 4M (reductions) |
| **2b (landed)** | `transform_inplace_indexed`, `zip_transform_inplace`, `zip3_transform_inplace`, tiled `transpose` — the position-aware and multi-input kernels the attention and AdamW paths need | matches the host kernels; device tests include a non-tile-multiple transpose |
| **3 (landed)** | tiled `matmul` / `batched_matmul` (16×16 tiles, local memory, masked edges) | beats `fp::linalg` at every measured size (22× at 1024²) and matches it numerically |
| 4 | fusion of `fp::views` pipelines into one kernel, async/event API, explicit `Context`, multi-queue, device buffer pool | a `map`+`filter`+`map` pipeline compiles to one kernel; overlap measurable. **Now justified by measurement:** 50 small kernels cost 0.056–0.076 ms/call synchronously but 0.016–0.017 ms/call submitted in a batch (one wait) — ~2–3 ms saved per model step, which is 20× the actual GPU compute at the tiny-model sizes ForgeML targets |
| 5 (optional) | `half`/`bfloat16`, sub-group reductions, device-side `serialize` | — |

## Risks and open questions

- **Header compile time.** `<sycl/sycl.hpp>` is heavy and `gpu.hpp` is now 1211
  lines; opt-in is the mitigation. If it becomes a problem, a
  `FP_GPU_IMPLEMENTATION` split (declarations in the header, kernels in one
  `.cpp`) is the fallback plan — it breaks strict header-only, so it is a last
  resort.
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

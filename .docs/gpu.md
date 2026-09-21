# GPU kernels (SYCL) — `gpu.hpp`

`gpu.hpp` is the fourth execution tier: device kernels through SYCL, with a CPU
fallback, so the same code compiles and runs with or without a toolchain. It is
**opt-in** (not in `all.hpp`), like `simd.hpp` and `autodiff.hpp`.

- Design, phases, and the measured crossovers: [`GPU.md`](../GPU.md).
- The consumer in this workspace is ForgeML; `forge-gl` stays CPU-only by
  decision.

```cpp
#include <fp/gpu.hpp>
```

Without `<sycl/sycl.hpp>` on the include path, `fp::gpu::available` is `false`
and every function forwards to the CPU implementation (`fp::map_to`,
`fp::fold_left`, `fp::matmul`, …). The API is identical either way.

## Quick start

```cpp
#include <fp/gpu.hpp>

int main() {
  if (!fp::gpu::usable()) {
    // No device (or no USM support): use the CPU path.
    return 0;
  }

  // Device block from host data; explicit transfers, no implicit access.
  auto x = fp::gpu::Buffer<float>::from_host(std::vector<float>{1, 2, 3, 4});
  auto y = fp::gpu::Buffer<float>::alloc(4);

  fp::gpu::map_to(y.value(), x.value(), [](float v) { return v * v; });
  fp::gpu::axpy_inplace(y.value(), 0.5f, x.value());          // y += 0.5x

  const auto total = fp::gpu::reduce(y.value(), 0.0f);        // Result<float>
  const auto host = y.value().to_host();                      // Result<vector>
}
```

Every operation returns `fp::Result`; `sycl::exception` never escapes.

## The types

| Type | What it is |
|---|---|
| `Buffer<T>` | Owning device block (USM). `alloc` / `alloc_shared` / `from_host` / `to_host` / `fill` / `clone`; move-only. `data()` is a plain pointer, so kernels look like fp's span-based CPU kernels. |
| `HostBuffer<T>` | Pinned host staging (`sycl::malloc_host`). Allocate once, reuse: write into `span()`, then `copy_from(staging)` for H2D, `to_host(staging)` for D2H. Roughly 2× the bandwidth of pageable memory. |
| `Scratch<T>` | Reusable workspace for `reduce`/`dot`. Passing one is the difference between beating the CPU and losing to it. |

```cpp
auto staging = fp::gpu::HostBuffer<float>::alloc(1 << 20);
std::copy(input.begin(), input.end(), staging.value().span().begin());

auto dev = fp::gpu::Buffer<float>::alloc(input.size());
dev.value().copy_from(staging.value());        // one DMA, no extra copy

fp::gpu::Scratch<float> scratch;               // reuse across calls
auto total = fp::gpu::reduce(dev.value(), 0.0f, scratch);
dev.value().to_host(staging.value());          // no allocation
std::span<float> values = staging.value().span();
```

Rules of thumb (measured, see `GPU.md`):

- Below ~1 MiB per tensor the CPU/SIMD path is faster; the GPU pays from
  ~16 MiB for elementwise work, ~1 MiB for row kernels, and 128² for `matmul`.
- Reuse `HostBuffer` and `Scratch`; per-call allocation costs more than the
  work itself (`sycl::free` ≈ 0.6 ms, `malloc_host` + free ≈ 2.6 ms).
- Do not route an existing `std::vector` *through* staging — the extra host copy
  makes it slower than `from_host`. Staging pays when the producer writes into
  it directly.

## Kernels

| Function | Effect | CPU counterpart |
|---|---|---|
| `map_to(dst, src, f)` | `dst[i] = f(src[i])` | `fp::map_to` |
| `transform_inplace(buf, f)` | `buf[i] = f(buf[i])` | `fp::transform_inplace` |
| `transform_inplace_indexed(buf, f)` | `buf[i] = f(i, buf[i])` | `fp::for_each_index` |
| `zip_transform_inplace(a, b, f)` | `a[i] = f(a[i], b[i])` | `fp::zip_transform_inplace` |
| `zip3_transform_inplace(a, b, c, f)` | `a[i] = f(a[i], b[i], c[i])` | `fp::zip3_for_each` |
| `axpy_inplace(y, a, x)` | `y[i] += a * x[i]` | `fp::axpy_inplace` |
| `softmax_rows(buf, rows, cols)` | stable row-wise softmax | `fp::softmax_rows` |
| `softmax_rows_wg(…)` | work-group-per-row variant (local memory) | `fp::softmax_rows` |
| `row_sums` / `row_means` / `col_sums` | row/column reductions | `fp::row_sums`, … |
| `add_row_broadcast(buf, rows, cols, bias)` | `buf[i][j] += bias[j]` | `fp::add_row_broadcast` |
| `transpose(src, rows, cols)` | tiled row-major transpose | `fp::transpose` |
| `matmul(a, b, m, k, n)` | `m×k · k×n`, row-major | `fp::matmul` |
| `batched_matmul(a, b, batches, m, k, n)` | batched, back to back | `fp::batched_matmul` |
| `reduce(buf, init[, scratch])` | sum | `fp::fold_left` / `fp::reduce` |
| `dot(a, b[, scratch])` | inner product | `fp::dot` |

`softmax_rows_wg`, the tiled `transpose`, and `matmul` use local memory and
work-group barriers. They are enabled automatically on a real implementation
and take the portable path on the CPU stub (which cannot emulate barriers);
define `FP_GPU_NO_GROUP_ALGORITHMS` to force the portable path everywhere.

## Device selection

The default queue prefers a GPU, then a CPU. Override at compile time:

```bash
acpp -std=c++20 --acpp-targets=generic -DFP_GPU_DEVICE_CPU app.cpp
```

`fp::gpu::available` (compile-time), `fp::gpu::usable()` (does the selected
device support USM device allocations), `fp::gpu::default_device_info()` and
`fp::gpu::backend_info()` report what was selected at runtime.

## Errors

| Failure | Message |
|---|---|
| No SYCL in the build | `"no SYCL device: ForgeFP was built without SYCL"` |
| Allocation failed | `"sycl: device allocation failed"` / `"sycl: <e.what()>"` |
| Shape/size mismatch | `"gpu::<kernel>: shape mismatch"` / `"…: size mismatch"` |
| Staging too small | `"gpu::copy_from: staging buffer too small"` |

## Determinism

Elementwise kernels are bit-identical to the CPU. `reduce`, `dot`, `softmax_*`
and `matmul` reorder arithmetic, so compare with `fp::approx_equal` — the same
rule `fp::simd` follows.

## Building

```bash
# AdaptiveCpp (verified: RTX 3060 under WSL2)
acpp -std=c++20 -O2 -Isrc --acpp-targets=generic app.cpp

# oneAPI DPC++
icpx -std=c++20 -fsycl -Isrc app.cpp
```

The library stays header-only; only the application links a runtime. In CMake,
with AdaptiveCpp:

```cmake
set(ACPP_EXTRA_ARGS "--acpp-targets=generic")
find_package(AdaptiveCpp CONFIG REQUIRED)
add_sycl_to_target(TARGET my_app)              # builds my_app through acpp
target_link_libraries(my_app PRIVATE forgefp AdaptiveCpp::acpp-rt)
```

`add_sycl_to_target` installs a compiler launcher, so the target keeps building
with the host compiler and no flags need to be sprinkled on the headers.

## Testing

- `scripts/run_gpu_stub_test.sh` — runs the whole API on CPU through
  `test/support/sycl/sycl.hpp`, which models the SYCL item types, so kernel
  signatures are type-checked without a GPU.
- `scripts/run_gpu_build.sh [--bench] [--info]` — real toolchain: the
  correctness harness (`bench/gpu_smoke.cpp`) and the crossover benchmark
  (`bench/gpu_bench.cpp`).
- `test/gpu_test.cpp` — the API on the CPU fallback, part of the normal suite.

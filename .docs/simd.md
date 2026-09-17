# SIMD — `simd.hpp`

Vectorized mapping, reduction, and math over `std::vector`, backed by
`std::experimental::simd` (GCC/Clang). **Opt-in**: `simd.hpp` is *not* in
`all.hpp` — include it explicitly and compile with `-march=native` (or a
specific `-mavx2`/`-mavx512`) so `native_simd` is actually a vector type.

```cpp
#include <fp/simd.hpp>
```

Build:

```bash
g++ -std=c++20 -O2 -march=native -I src app.cpp
```

`fp::vec<T>` is `std::experimental::native_simd<T>` — the widest vector for the
target CPU.

**Why SIMD here:** the compiler auto-vectorizes simple arithmetic loops on its
own, but it *can't* vectorize a call to `std::sqrt`/`std::exp` (those are
opaque libm calls). `simd.hpp` exposes the vector intrinsics as *functions you
map with*, so the math cases — where the win is — are one line. The mental
model is the same as `vec.hpp`: your lambda just receives a whole vector
instead of one element.

## Mapping

```cpp
std::vector<double> v = {1, 2, 3, 4, 5};

fp::map_inplace(v, [](fp::vec<double> x) { return x * 2.0 + 1.0; });  // in place
auto out = fp::map_to(v, [](fp::vec<double> x) { return x * x; });    // new vector
```

The lambda receives a **whole vector** (several lanes) and returns a vector.
`map_inplace` writes back in place; `map_to` allocates a new vector. The scalar
tail (the elements that don't fill a full vector) is handled for you.

```cpp
// fixed-width with a masked tail
fp::map_inplace_fixed<double, 8>(v, [](auto x) { return x * 2.0; });
```

`fp::simd<T, N>` / `fp::simd_mask<T, N>` are fixed-width aliases, with
`fp::vec2f`, `fp::vec4f`, `fp::vec8f`, `fp::vec16f`, `fp::vec2d`, `fp::vec4d`,
`fp::vec8d` conveniences.

## Reductions

```cpp
double sum = fp::reduce(v);            // sum, SIMD lanes then a horizontal add
double dot = fp::dot(a, b);            // dot product (fused multiply-accumulate)
```

## Math

```cpp
fp::map_sqrt(v);    // SIMD sqrt  (vs. a libm call per element)
fp::map_exp(v);     // SIMD exp

fp::map_math(v, [](fp::vec<double> x) { return std::experimental::sin(x); });

fp::clamp_inplace(v, 0.0, 1.0);        // clamp to [lo, hi]
fp::normalize(v);                      // divide by max |x|
fp::threshold_inplace(v, lo, hi, 0.0); // out-of-range -> replace
```

## Parallel + SIMD

```cpp
fp::ThreadPool pool(8);
fp::par_map_inplace(pool, v, [](fp::vec<double> x) { return x + 1.0; });
```

`par_map_inplace` splits the vector across pool workers and applies a SIMD
lambda to each chunk.

## Indexed gather

```cpp
auto picked = fp::gather(v, std::vector<size_t>{2, 0, 4});  // {v[2], v[0], v[4]}
```

## What to expect (and why)

- **`reduce` / `dot`** beat `std::accumulate` / a naive loop (typically 1.5–4×)
  — reductions don't auto-vectorize as cleanly, so the manual version wins.
- **`map_sqrt` / `map_exp`** are where SIMD shines — the compiler can't
  auto-vectorize a libm `sqrt` call, so a SIMD intrinsic is ~3× faster.
- **`map_inplace` on trivial arithmetic** (`x*2+1`) usually only *matches* the
  compiler's own auto-vectorized scalar loop — GCC already vectorizes that
  pattern, so there's nothing left to win. SIMD pays off for math functions and
  reductions, not for the cases the compiler already handles.

Alignment note: `map_inplace`/`map_to` use `element_aligned` (safe for any
`std::vector` data); `vector_aligned` would require a custom 32/64-byte-aligned
allocator and is not used by default. On modern CPUs, aligned vs unaligned
loads are effectively free anyway.

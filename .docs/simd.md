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
target CPU (4 `double` lanes on AVX2, 8 on AVX-512, 2 on SSE).

## Why SIMD here

The compiler auto-vectorizes simple arithmetic loops on its own, but it *can't*
vectorize a call to `std::sqrt`/`std::exp` (those are opaque libm calls), and
it often gives up on reductions (the accumulator dependency chain blocks it).
`simd.hpp` exposes the vector intrinsics as *functions you map with*, so the
cases where the win is — math functions and reductions — are one line. The
mental model is the same as `vec.hpp`: your lambda just receives a whole vector
instead of one element.

## The mental model: lanes and tails

A SIMD operation works on `W` elements at once, where `W` is the lane count of
`fp::vec<T>`:

```text
data:   [ d0 d1 d2 d3 | d4 d5 d6 d7 | d8 ]      n = 9, W = 4
chunk:  [  lanes 0-3  ][  lanes 4-7  ][ tail ]  tail = 1 element
```

Every function in this header handles the tail for you: full vectors for the
bulk, a scalar (or masked) step for the remainder. You never write the cleanup
loop.

Two widths are available:

```cpp
fp::vec<double> v;                       // native (runtime-selected width)
fp::simd<double, 8> fixed;               // exactly 8 lanes (AVX-512 or emulated)
fp::vec4f, fp::vec8f, fp::vec2d, …       // common fixed aliases
```

## Mapping

```cpp
std::vector<double> v = {1, 2, 3, 4, 5};

fp::map_inplace(v, [](fp::vec<double> x) { return x * 2.0 + 1.0; });  // in place
auto out = fp::map_to(v, [](fp::vec<double> x) { return x * x; });    // new vector
```

The lambda receives a **whole vector** and returns a vector. `map_inplace`
writes back in place; `map_to` allocates a new vector (and takes the SIMD
result type, so `map_to` can change the element type: `map_to(v, f)` where `f`
returns `fp::vec<float>` yields `vector<float>`).

```cpp
// fixed-width with a masked tail
fp::map_inplace_fixed<double, 8>(v, [](auto x) { return x * 2.0; });
```

`map_inplace`/`map_to` are overloaded for `std::span`, so they work on
`fp::Buffer<T>` and any contiguous view.

## Math

```cpp
fp::map_sqrt(v);    // SIMD sqrt  (vs. a libm call per element)
fp::map_exp(v);     // SIMD exp

fp::map_math(v, [](fp::vec<double> x) { return std::experimental::sin(x); });

fp::clamp_inplace(v, 0.0, 1.0);        // clamp to [lo, hi]
fp::normalize(v);                      // divide by max |x|
fp::threshold_inplace(v, lo, hi, 0.0); // out-of-range -> replace
```

`clamp_inplace`, `normalize`, and `threshold_inplace` are the data-shaping
primitives: clamp activations, scale a signal, blank outliers. They all operate
in place with no allocation.

### `axpy_inplace` — the optimizer kernel

```cpp
std::vector<double> w = /* weights */;
std::vector<double> g = /* gradients */;

fp::axpy_inplace(w, -lr, g);           // w += (-lr) * g  ==  w -= lr * g
```

`y += a * x` is the single most common update in numerical code (gradient
descent, momentum, exponential smoothing). It is one fused multiply-add per
lane, and it is the reason a parameter update stays allocation-free. The scalar
tail is handled for you; if `y` and `x` differ in length, the common prefix is
updated.

## Reductions

```cpp
double sum = fp::reduce(v);            // sum, SIMD lanes then a horizontal add
double dot = fp::dot(a, b);            // dot product (fused multiply-accumulate)
```

`reduce` accumulates into a vector register and does one horizontal add at the
end — no long dependency chain, which is exactly what the compiler struggles
with. `dot` is the fused version (`acc += x * y` per lane).

## Parallel + SIMD

```cpp
fp::ThreadPool pool(8);
fp::par_map_inplace(pool, v, [](fp::vec<double> x) { return x + 1.0; });
```

`par_map_inplace` splits the vector across pool workers and applies a SIMD
lambda to each chunk. The two optimizations compose: threads for the bulk,
lanes for the math. For a plain (non-SIMD) parallel map, see
[`fp::par_map`](concurrency.md).

## Indexed gather

```cpp
auto picked = fp::gather(v, std::vector<size_t>{2, 0, 4});  // {v[2], v[0], v[4]}
```

Useful for embedding lookups and permutation tables.

## What to expect (and why)

Measured on 4M doubles (`bench/simd_bench.cpp`):

| Case | Naive | fp | Why |
|---|---|---|---|
| sum | `std::accumulate` | `fp::reduce` | the accumulator chain blocks auto-vectorization |
| dot | product-sum loop | `fp::dot` | fused multiply-add per lane |
| `sqrt` | libm call per element | `fp::map_sqrt` | libm calls are opaque to the vectorizer |
| `exp` | libm call per element | `fp::map_exp` | same |
| `x*2+1` | scalar loop | `fp::map_inplace` | *matches* — GCC already vectorizes this |

The last row is the important lesson: **SIMD pays off for math functions and
reductions, not for trivial arithmetic the compiler already handles.** Reach
for `simd.hpp` when the operation is a libm call, a transcendental, or a
reduction — not because "vectorized" sounds faster.

## Alignment

`map_inplace`/`map_to` use `element_aligned` (safe for any `std::vector` data);
`vector_aligned` would require a custom 32/64-byte-aligned allocator and is not
used by default. On modern CPUs, aligned vs unaligned loads are effectively
free anyway. If you control the buffer, `fp::Arena` blocks are 64-byte aligned,
so arena-backed spans are naturally vector-aligned.

## Gotchas

- **Opt-in.** `simd.hpp` is not in `all.hpp`; include it explicitly.
- **Compile flags matter.** Without `-march=native` (or an explicit ISA flag),
  `native_simd` degrades to the baseline width and the win shrinks.
- **The lambda takes a vector, not an element.** `map_inplace(v, [](double x){…})`
  does not compile; the parameter must be `fp::vec<T>` (or `auto`).
- **Small vectors are not worth it.** For `n` below a few hundred elements, the
  setup and tail handling can dominate; measure before switching a hot path.
- **Reductions are reordered.** `reduce`/`dot` sum lanes in a different order
  than a scalar loop, so floating-point results can differ in the last bits.
  For exact-order sums, use `fp::fold_left`.
- **`par_map_inplace` needs `-pthread`** (it uses the thread pool).

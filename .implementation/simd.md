# simd.hpp — implementation status

Current surface: `vec<T>` alias (`native_simd<T>`), `map_inplace(vector<T>&,
F)` with scalar tail. The module does in-place transform only — no
reductions, no out-of-place maps, no math overloads, no tail policy beyond
the scalar loop.

## Implemented

| Feature | Status |
|---|---|
| `vec<T>` alias | done (pre-existing) |
| `map_inplace` | done (pre-existing) |

Nothing from the backlog has landed yet. `bench/simd_bench.cpp` has the
`reduce`/`dot` cases commented out — uncomment as each lands.

## Backlog

## 1. `reduce` — vectorized sum/min/max [P0]

```
template <class T> T reduce(std::vector<T> const& v, T init = T{});
// T: arithmetic, libstdc++ native_simd<T> with +, min, max (via std::experimental::min)
```

**Why:** Reductions are the *classic* SIMD win (sum over millions of samples,
max of a signal). Today the library can transform in SIMD but can only
aggregate with a scalar loop — leaving the single most common analysis
operation slower than the machine can do it.

**Implementation**

```cpp
template <class T>
T reduce(std::vector<T> const& v, T init = T{}) {
  using Vec = fp::vec<T>;
  constexpr size_t lanes = Vec::size();
  Vec acc{init};
  size_t i = 0;
  for (; i + lanes <= v.size(); i += lanes) {
    Vec chunk;
    chunk.copy_from(&v[i], std::experimental::element_aligned);
    acc += chunk;
  }
  T total = init;
  for (size_t k = 0; k < lanes; ++k)
    total += acc[k];
  for (; i < v.size(); ++i)
    total += v[i];
  return total;
}
```

For min/max variants, accumulate with `std::experimental::min(acc, chunk)`
(`max` likewise) instead of `+=`.

## 2. `dot` — vectorized dot product [P1]

```
template <class T> T dot(std::vector<T> const& a, std::vector<T> const& b);
```

**Why:** The building block of linear algebra kernels (embeddings,
similarity scoring, ML features); fuses multiply-accumulate into one wide
loop. Pairs with `reduce` to give the module a numeric-kernels story.

**Implementation** — multiply-accumulate per lane (fusable by the compiler)

```cpp
template <class T>
T dot(std::vector<T> const& a, std::vector<T> const& b) {
  using Vec = fp::vec<T>;
  constexpr size_t lanes = Vec::size();
  Vec acc{};
  size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  for (; i + lanes <= n; i += lanes) {
    Vec x, y;
    x.copy_from(&a[i], std::experimental::element_aligned);
    y.copy_from(&b[i], std::experimental::element_aligned);
    acc += x * y;
  }
  T total = 0;
  for (size_t k = 0; k < lanes; ++k)
    total += acc[k];
  for (; i < n; ++i)
    total += a[i] * b[i];
  return total;
}
```

## 3. `map_to` — out-of-place, possibly different result type [P1]

```
template <class T, class R>
std::vector<R> map_to(std::vector<T> const& src, F1 f);   // f: vec<T> -> vec<R>, tail scalar
```

**Why:** `map_inplace` forces mutation of the input — callers transform
read-only data (const sources, shared buffers, `std::span` views of shared
memory) or convert element types (float→double, int→float). A pure
`map_to(src, f)` returning a fresh vector is the "map" half that `vec.hpp`'s
`map` already provides in scalar form; without it, SIMD users must copy-then-
mutate, which doubles memory traffic and undermines the point.

**Implementation**

```cpp
template <class T, class F>
auto map_to(std::vector<T> const& src, F f) {
  using VecT = fp::vec<T>;
  using VecR = std::invoke_result_t<F, VecT>;
  using R = typename VecR::value_type;
  constexpr size_t lanes = VecT::size();
  std::vector<R> out(src.size());
  size_t i = 0;
  for (; i + lanes <= src.size(); i += lanes) {
    VecT x;
    x.copy_from(&src[i], std::experimental::element_aligned);
    VecR y = f(x);
    y.copy_to(&out[i], std::experimental::element_aligned);
  }
  for (; i < src.size(); ++i)
    out[i] = f(VecT(src[i]))[0];
  return out;
}
```

## 4. Masked tail handling (no scalar tail) [P1]

```
template <class T, size_t N, class F>
void map_inplace_fixed(std::vector<T>& v, F f);   // simd<T, N> + simd_mask tail
```

**Why:** The scalar tail of `map_inplace` is scalar — for widths where the
tail is large relative to full chunks (e.g. 1023 elements with 8-wide
vectors: 127 scalar iterations), it can dominate runtime. libstdc++ supports
`copy_from(ptr, flags, mask)` and `where(mask, v)`; a masked-final-chunk
load/store keeps *everything* vectorized. This matters most for the
`reduce`/`dot` kernels above (many-input tails).

**Implementation** — the tail is loaded/stored *masked*, so no scalar loop at
all (uses the `fixed_size<N>` ABI from entry 5)

```cpp
template <class T, size_t N, class F>
void map_inplace_fixed(std::vector<T>& v, F f) {
  using Vec = std::experimental::simd<
      T, std::experimental::simd_abi::fixed_size<N>>;
  using Mask = std::experimental::simd_mask<
      T, std::experimental::simd_abi::fixed_size<N>>;
  size_t i = 0;
  for (; i + N <= v.size(); i += N) {
    Vec x;
    x.copy_from(&v[i], std::experimental::element_aligned);
    f(x).copy_to(&v[i], std::experimental::element_aligned);
  }
  size_t tail = v.size() - i;
  if (tail == 0)
    return;
  Mask m([&](size_t k) { return k < tail; });
  Vec x;
  x.copy_from(&v[i], std::experimental::element_aligned, m);
  Vec y = f(x);
  std::experimental::where(m, y)
      .copy_to(&v[i], std::experimental::element_aligned);
}
```

libstdc++ supports the generator-lambda `simd_mask` ctor, `copy_from(ptr,
flags, mask)` and `where(mask, v).copy_to(...)`.

## 5. Fixed-width aliases [P1]

```
template <class T, size_t N> using simd = std::experimental::simd<T, fixed_size<N>>;
using vec4f = simd<float, 4>; using vec8f = simd<float, 8>; using vec16f = simd<float, 16>; ...
```

**Why:** `native_simd<T>` changes width per architecture, which makes results
width-dependent (sum order, cache behavior) and makes masked-tail code
impossible to test portably. Fixed-width `simd<T, N>` gives deterministic
behavior and testable tails across machines. Keep `vec<T>` as the "native"
alias for the common case.

**Implementation** — `native_simd<T>` takes only `T` (the ABI is implicit);
fixed widths need the `fixed_size<N>` ABI tag explicitly

```cpp
template <class T, size_t N>
using simd = std::experimental::simd<
    T, std::experimental::simd_abi::fixed_size<N>>;
template <class T, size_t N>
using simd_mask = std::experimental::simd_mask<
    T, std::experimental::simd_abi::fixed_size<N>>;

using vec2f = simd<float, 2>;
using vec4f = simd<float, 4>;
using vec8f = simd<float, 8>;
using vec16f = simd<float, 16>;
using vec2d = simd<double, 2>;
using vec4d = simd<double, 4>;
using vec8d = simd<double, 8>;
```

## 6. Math function overloads (sqrt, sin, cos, exp, log, floor, fabs) [P1]

```
// via std::experimental::sqrt(x), sin(x), ... on vec<T> — wrap in helpers:
template <class T> void map_sqrt(std::vector<T>& v);
// or document that f = std::experimental::sqrt(t) already satisfies map_inplace's F
```

**Why:** Element-wise math is *the* reason people reach for SIMD (audio DSP,
physics, ML activations). libstdc++ ships the overloads; the library
currently exposes none of them, so users don't even know
`map_inplace(v, [](auto x) { return std::experimental::sqrt(x); })` compiles.
Named wrappers (`map_sqrt`, `map_exp`, ...) make the capability discoverable.

**Implementation** — one generic helper + named wrappers for discoverability

```cpp
template <class T, class Math>
void map_math(std::vector<T>& v, Math m) {
  map_inplace(v, m);
}

template <class T>
void map_sqrt(std::vector<T>& v) {
  map_inplace(v, [](fp::vec<T> x) { return std::experimental::sqrt(x); });
}

template <class T>
void map_exp(std::vector<T>& v) {
  map_inplace(v, [](fp::vec<T> x) { return std::experimental::exp(x); });
}

// map_log, map_sin, map_cos, map_floor, map_fabs — same shape
```

## 7. `clamp` / `normalize` helpers [P2]

```
template <class T> void clamp_inplace(std::vector<T>& v, T lo, T hi);   // lane min/max
template <class T> void normalize(std::vector<T>& v);   // divide by reduce max
```

**Why:** The two most common scalar post-processing kernels (samples,
weights, image data) become one-liners on top of `reduce` + lane min/max.

**Implementation** — `clamp_inplace` is fully vectorized; `normalize` uses a
scalar pre-pass for the max (swap in `reduce` once it lands)

```cpp
template <class T>
void clamp_inplace(std::vector<T>& v, T lo, T hi) {
  using Vec = fp::vec<T>;
  map_inplace(v, [=](Vec x) {
    return std::experimental::min(std::experimental::max(x, Vec(lo)), Vec(hi));
  });
}

template <class T>
void normalize(std::vector<T>& v) {
  T m = 0;
  for (T x : v)
    m = std::max(m, std::abs(x));
  if (m == T{})
    return;
  using Vec = fp::vec<T>;
  map_inplace(v, [=](Vec x) { return x / Vec(m); });
}
```

## 8. `par_map_inplace` — SIMD across cores [P2]

```
template <class T, class F>
void par_map_inplace(ThreadPool& pool, std::vector<T>& v, F f);
```

**Why:** The natural convergence of the ThreadPool (concurrent.md #1) and
this module: wide *and* deep parallelism. This is the headline feature of
"serious SIMD" libraries (bandwidth-bound workloads scale with cores, not
just width). P2 — requires `ThreadPool` and the `span` support (entry 9).

**Implementation** — chunk, transform each slice, write back (use the
`std::span` slice variant from entry 9 to avoid the copy)

```cpp
template <class T, class F>
void par_map_inplace(ThreadPool& pool, std::vector<T>& v, F f) {
  size_t threads = std::min(pool.size(), v.size());
  if (threads <= 1) {
    map_inplace(v, f);
    return;
  }
  size_t chunk = (v.size() + threads - 1) / threads;
  std::vector<std::future<void>> futs;
  for (size_t s = 0; s < v.size(); s += chunk) {
    size_t e = std::min(v.size(), s + chunk);
    futs.push_back(pool.enqueue([&v, &f, s, e] {
      std::vector<T> part(v.begin() + s, v.begin() + e);
      map_inplace(part, f);
      std::copy(part.begin(), part.end(), v.begin() + s);
    }));
  }
  for (auto& fut : futs)
    fut.get();
}
```

## 9. `std::span` support [P1]

```
template <class T, class F> void map_inplace(std::span<T> data, F f);
template <class T, class F> auto map_to(std::span<T>, F f);
```

**Why:** SIMD code is overwhelmingly used on buffers that are *not* owners
(shared memory, frame buffers, `mmap`'d files); `std::vector&` excludes
them. `span` variants make the module applicable to the actual use cases and
unlock `par_map_inplace`'s chunking over a single span.

**Implementation** — needs `#include <span>`; same tail semantics as the vector version

```cpp
template <class T, class F>
void map_inplace(std::span<T> data, F f) {
  using Vec = fp::vec<T>;
  constexpr size_t lanes = Vec::size();
  size_t i = 0;
  for (; i + lanes <= data.size(); i += lanes) {
    Vec x;
    x.copy_from(data.data() + i, std::experimental::element_aligned);
    auto y = f(x);
    y.copy_to(data.data() + i, std::experimental::element_aligned);
  }
  for (; i < data.size(); ++i)
    data[i] = f(Vec(data[i]))[0];
}
```

`map_to(std::span, F)` = the vector version with `src.size()` replaced by
`src.size()` on the span — same code shape.

## 10. Lane-wise `where`/select helpers [P2]

```
// document/export std::experimental::where(mask, a, b) usage via helpers
template <class T> void select_inplace(std::vector<T>& v, F pred, T then_val, T else_val);
```

**Why:** Ternary vector ops ("keep values above threshold; zero the rest") are
the idiomatic SIMD conditional and read better than `map` over `if`s.

**Implementation** — representative `where`-based helper: threshold-and-replace

```cpp
template <class T>
void threshold_inplace(std::vector<T>& v, T lo, T hi, T replace) {
  using Vec = fp::vec<T>;
  map_inplace(v, [=](Vec x) {
    auto mask = (x < Vec(lo)) || (x > Vec(hi));
    return std::experimental::where(mask, Vec(replace), x);
  });
}
```

## 11. `gather` / `scatter` [P2]

```
template <class T> std::vector<T> gather(std::vector<T> const&, std::vector<size_t> const& idx);
```

**Why:** Indexed reads (lookup tables, permutation) are common in image and
simulation code. Implementations vary per backend — document portability
limits.

**Implementation** — scalar reference (correct everywhere); getters via the
indexed `simd` load are backend-specific

```cpp
template <class T>
std::vector<T> gather(std::vector<T> const& v, std::vector<size_t> const& idx) {
  std::vector<T> out;
  out.reserve(idx.size());
  for (size_t i : idx)
    out.push_back(v.at(i));
  return out;
}
```

(`scatter(v, idx, vals)` is the mirror — write back through the same
index vector.)

---

## Design decisions (resolved)

1. **Alignment — `element_aligned` stays the documented default.** No silent
   flag changes in `map_inplace`/`map_to`/`reduce`/`dot`; `std::vector` and
   `std::span` allocation is not guaranteed over-aligned, so
   `vector_aligned` would be a contract lie for the existing overloads.
   Aligned-allocator support (`vector_aligned`) may be added later as an
   explicit opt-in surface (e.g. `map_inplace_aligned`), only if a bench
   shows a real win. Document this in the README.
2. **Tail policy — scalar tail for `native_simd`, masked for fixed widths.**
   `map_inplace` keeps its scalar tail (stable signature, correct
   everywhere). Masked tails are provided by `map_inplace_fixed` (entry 4),
   which is inherently fixed-width; no policy parameter on the native path —
   the width-dependent tail behavior would be untestable.
3. **Math overloads — thin named wrappers in `simd.hpp`.** `map_sqrt`,
   `map_exp`, `map_log`, `map_sin`, `map_cos`, `map_floor`, `map_fabs`
   (entry 6). Discoverable, tab-completable, and unit-testable; pure
   documentation of `std::experimental::` overloads is zero-maintenance but
   invisible to users who don't know the extension exists.
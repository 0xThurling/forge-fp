# Iteration & in-place algorithms — `inplace.hpp`

The eager combinators in `vec.hpp`/`ranges.hpp` are **pure**: `map`, `filter`,
`sort_by` always return a *fresh* collection. That is what makes pipelines safe
and composable — but it is also an allocation and a copy on every call.

`inplace.hpp` is the other half of the story: the same jobs expressed as
**plain loops over an existing container**, with no allocation, no
`std::function`, and no bounds checks. Every function here is a loop the
compiler can inline and vectorize exactly as if you had written it by hand.

```cpp
#include <fp/inplace.hpp>
```

**Why a separate header:** the naming convention is the contract. `map` returns
a new vector; `transform_inplace` mutates. `sort_by` returns a sorted copy;
`sort_by_inplace` sorts where it lives. You can read a pipeline and know where
memory moves without opening a header.

## The mental model

Two ways to describe the same computation:

```text
pure:      v2 = fp::map(v, f)              // v untouched, v2 is new
in-place:  fp::transform_inplace(v, f)     // v is updated, nothing allocated
```

Use pure combinators while shaping data (you can re-use the input, branch, and
compose freely). Switch to `*_inplace` when the data is large, the transform is
hot, and the old values are dead anyway — frame buffers, optimizer updates,
rasterizer spans, token buffers.

Rule of thumb:

| Situation | Use |
|---|---|
| The result feeds two different branches | eager `fp::map`/`fp::filter` |
| The input is dead after the step | `*_inplace` |
| The step runs per frame / per batch / per token | `*_inplace` |
| You want a lazy, zero-copy view | `fp::views` (see [collections](collections.md)) |
| The container is a wrapper (`Result`, `optional`) | `fp::map` — wrappers are values |

## Iteration

```cpp
std::vector<int> v = {1, 2, 3};

int sum = 0;
fp::for_each(v, [&](int x) { sum += x; });              // 6

fp::for_each_index(v, [&](std::size_t i, int x) {
  v[i] = static_cast<int>(i) * x;                        // indexed access
});

fp::transform_inplace(v, [](int x) { return x + 1; });   // v = {2, 4, 7}

fp::fill(v, 0);                                          // v = {0, 0, 0}
```

`f` receives the element by reference, so mutating it is allowed:

```cpp
fp::for_each(v, [](int &x) { x *= 2; });
```

Pairwise and three-way iteration truncate to the shorter input, exactly like
`fp::zip`:

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {10, 20};

fp::zip_for_each(a, b, [](int x, int y) { /* (1,10), (2,20) */ });

std::vector<int> c = {100, 200, 300};
fp::zip3_for_each(a, b, c, [](int x, int y, int z) { /* two triples */ });
```

## Writing into a destination

`map_to` is `map` without the result allocation: you supply the destination.

```cpp
const std::vector<double> src = {1.0, 2.0, 3.0};
std::vector<double> dst(3);

fp::map_to(src, dst, [](double x) { return x * x; });   // dst = {1, 4, 9}
```

The common hot-loop shape is a fused update — read two inputs, write one back:

```cpp
// w -= lr * g
fp::zip_transform_inplace(w, g, [lr](double wi, double gi) {
  return wi - lr * gi;
});
```

That is the entire optimizer step for a dense parameter vector: one loop, no
temporaries, no allocations.

## In-place algorithms

```cpp
std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6};

fp::sort_inplace(v);                       // {1, 1, 2, 3, 4, 5, 6, 9}
fp::reverse_inplace(v);                    // {9, 6, 5, 4, 3, 2, 1, 1}
fp::unique_inplace(v);                     // {9, 6, 5, 4, 3, 2, 1}   (adjacent dedup)
fp::remove_if_inplace(v, [](int x) { return x % 2 == 0; });  // {9, 5, 3, 1}
```

Sorting by a key (the painter's algorithm, top-k scoring, priority queues):

```cpp
struct Triangle { float depth; int id; };
std::vector<Triangle> tris = /* ... */;

fp::sort_by_inplace(tris, [](Triangle const &t) { return -t.depth; });

// Keep only the best 10 without copying:
tris.resize(std::min<std::size_t>(tris.size(), 10));
```

`stable_sort_by_inplace` uses `std::stable_sort` when equal keys must keep
their input order (e.g. deterministic tie-breaking in reports). The default
`sort_by_inplace` is the faster unstable sort.

`sort_by_cached_inplace` computes each key once into a scratch buffer and sorts
that, then moves the elements back — one allocation, and measured ~1.4x faster
than `sort_by_inplace` when the key allocates (a string built per comparison),
at the cost of ~1.9x slower for a plain integer key. Keep `sort_by_inplace` for
cheap keys.

`unique_inplace` and `remove_if_inplace` shrink the container, so they are
constrained to **erasable ranges** — containers with an `erase(first, last)`
member (`std::vector`, `std::string`, `std::deque`). A raw array or a
`std::span` has no capacity to shrink, so those calls don't compile.

```cpp
// std::span: no erase -> use the eager fp::filter instead
auto kept = fp::filter(v, pred);
```

## Eager vs in-place at a glance

| Job | Pure (allocates) | In-place (no allocation) |
|---|---|---|
| iterate | `for (auto&& x : v)` | `fp::for_each(v, f)` |
| indexed iterate | `for (i)` | `fp::for_each_index(v, f)` |
| transform | `fp::map(v, f)` | `fp::transform_inplace(v, f)` |
| transform into a buffer | `auto out = fp::map(...)` | `fp::map_to(v, out, f)` |
| pairwise | `fp::zip_with(a, b, f)` | `fp::zip_for_each(a, b, f)` / `fp::zip_transform_inplace(a, b, f)` |
| sort | `fp::sort(v)` / `fp::sort_by(v, k)` / `fp::sort_by_cached(v, k)` | `fp::sort_inplace(v)` / `fp::sort_by_inplace(v, k)` / `fp::sort_by_cached_inplace(v, k)` |
| reverse | `fp::reverse(v)` | `fp::reverse_inplace(v)` |
| dedup | `fp::unique(v)` | `fp::unique_inplace(v)` |
| filter | `fp::filter(v, p)` | `fp::remove_if_inplace(v, !p)` |

## Worked examples

### 1. A frame loop that touches every vertex

```cpp
std::vector<Vertex> verts = mesh.vertices;   // reused every frame

fp::for_each(verts, [&](Vertex &v) {
  v.clip = project(v.world);
});

fp::transform_inplace(verts, [](Vertex v) {
  v.depth = (v.clip.x + v.clip.y + v.clip.z) / 3.0f;
  return v;
});
```

No per-frame allocations, no temporary vectors, and the loop body is the same
code you would have written with `for (auto &v : verts)`.

### 2. A training step

```cpp
void sgd_step(std::vector<double> &w, std::vector<double> const &grad,
              double lr, double momentum, std::vector<double> &velocity) {
  fp::zip3_for_each(w, grad, velocity, [&](double &wi, double gi, double &vi) {
    vi = momentum * vi + gi;
    wi -= lr * vi;
  });
}
```

Three arrays, one pass, zero allocations — and the whole update rule reads on
one line.

### 3. Reusing a scratch buffer

```cpp
std::vector<double> scratch(n);

for (auto const &batch : batches) {
  fp::map_to(batch, scratch, activate);          // scratch = activate(batch)
  fp::transform_inplace(scratch, normalize);
  accumulate(scratch);
}
```

The allocation happened once; every batch reuses the same memory.

### 4. In-place filtering of log lines

```cpp
std::vector<std::string> lines = fp::read_lines(path).value();

fp::remove_if_inplace(lines, [](std::string const &line) {
  return line.empty() || line[0] == '#';
});
```

## Performance contract

Every function in this header:

- **allocates nothing** — the destination is yours;
- **stores no `std::function`** — `f` is a template parameter and inlines;
- **has no release-mode bounds checks**;
- **compiles to the same code as the hand-written loop** (verified in
  `bench/inplace_bench.cpp`).

Measured on 4M doubles (`scripts/run_bench.sh`):

| Task | Hand loop | fp |
|---|---|---|
| sum | 3.19 ms | `fp::for_each` 3.20 ms |
| map | 3.37 ms | `fp::transform_inplace` 3.28 ms |
| sort | 39.36 ms | `fp::sort_inplace` 40.22 ms |
| allocate + fill | 19.46 ms (`std::vector`) | `fp::Buffer` 19.22 ms |

The differences are within run-to-run noise.

## Gotchas

- **Truncation, not error.** `zip_for_each`, `zip3_for_each`, and `map_to`
  stop at the shorter input, matching `fp::zip`. If equal lengths are a
  precondition, assert it at your boundary.
- **Views borrow.** If a `fp::views` pipeline holds a reference to `v`,
  mutating `v` through `*_inplace` changes what the view sees (and shrinking
  `v` can dangle it). Materialize with `fp::to_vector` first if in doubt.
- **Erase invalidates iterators.** Don't keep an iterator into `v` across
  `remove_if_inplace`/`unique_inplace`.
- **Stability costs.** Use `stable_sort_by_inplace` only when tie order is
  observable; otherwise the default sort is faster.
- **Purity matters when you branch.** If two later steps need the original
  values, keep the eager combinator and pay for the copy.
- **`fill` writes the same value to every element.** For `Buffer` (raw,
  uninitialized memory) this is also how you initialize it — see
  [memory](memory.md).

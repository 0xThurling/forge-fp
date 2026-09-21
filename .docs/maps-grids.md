# Maps & grids — `map.hpp` and `grid.hpp`

## `map.hpp` — associative-container combinators

Helpers for consuming and transforming associative containers. They are
generic over the map type, so they work with both `std::map` and
`std::unordered_map`. `group_by` produces an `unordered_map`; these let you
consume one.

```cpp
#include <fp/map.hpp>

std::map<std::string, int> m = {{"a", 1}, {"b", 2}, {"c", 3}};
std::unordered_map<std::string, int> u = {{"a", 1}};
```

| Function | Result |
|---|---|
| `lookup(m, k)` | `optional<V>` — value or `nullopt` |
| `map_values(m, f)` | new map of the **same kind** with `f(v)` applied to each value |
| `filter_values(m, pred)` | keep entries where `pred(v)` |
| `merge_with(a, b, combine)` | union; `combine(a_v, b_v)` resolves collisions |
| `keys(m)` / `values(m)` | `vector<K>` / `vector<V>` |
| `to_map(vector<pair<K,V>>)` | build a `std::map` from pairs |
| `to_unordered_map(vector<pair<K,V>>)` | build a `std::unordered_map` from pairs |

```cpp
fp::lookup(m, std::string("b"));                       // optional{2}
fp::lookup(m, std::string("z"));                       // nullopt

fp::map_values(m, [](int v) { return v * 10; });       // {"a":10,"b":20,"c":30}
fp::filter_values(m, [](int v) { return v % 2 == 1; });       // {"a":1,"c":3}

std::map<std::string, int> n = {{"c", 30}, {"d", 4}};
fp::merge_with(m, n, [](int a, int b) { return a + b; }); // c -> 33, d -> 4

fp::keys(m);    // {"a","b","c"}
fp::values(m);  // {1,2,3}

auto remap = fp::to_map<std::string, int>({{"x", 9}, {"y", 8}});
```

**Why these exist:** the library already *produces* maps (`group_by`), so
without these, that output is a dead end — you'd hand-roll a `find`/`emplace`
loop to consume it. Each helper is that loop, named: `lookup` is the safe
`find`, `merge_with` is the collision-handling union, `keys`/`values` are the
common projections.

### A full pipeline

```cpp
// word frequency -> only the frequent words -> sorted
auto counts = fp::group_by(words, [](std::string const &w) { return w; });

auto frequent = fp::filter_values(counts, [](std::vector<std::string> const &v) {
  return v.size() >= 3;
});

auto names = fp::keys(frequent);
fp::sort_inplace(names);
```

### Notes and gotchas

- `lookup` takes the key type exactly (`K const&`); pass a `std::string` for
  `std::string` keys (a `const char*` literal won't deduce).
- `map_values`/`filter_values` return the **same container type**, so a
  `std::map` stays ordered and an `unordered_map` stays hashed.
- `merge_with` copies `a` and folds `b` into it; `combine` is called only on
  collisions.
- There is no `insert_or_assign` helper — `map_values`/`merge_with` cover the
  transform cases, and a plain `m[k] = v` covers the rest.

## `grid.hpp` — 2D/3D grids and index space

Grids are `std::vector<std::vector<T>>` (row-major) — no new container, so they
compose with every `vec.hpp` combinator, and a `Matrix` in your domain layer is
just a named grid.

```cpp
#include <fp/grid.hpp>

std::vector<std::vector<int>> g = {{1, 2, 3}, {4, 5, 6}};
```

| Function | Result |
|---|---|
| `map2d(g, f)` | elementwise map, preserving shape |
| `map2d_indexed(g, f)` | `f(i, j, x)` — index-aware elementwise map |
| `map2d_inplace(g, f)` | elementwise map, mutating `g` |
| `map3d(g, f)` | same, one more axis |
| `transpose(g)` | swap rows/columns |
| `flatten(g)` | `vector<vector<T>>` → `vector<T>` |
| `column(g, j)` | column `j` as a vector (copy; columns are strided) |
| `windows2d(g, kh, kw)` | non-overlapping `kh x kw` patches |
| `for_each_index(rows, cols, f)` | call `f(i, j)` for every cell |
| `tabulate(n, f)` | `{f(0), f(1), …, f(n-1)}` |
| `cartesian_product(as, bs)` | all `(a, b)` pairs |

```cpp
fp::map2d(g, fp::plus(1));            // {{2,3,4},{5,6,7}}
fp::transpose(g);                     // {{1,4},{2,5},{3,6}}
fp::flatten(g);                       // {1,2,3,4,5,6}

fp::for_each_index(2, 3, [](size_t i, size_t j) { /* visit g[i][j] */ });

fp::tabulate(5, [](size_t i) { return i * i; });        // {0,1,4,9,16}

fp::cartesian_product(std::vector<int>{1,2}, std::vector<char>{'a','b'});
// {{1,'a'},{1,'b'},{2,'a'},{2,'b'}}
```

### Index-aware mapping

`map2d` gives you the value; `map2d_indexed` gives you the position too. That is
what bias adds, positional masks, distance fields, and index features need:

```cpp
auto biased = fp::map2d_indexed(g, [](std::size_t i, std::size_t j, int x) {
  return x + static_cast<int>(i * 10 + j);
});
```

### In-place and column access

```cpp
fp::map2d_inplace(g, [](int x) { return x * 2; });   // no copy

auto second = fp::column(g, 1);                      // {g[0][1], g[1][1], …}
```

`column` is one pass and no transpose; use it (or the `col_sums`/`col_means`
reductions in [linalg](linalg.md)) instead of materializing a transposed grid.

### Patches for convolution

`windows2d` slices a grid into non-overlapping `kh x kw` patches in row-major
order — the patch primitive behind a from-scratch convolution:

```cpp
std::vector<std::vector<int>> image = {
    {1, 2, 3},
    {4, 5, 6},
    {7, 8, 9},
};

auto patches = fp::windows2d(image, 2, 2);
// one patch: {{1,2},{4,5}}  (the 3x3 grid has no room for a second 2x2 block)
```

For overlapping windows with a stride, walk the top-left corner yourself with
`for_each_index` and copy the `kh x kw` block — `windows2d` is deliberately the
simple, non-overlapping case.

### Worked example: a 3x3 box blur

```cpp
std::vector<std::vector<double>> blur(std::vector<std::vector<double>> const &src) {
  const std::size_t rows = src.size(), cols = src[0].size();
  std::vector<std::vector<double>> out(rows, std::vector<double>(cols, 0.0));

  fp::for_each_index(rows, cols, [&](std::size_t i, std::size_t j) {
    double sum = 0.0;
    int count = 0;
    for (int di = -1; di <= 1; ++di) {
      for (int dj = -1; dj <= 1; ++dj) {
        const auto r = static_cast<std::ptrdiff_t>(i) + di;
        const auto c = static_cast<std::ptrdiff_t>(j) + dj;
        if (r < 0 || c < 0 || r >= static_cast<std::ptrdiff_t>(rows) ||
            c >= static_cast<std::ptrdiff_t>(cols))
          continue;
        sum += src[r][c];
        ++count;
      }
    }
    out[i][j] = sum / count;
  });
  return out;
}
```

The nested loops are the kernel; `for_each_index` owns the outer traversal and
keeps the bounds handling in one place.

**Why grids matter:** index-space work (render passes, image kernels, matrix
transpose) is nested `for (i) for (j)` loops — the one shape `vec.hpp`/`ranges.hpp`
(which are flat) don't cover. `map2d`/`transpose`/`for_each_index` turn those
nested loops into one call; `flatten` hands the result back to the flat
combinators. `tabulate` + `cartesian_product` are the declarative
nested-loop makers, and `map2d_indexed`/`windows2d` cover the two cases that
need the *position* rather than just the value.

For in-place row-wise math (softmax over logits, normalization), see
[`softmax_rows`](numerics.md); for column-wise reductions without a transpose,
see [`linalg`](linalg.md#reductions).

### Gotchas

- **Grids are not ragged-aware.** `transpose`, `column`, and `windows2d`
  assume every row has the same length; a ragged grid is a bug in your data,
  not something fp repairs.
- **`transpose` copies.** For a one-off column reduction, use `column` or
  `col_sums` instead.
- **`windows2d` drops incomplete edges** (no padding). Add padding to the grid
  first if you need every cell covered.
- **`for_each_index` takes `(rows, cols)`**, not a grid — it works for
  index-space algorithms that compute values from the indices alone.
- **`map2d_inplace` mutates; `map2d` copies.** The name tells you which.

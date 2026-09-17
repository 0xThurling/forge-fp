# Maps & grids — `map.hpp` and `grid.hpp`

## `map.hpp` — associative-container combinators

Helpers for consuming and transforming `std::map<K, V>`. `vec.hpp`'s
`group_by` produces an `unordered_map`; these let you consume one.

```cpp
#include <fp/map.hpp>

std::map<std::string, int> m = {{"a", 1}, {"b", 2}, {"c", 3}};
```

| Function | Result |
|---|---|
| `lookup(m, k)` | `optional<V>` — value or `nullopt` |
| `map_values(m, f)` | new map with `f(v)` applied to each value |
| `filter(m, pred)` | keep entries where `pred(v)` |
| `merge_with(a, b, combine)` | union; `combine(a_v, b_v)` resolves collisions |
| `keys(m)` / `values(m)` | `vector<K>` / `vector<V>` |
| `to_map(vector<pair<K,V>>)` | build a map from pairs |

```cpp
fp::lookup(m, std::string("b"));                       // optional{2}
fp::lookup(m, std::string("z"));                       // nullopt

fp::map_values(m, [](int v) { return v * 10; });       // {"a":10,"b":20,"c":30}
fp::filter(m, [](int v) { return v % 2 == 1; });       // {"a":1,"c":3}

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

Notes:

- `lookup` takes the key type exactly (`K const&`); pass a `std::string` for
  `std::string` keys (a `const char*` literal won't deduce).
- These work on `std::map`; the same signatures apply conceptually to
  `std::unordered_map` (add overloads if you need them).

## `grid.hpp` — 2D/3D grids and index space

Grids are `std::vector<std::vector<T>>` (row-major) — no new container, so they
compose with every `vec.hpp` combinator.

```cpp
#include <fp/grid.hpp>

std::vector<std::vector<int>> g = {{1, 2, 3}, {4, 5, 6}};
```

| Function | Result |
|---|---|
| `map2d(g, f)` | elementwise map, preserving shape |
| `map3d(g, f)` | same, one more axis |
| `transpose(g)` | swap rows/columns |
| `flatten(g)` | `vector<vector<T>>` → `vector<T>` |
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

**Why grids matter:** index-space work (render passes, image kernels, matrix
transpose) is nested `for (i) for (j)` loops — the one shape `vec.hpp`/`ranges.hpp`
(which are flat) don't cover. `map2d`/`transpose`/`for_each_index` turn those
nested loops into one call; `flatten` hands the result back to the flat
combinators. `tabulate` + `cartesian_product` are the declarative
nested-loop makers.

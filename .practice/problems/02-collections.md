# 02 — Collections

**Headers:** `<fp/vec.hpp>`, `<fp/ranges.hpp>`, `<fp/grid.hpp>`, `<fp/ops.hpp>`.

## What this module is about

Beyond `map`/`filter`, real data is *structured*: it comes as pairs (`zip`),
groups (`group_by`), chunks (`chunk`), running states (`scan`), and grids
(`map2d`/`transpose`). These combinators replace the nested loops and
`unordered_map` bookkeeping that structure usually forces you to write by
hand. The theme here is **composition**: each problem is two or three
combinators threaded together.

---

### 1. Pairwise sums · Easy

Element-wise add `{1,2,3}` and `{10,20,30}` → `{11,22,33}`.

**Why `zip_with`:** "apply a binary function to corresponding elements" is the
*zip* pattern. `zip_with(a, b, f)` bundles the pairing and the applying into
one call, so you never touch two indices in parallel (the classic off-by-one
trap).

```cpp
assert(fp::zip_with(std::vector<int>{1,2,3}, std::vector<int>{10,20,30},
                    [](int a, int b){ return a+b; })
       == std::vector<int>({11,22,33}));
```

### 2. Group words by length · Easy

Group `{"a","bb","cc","ddd"}` by string length.

**Why `group_by`:** "bucket elements by a key" is a fold into a map that you'd
otherwise write as an `unordered_map<Key, vector<T>>` loop. `group_by(v, key_fn)`
is that loop, named — you supply only the key function.

```cpp
auto g = fp::group_by(std::vector<std::string>{"a","bb","cc","ddd"},
                      [](std::string const& s){ return s.size(); });
assert(g.at(1) == std::vector<std::string>({"a"}));
assert(g.at(2) == std::vector<std::string>({"bb","cc"}));
```

### 3. Running total · Easy

Prefix sums of `{1,2,3,4}` → `{1,3,6,10}`.

**Why `scan`:** `fold_left` gives only the final accumulator; `scan` gives
*every intermediate state*. When you need the trajectory (a running total, a
filter's state, a cumulative distribution), `scan` is `fold_left` that doesn't
throw away the journey.

```cpp
assert(fp::scan(std::vector<int>{1,2,3,4}, 0, [](int a,int b){ return a+b; })
       == std::vector<int>({1,3,6,10}));
```

### 4. Chunk into pairs · Easy

Split `{1,2,3,4,5}` into `{{1,2},{3,4},{5}}`.

**Why `chunk`:** fixed-size batching is an index-arithmetic loop (`i += size`)
that's easy to get subtly wrong at the boundary. `chunk(v, n)` encodes the
"full slices then a possibly-short tail" rule once and for all.

```cpp
assert(fp::chunk(std::vector<int>{1,2,3,4,5}, 2)
       == std::vector<std::vector<int>>({{1,2},{3,4},{5}}));
```

### 5. Split evens and odds · Easy

Partition `{1,2,3,4,5}` into `pair{{2,4},{1,3,5}}`.

**Why `partition`:** "split into the ones that pass and the ones that don't" is
a two-bucket pass you'd write with two `push_back`s. `partition` returns the
two buckets as a `pair` in one traversal — and structured bindings
(`auto [a, b] = ...`) make the call site read cleanly.

```cpp
auto [evens, odds] = fp::partition(std::vector<int>{1,2,3,4,5},
                                   [](int x){ return x%2==0; });
assert(evens == std::vector<int>({2,4}));
```

### 6. Flatten a list of lists · Easy

`{{1,2},{3},{4,5}}` → `{1,2,3,4,5}`.

**Why `concat`:** collapsing nested vectors is the *flatten* operation.
`concat(nested)` is the library name for it (the grid module's `flatten` does
the same for `vector<vector<T>>`). One call instead of two nested loops.

```cpp
assert(fp::concat(std::vector<std::vector<int>>{{1,2},{3},{4,5}})
       == std::vector<int>({1,2,3,4,5}));
```

### 7. Words per sentence · Medium

Count the total words across `{"a b", "c d e"}` (5).

**Why `flat_map`:** you have a *mapping that produces a collection per element*
(`split`), and you want the results stitched together. `flat_map` = `map` then
flatten in one step — the two-idea pipeline in a single combinator.

```cpp
auto words = fp::flat_map(std::vector<std::string>{"a b","c d e"},
                          [](std::string const& s){ return fp::str::split(s, ' '); });
assert(words.size() == 5);
```

### 8. Transpose a matrix · Medium

`{{1,2,3},{4,5,6}}` → `{{1,4},{2,5},{3,6}}`.

**Why `fp::transpose` (grid.hpp):** swapping rows and columns is the
double-indexed `out[j][i] = g[i][j]` loop that's prime real estate for typos.
A named `transpose` says exactly what it does and handles the rectangular-shape
bookkeeping for you.

```cpp
assert(fp::transpose(std::vector<std::vector<int>>{{1,2,3},{4,5,6}})
       == std::vector<std::vector<int>>({{1,4},{2,5},{3,6}}));
```

### 9. Sum of a flattened grid · Medium

Sum `{{1,2},{3,4}}` (10) via `flatten` + `sum`.

**Why flatten-then-reduce:** a 2D structure is only "2D" while you need the
shape; to *consume* it, drop to 1D. `flatten` (grid) + `sum` is the idiomatic
"reduce a grid" — no nested loop, no double accumulator.

```cpp
assert(fp::sum(fp::flatten(std::vector<std::vector<int>>{{1,2},{3,4}})) == 10);
```

### 10. Enumerate and filter indices · Medium

Keep `{"x","y","z"}` at even indices: `{{0,"x"},{2,"z"}}`.

**Why `enumerate` + `filter`:** you often need the *position* alongside the
value. `enumerate` attaches indices as pairs, then ordinary `filter` selects by
index. It's two ideas (indexing, filtering) composed — each stays simple.

```cpp
auto pairs = fp::enumerate(std::vector<std::string>{"x","y","z"});
auto even = fp::filter(pairs, [](auto const& p){ return p.first % 2 == 0; });
assert(even.size() == 2 && even[0].second == "x" && even[1].second == "z");
```

---

## Solutions

<details>
<summary>1. Pairwise sums</summary>

```cpp
auto out = fp::zip_with(a, b, [](int x, int y) { return x + y; });
// or point-free: fp::zip_with(a, b, fp::plus);
```
</details>

<details>
<summary>2. Group words by length</summary>

```cpp
auto g = fp::group_by(words, [](std::string const& s) { return s.size(); });
```
</details>

<details>
<summary>3. Running total</summary>

```cpp
auto out = fp::scan(v, 0, fp::plus);
```
</details>

<details>
<summary>4. Chunk into pairs</summary>

```cpp
auto out = fp::chunk(v, 2);
```
</details>

<details>
<summary>5. Split evens and odds</summary>

```cpp
auto [evens, odds] = fp::partition(v, [](int x) { return x % 2 == 0; });
```
</details>

<details>
<summary>6. Flatten a list of lists</summary>

```cpp
auto out = fp::concat(nested);
```
</details>

<details>
<summary>7. Words per sentence</summary>

```cpp
auto words = fp::flat_map(sentences, [](std::string const& s) {
    return fp::str::split(s, ' ');
});
size_t count = words.size();
```
</details>

<details>
<summary>8. Transpose a matrix</summary>

```cpp
auto t = fp::transpose(m);
```
</details>

<details>
<summary>9. Sum of a flattened grid</summary>

```cpp
int total = fp::sum(fp::flatten(grid));
```
</details>

<details>
<summary>10. Enumerate and filter indices</summary>

```cpp
auto even = fp::filter(fp::enumerate(vs),
                       [](auto const& p) { return p.first % 2 == 0; });
```
</details>

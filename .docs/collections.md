# Collections — `vec.hpp` and `ranges.hpp`

The collection layer transforms, filters, folds, and zips sequences. Most
functions take `std::vector<T> const&` (`vec.hpp`); `ranges.hpp` provides the
same names (and a few extras) generically over any range — arrays, `std::span`,
views, `std::list`, etc.

```cpp
#include <fp/all.hpp>

std::vector<int> v = {1, 2, 3, 4};
auto doubled = fp::map(v, [](int x) { return x * 2; });   // {2,4,6,8}
```

## Two principles behind the API

**1. Pure — you always get a fresh collection.** No combinator mutates its
input (the `*_inplace` SIMD variants are the one documented exception, and they
live in `simd.hpp`). This is what makes pipelines safe: `filter(sort(v), …)`
can't clobber `v`, and the same input can feed several branches.

**2. Collection → collection, not element → element.** You describe *what the
whole collection becomes* (`map`, `filter`, `fold`), never write the loop. The
loop, the index, the `push_back`, and the off-by-ones are the library's job.

## `vec.hpp` vs `ranges.hpp`

- **`vec.hpp`** functions take `std::vector<T> const&`. Use these when you
  hold a vector — the common case.
- **`ranges.hpp`** functions are constrained on `std::ranges::range`, so they
  accept *anything* iterable: raw arrays, `std::span`, `std::views::*` results,
  `std::list`, your own containers.

Because the ranges overloads are constrained, both headers can be included
together (they are, via `all.hpp`) and overload resolution picks the right one
per argument type.

```cpp
#include <fp/ranges.hpp>

int arr[] = {1, 2, 3};
auto v = fp::map(arr, fp::plus(1));    // works on a raw array -> {2,3,4}
auto v2 = fp::to_vector(arr);          // materialize anything to vector
```

**Rule of thumb:** if you hold a `std::vector`, either header works. If you hold
anything else, include `ranges.hpp` and pass it directly.

## Mapping

| Function | Result |
|---|---|
| `map(v, f)` | `vector<invoke_result_t<F,T>>` |
| `flat_map(v, f)` | flatten the per-element sub-ranges into one vector |
| `filter_map(r, f)` *(ranges)* | `f` returns `optional`; keep only the present ones |

```cpp
auto words = fp::flat_map(std::vector<std::string>{"a b", "c"},
                          [](std::string const& s) { return fp::str::split(s, ' '); });
// {"a","b","c"}

// filter_map keeps the elements whose f(x) returns a present optional
auto evens = fp::filter_map(std::vector<int>{1, 2, 3, 4},
                            [](int x) { return x % 2 == 0 ? std::optional<int>{x} : std::nullopt; });
// {2, 4}
```

`map` keeps the shape (same length); `flat_map` changes it (one-to-many);
`filter_map` is `map` + "drop the empties" — the three cover most transforms.

## Filtering and slicing

| Function | Result |
|---|---|
| `filter(v, pred)` | keep elements where `pred(x)` |
| `take_while` / `drop_while` | prefix where `pred` holds / drop that prefix |
| `partition(v, pred)` | `pair{yes, no}` |
| `span(v, pred)` | `pair{take_while, rest}` |
| `take(v, n)` / `drop(v, n)` | first / rest `n` |
| `head` / `tail` / `last` / `init` | first / all-but-first / last / all-but-last |

```cpp
fp::filter(v, [](int x) { return x % 2 == 0; });   // {2,4}
auto [yes, no] = fp::partition(v, [](int x) { return x > 2; });
fp::take(v, 2);                                     // {1,2}
fp::head(v);                                        // optional{1}
fp::last(v);                                        // optional{4}
fp::tail(v);                                        // {2,3,4}
fp::init(v);                                        // {1,2,3}
```

Note the `head`/`last` return `optional` — an empty vector has no first/last
element, and `optional` says so honestly instead of a `-1`/`throw`.

## Folding

```cpp
// fold_left: (init op x1) op x2 ...  — left-associative (ranges.hpp)
int sum = fp::fold_left(v, 0, [](int a, int b) { return a + b; });   // 10

// fold_right: x1 op (x2 op (... op init))  — right-associative (ranges.hpp)
// scan: every intermediate state, not just the final one
auto running = fp::scan(v, 0, [](int a, int b) { return a + b; });    // {1,3,6,10}

// shortcuts
fp::sum(v);        // 10
fp::product(v);    // 24
```

`fold_left` is the general reducer; `sum`/`product` are its common cases; `scan`
is "fold that keeps the journey".

## Predicates

| Function | Result |
|---|---|
| `all(v, pred)` / `any` / `none` | `bool` |
| `contains(v, x)` | `bool` |
| `find(v, pred)` | `optional<size_t>` index |
| `count_if(v, pred)` / `count(r, x)` | `size_t` |

```cpp
fp::all(v, fp::gt(0));              // true
fp::contains(v, 3);                 // true
fp::find(v, [](int x) { return x == 3; });   // optional{2}
fp::count_if(v, [](int x) { return x > 2; }); // 2
```

## Zipping

```cpp
auto z = fp::zip(std::vector<int>{1,2,3}, std::vector<char>{'a','b'}); // {{1,'a'},{2,'b'}}
auto z3 = fp::zip3(as, bs, cs);
auto zw = fp::zip_with(as, bs, [](int a, int b) { return a + b; });
auto zw3 = fp::zip_with3(as, bs, cs, f);

// unzip: pairs back to two vectors
auto [a2, b2] = fp::unzip(z);

// enumerate: attach indices
for (auto& [i, x] : fp::enumerate(v)) { /* i, x */ }
```

`zip` truncates to the shorter input. `enumerate` is "zip with the indices".

## Grouping, sorting, reshaping

```cpp
// group_by: key -> vector of elements with that key
auto by = fp::group_by(std::vector<std::string>{"a","bb","cc"},
                       [](std::string const& s) { return s.size(); });
// {1: {"a"}, 2: {"bb","cc"}}   (std::unordered_map<size_t, vector<string>>)

// chunk: fixed-size windows
fp::chunk(v, 2);        // {{1,2},{3,4}}
// windows: overlapping windows of size n (ranges.hpp)
fp::windows(v, 2);      // {{1,2},{2,3},{3,4}}

// concat / flatten: join nested vectors
fp::concat(std::vector<std::vector<int>>{{1,2},{3}});  // {1,2,3}

// intersperse: insert a separator between elements
fp::intersperse(std::vector<int>{1,2,3}, 0);           // {1,0,2,0,3}
// intercalate: join vectors with a separator vector
fp::intercalate(std::vector<std::vector<int>>{{1},{2}}, std::vector<int>{0}); // {1,0,2}

// sort / sort_by / reverse / unique
fp::sort(std::vector<int>{3,1,2});                     // {1,2,3}
fp::sort_by(std::vector<std::string>{"bb","a"}, [](auto const& s){ return s.size(); });
fp::reverse(v);                                        // {4,3,2,1}
fp::unique(std::vector<int>{1,1,2});                   // {1,2}  (consecutive dedup)

// min/max
fp::maximum(v);   // optional{4}
fp::minimum(v);   // optional{1}
```

## Generation

```cpp
fp::range(0, 5);                 // {0,1,2,3,4}
fp::range(0, 10, 3);             // {0,3,6,9}
fp::replicate(3, std::string("x"));  // {"x","x","x"}
```

## Pairing with `fp::ops`

The named operators (see [Function composition](composition.md)) make these
combinators read like point-free pipelines:

```cpp
fp::filter(v, fp::gt(2));                          // {3,4}
fp::map(v, fp::plus(1));                           // {2,3,4,5}
fp::fold_left(v, 0, fp::plus);                     // 10
fp::map(v, fp::times(2));                          // {2,4,6,8}
```

The point-free form is worth it when the operation is common (`+1`, `>0`);
for anything one-off, a lambda is clearer.

## Point-free pipelines

Every range combinator also has a *curried* form taking fewer arguments and
returning `range -> range`, so you can build a pipeline with `into(…) | …` (see
[Function composition](composition.md)); nothing runs until you `out(…)` it:

```cpp
auto r = fp::out(fp::into(v) | fp::filter(fp::gt(0))
                            | fp::map(fp::times(2))
                            | fp::take(2));
```

The available stages are `map`, `filter`, `filter_map`, `flat_map`,
`take_while`, `drop_while`, `group_by`, `sort_by`, `partition`, `span`,
`take(n)`, `drop(n)`, `chunk(n)`, `zip_with(other, f)`, `fold_left(init, op)`,
`fold_right(init, op)`, `scan(init, op)`, and the zero-argument `unique()`,
`sort()`, `reverse()`, `enumerate()`.

`fp::partition`/`fp::span` split a sequence into a pair; the curried forms work
the same way inside a pipe:

```cpp
auto [evens, odds] = fp::out(fp::into(v) | fp::partition([](int x) { return x % 2 == 0; }));
auto [head, rest] = fp::out(fp::into(v) | fp::span(fp::lt(3)));
```

## Lazy views (`fp::views`)

The `fp::` combinators are **eager** — each stage builds a `std::vector`. When
you want a lazy pipeline (no allocation, single pass, composable with
`std::views`), use `fp::views::` — thin wrappers over the standard view
adaptors:

```cpp
#include <fp/views.hpp>

std::vector<int> v = {1, 2, 3, 4, 5, 6};

auto r = v | fp::views::filter([](int x) { return x % 2 == 0; })
           | fp::views::map([](int x) { return x * 10; });
fp::to_vector(r);    // {20, 40, 60} — nothing ran until here
```

| `fp::views::` | Notes |
|---|---|
| `map(f)`, `filter(p)`, `take(n)`, `drop(n)` | std adaptors; work with `\|` |
| `take_while(p)`, `drop_while(p)` | prefix adaptors |
| `reverse`, `join`, `iota(a, b)` | std adaptors; `iota` is the lazy integer range |
| `filter_map(f)` | `f` returns `optional`; present values kept |
| `flat_map(f)` | map to a range, then flatten |
| `enumerate(r)` / `enumerate()` | index + element (random-access ranges) |
| `zip(a, b)` / `zip(b)` | truncating pair-ups (random-access ranges) |
| `chunk(n)` | non-overlapping groups of `n` (each a subrange view) |
| `slide(n)` | overlapping windows of `n` |
| `stride(n)` | every `n`-th element |
| `take_last(n)` | the last `n` elements |
| `zip_with(other, f)` | fused zip + transform (no pair vector) |
| `zip3(a, b, c)` | three-way zip into tuples |

Views are **lazy and borrowing**:

- nothing runs until you iterate, `fp::to_vector`, or fold;
- a view must not outlive the range it borrows. Passing an rvalue (or using the
  `fp::into(x) | …` pipe, which moves) makes the pipeline own its data;
- operations that fundamentally need all elements first — `sort`, `sort_by`,
  `partition`, `span`, `chunk`, `windows`, `group_by` — stay eager in `fp::`.

```cpp
// infinite source, finite pipeline
auto squares = std::views::iota(1)
             | fp::views::map([](int x) { return x * x; })
             | fp::views::take(5);
fp::to_vector(squares);   // {1, 4, 9, 16, 25}

// heterogeneous zip (different iterator types are fine)
fp::views::zip(std::vector<int>{1, 2}, std::vector<std::string>{"a", "b"});
```

The windowing adaptors are lazy subrange views — nothing is copied until you
materialize:

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};

for (auto part : v | fp::views::chunk(2))     // {1,2}, {3,4}, {5}
    use(part);                                 // `part` is a subrange view

for (auto w : v | fp::views::slide(3))         // {1,2,3}, {2,3,4}, {3,4,5}
    use(w);

fp::to_vector(v | fp::views::stride(2));       // {1, 3, 5}
fp::to_vector(fp::views::take_last(v, 2));     // {4, 5}

// fused zip + map: no intermediate pair vector
auto sums = fp::out(fp::into(a) | fp::views::zip_with(b, [](int x, int y) {
                      return x + y;
                    }));

// three-way zip into tuples
for (auto [x, y, z] : fp::views::zip3(a, b, c)) { /* ... */ }
```

All the new adaptors are *pipe closures*: they work both as
`fp::views::chunk(v, n)` and as `v | fp::views::chunk(n)` (or
`fp::into(v) | fp::views::chunk(n)`), and they own the range when it is an
rvalue — so `fp::out(fp::into(std::move(v)) | fp::views::slide(4))` is safe.

Rule of thumb: reach for `fp::` when you want a materialized result,
`fp::views::` when you want to compose passes without intermediates, and
[`inplace.hpp`](inplace.md) when you want to mutate without allocating at all.

# Collections — `vec.hpp` and `ranges.hpp`

The collection layer transforms, filters, folds, and zips sequences. Most
functions take `std::vector<T> const&` (`vec.hpp`); `ranges.hpp` provides the
same names (and a few extras) generically over any range — arrays, `std::span`,
views, `std::list`, etc. Because the `ranges.hpp` overloads are constrained to
`std::ranges::range`, both headers can be included together and overload
resolution picks the right one.

```cpp
#include <fp/all.hpp>

std::vector<int> v = {1, 2, 3, 4};
auto doubled = fp::map(v, [](int x) { return x * 2; });   // {2,4,6,8}
```

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

## `ranges.hpp` extras

In addition to the range-generic overloads of everything above, `ranges.hpp`
adds `to_vector(r)` (materialize any range to `vector`):

```cpp
int arr[] = {1, 2, 3};
auto v2 = fp::map(arr, fp::plus(1));     // works on a raw array -> {2,3,4}
auto v3 = fp::to_vector(arr);            // {1,2,3}
```

**Rule of thumb:** if you hold a `std::vector`, either header works. If you hold
anything else (array, `std::span`, a `std::views::filter` result), include
`ranges.hpp` and pass it directly.

## Pairing with `fp::ops`

The named operators (see [Function composition](composition.md)) make these
combinators read like point-free pipelines:

```cpp
fp::filter(v, fp::gt(2));                          // {3,4}
fp::map(v, fp::plus(1));                           // {2,3,4,5}
fp::fold_left(v, 0, fp::plus);                     // 10
fp::map(v, fp::times(2));                          // {2,4,6,8}
```

# 02 — Collections

**Headers:** `<fp/vec.hpp>`, `<fp/ranges.hpp>`, `<fp/grid.hpp>`, `<fp/ops.hpp>`.

`zip`, `group_by`, `scan`, `chunk`, `partition`, `flat_map`, and the grid
helpers. These need two or three combinators composed.

---

### 1. Pairwise sums · Easy

Given `{1,2,3}` and `{10,20,30}`, produce `{11,22,33}` (element-wise add).
Use `zip_with`.

```cpp
assert(fp::zip_with(std::vector<int>{1,2,3}, std::vector<int>{10,20,30},
                    [](int a, int b){ return a+b; })
       == std::vector<int>({11,22,33}));
```

### 2. Group words by length · Easy

Given `{"a","bb","cc","ddd"}`, build a map from length to the words of that
length. Use `fp::group_by`.

```cpp
auto g = fp::group_by(std::vector<std::string>{"a","bb","cc","ddd"},
                      [](std::string const& s){ return s.size(); });
assert(g.at(1) == std::vector<std::string>({"a"}));
assert(g.at(2) == std::vector<std::string>({"bb","cc"}));
```

### 3. Running total · Easy

Given `{1,2,3,4}`, produce `{1,3,6,10}` (prefix sums). Use `fp::scan`.

```cpp
assert(fp::scan(std::vector<int>{1,2,3,4}, 0, [](int a,int b){ return a+b; })
       == std::vector<int>({1,3,6,10}));
```

### 4. Chunk into pairs · Easy

Given `{1,2,3,4,5}`, produce `{{1,2},{3,4},{5}}`. Use `fp::chunk`.

```cpp
assert(fp::chunk(std::vector<int>{1,2,3,4,5}, 2)
       == std::vector<std::vector<int>>({{1,2},{3,4},{5}}));
```

### 5. Split evens and odds · Easy

Given `{1,2,3,4,5}`, return `pair{{2,4},{1,3,5}}`. Use `fp::partition`.

```cpp
auto [evens, odds] = fp::partition(std::vector<int>{1,2,3,4,5},
                                   [](int x){ return x%2==0; });
assert(evens == std::vector<int>({2,4}));
```

### 6. Flatten a list of lists · Easy

Given `{{1,2},{3},{4,5}}`, produce `{1,2,3,4,5}`. Use `fp::concat`.

```cpp
assert(fp::concat(std::vector<std::vector<int>>{{1,2},{3},{4,5}})
       == std::vector<int>({1,2,3,4,5}));
```

### 7. Words per sentence · Medium

Given `{"a b", "c d e"}`, count the total number of words across all strings
(5). Use `flat_map` + `str::split` + `size`.

```cpp
auto words = fp::flat_map(std::vector<std::string>{"a b","c d e"},
                          [](std::string const& s){ return fp::str::split(s, ' '); });
assert(words.size() == 5);
```

### 8. Transpose a matrix · Medium

Given `{{1,2,3},{4,5,6}}`, return `{{1,4},{2,5},{3,6}}`. Use `fp::transpose`
from `grid.hpp`.

```cpp
assert(fp::transpose(std::vector<std::vector<int>>{{1,2,3},{4,5,6}})
       == std::vector<std::vector<int>>({{1,4},{2,5},{3,6}}));
```

### 9. Sum of a flattened grid · Medium

Given `{{1,2},{3,4}}`, compute `1+2+3+4 = 10` via `fp::flatten` (grid) + `sum`.

```cpp
assert(fp::sum(fp::flatten(std::vector<std::vector<int>>{{1,2},{3,4}})) == 10);
```

### 10. Enumerate and filter indices · Medium

Given `{"x","y","z"}`, keep the elements at even indices: `{{0,"x"},{2,"z"}}`.
Use `fp::enumerate` + `fp::filter`.

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

# 01 — Warm-up

**Headers:** `<fp/vec.hpp>`, `<fp/ops.hpp>` (or `<fp/all.hpp>`).

These are the core vocabulary: `map`, `filter`, `fold_left`/`sum`, and the
named operators in `fp::ops`. Solve each with a single combinator call.

---

### 1. Double every element · Easy

Given `std::vector<int>{1,2,3,4}`, produce `{2,4,6,8}` using `fp::map`.

```cpp
// check
assert(fp::map(std::vector<int>{1,2,3,4}, [](int x){ return x*2; })
       == std::vector<int>({2,4,6,8}));
```

### 2. Sum of squares · Easy

Given `{1,2,3,4}`, compute `1²+2²+3²+4² = 30`. Use `map` then `sum`
(or `fold_left`).

```cpp
assert(fp::sum(fp::map(std::vector<int>{1,2,3,4}, [](int x){ return x*x; })) == 30);
```

### 3. Keep the positives · Easy

Given `{-2,-1,0,1,2}`, return `{1,2}` with `fp::filter`.

```cpp
assert(fp::filter(std::vector<int>{-2,-1,0,1,2}, fp::gt(0))
       == std::vector<int>({1,2}));
```

### 4. Count the evens · Easy

Given `{1,2,3,4,5,6}`, count how many are even (3). Use `fp::count_if` (or
`fp::count` from `ranges.hpp`).

```cpp
assert(fp::count_if(std::vector<int>{1,2,3,4,5,6}, [](int x){ return x%2==0; }) == 3);
```

### 5. First even number · Easy

Given `{1,3,5,6,7}`, find the index of the first even number (3). Use
`fp::find` (returns `optional<size_t>`).

```cpp
assert(fp::find(std::vector<int>{1,3,5,6,7}, [](int x){ return x%2==0; })
       == std::optional<size_t>(3));
```

### 6. Reverse it · Easy

Given `{1,2,3}`, return `{3,2,1}` with `fp::reverse`.

```cpp
assert(fp::reverse(std::vector<int>{1,2,3}) == std::vector<int>({3,2,1}));
```

---

## Solutions

<details>
<summary>1. Double every element</summary>

```cpp
auto out = fp::map(v, [](int x) { return x * 2; });
// or point-free: fp::map(v, fp::times(2));
```
</details>

<details>
<summary>2. Sum of squares</summary>

```cpp
int result = fp::sum(fp::map(v, [](int x) { return x * x; }));
// or: fp::fold_left(v, 0, [](int a, int x) { return a + x * x; });
```
</details>

<details>
<summary>3. Keep the positives</summary>

```cpp
auto out = fp::filter(v, fp::gt(0));
```
</details>

<details>
<summary>4. Count the evens</summary>

```cpp
size_t n = fp::count_if(v, [](int x) { return x % 2 == 0; });
```
</details>

<details>
<summary>5. First even number</summary>

```cpp
auto idx = fp::find(v, [](int x) { return x % 2 == 0; });
```
</details>

<details>
<summary>6. Reverse it</summary>

```cpp
auto out = fp::reverse(v);
```
</details>

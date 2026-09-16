# 09 — Memory & SIMD

**Headers:** `<fp/arena.hpp>`, `<fp/simd.hpp>` (simd is opt-in; build with
`-O2 -march=native`).

The `Arena` bump allocator and the vectorized mapping/reduction helpers.

---

### 1. Scoped scratch buffer · Easy

Use `fp::with_arena` to allocate a scratch `double[4]`, fill it, sum it, and
return the sum (a *value*, not a pointer). Confirm the result is `10.0`.

```cpp
double total = fp::with_arena(1024, [](fp::Arena& a) {
    auto* p = a.alloc<double>(4);
    p[0] = 1; p[1] = 2; p[2] = 3; p[3] = 4;
    return p[0] + p[1] + p[2] + p[3];
});
assert(total == 10.0);
```

### 2. Construct in place · Easy

Allocate and construct a small struct with `Arena::make`, then read a field.

```cpp
struct Point { int x, y; };
fp::Arena a;
auto* p = a.make<Point>(3, 4);
assert(p->x == 3 && p->y == 4);
a.reset();
```

### 3. SIMD reduction · Easy

Sum a vector of doubles with `fp::reduce` and compare to `std::accumulate`.

```cpp
std::vector<double> v = {1.0, 2.0, 3.0, 4.0};
assert(fp::reduce(v) == std::accumulate(v.begin(), v.end(), 0.0));
```

### 4. SIMD map in place · Easy

Double every element in place with `fp::map_inplace` and check the result.

```cpp
std::vector<double> v = {1.0, 2.0, 3.0};
fp::map_inplace(v, [](fp::vec<double> x){ return x * 2.0; });
assert(v == std::vector<double>({2.0, 4.0, 6.0}));
```

### 5. SIMD dot product · Medium

Compute the dot product of `{1,2,3}` and `{4,5,6}` (`32`) with `fp::dot`.

```cpp
assert(fp::dot(std::vector<double>{1,2,3}, std::vector<double>{4,5,6}) == 32.0);
```

---

## Solutions

<details>
<summary>1. Scoped scratch buffer</summary>

```cpp
double total = fp::with_arena(1024, [](fp::Arena& a) {
    auto* p = a.alloc<double>(4);
    // ... fill ...
    return p[0] + p[1] + p[2] + p[3];
});
```
</details>

<details>
<summary>2. Construct in place</summary>

```cpp
auto* p = a.make<Point>(3, 4);
```
</details>

<details>
<summary>3. SIMD reduction</summary>

```cpp
double sum = fp::reduce(v);
```
</details>

<details>
<summary>4. SIMD map in place</summary>

```cpp
fp::map_inplace(v, [](fp::vec<double> x){ return x * 2.0; });
```
</details>

<details>
<summary>5. SIMD dot product</summary>

```cpp
double d = fp::dot(a, b);
```
</details>

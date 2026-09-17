# 09 — Memory & SIMD

**Headers:** `<fp/arena.hpp>`, `<fp/simd.hpp>` (simd is opt-in; build with
`-O2 -march=native`).

## What this module is about

Two performance tools with the same ethos — give up a little ergonomics to win
where it counts:

- **`Arena`** — allocate scratch memory with one bulk `reset()` instead of
  per-object `malloc`/`free`. Realtime threads (audio, render) must not touch
  the heap; an arena is their allocation path.
- **`simd.hpp`** — `native_simd` vectors let you write the *operation* once and
  have it run across several lanes at once, for the math the compiler can't
  auto-vectorize (`sqrt`, `exp`) and for reductions.

The contract that keeps both safe: the arena is *scoped* (never escapes its
scope, return values not pointers), and SIMD lambdas operate on whole vectors.

---

### 1. Scoped scratch buffer · Easy

Use `with_arena` to allocate a scratch `double[4]`, fill it, sum it, return the
sum.

**Why `with_arena`:** the arena is only safe if it can't outlive its scope.
`with_arena(block, f)` hands `f` an arena that dies when `f` returns — so you
*physically cannot* leak an arena pointer out. You return the *value* (the sum),
not the pointer, keeping the enclosing expression pure.

```cpp
double total = fp::with_arena(1024, [](fp::Arena& a) {
    auto* p = a.alloc<double>(4);
    p[0] = 1; p[1] = 2; p[2] = 3; p[3] = 4;
    return p[0] + p[1] + p[2] + p[3];
});
assert(total == 10.0);
```

### 2. Construct in place · Easy

`Arena::make` a small struct and read a field.

**Why `make` over `alloc`:** `alloc` gives raw, uninitialized memory (you must
write into it). `make<T>(args...)` also runs `T`'s constructor via placement
`new`, so you get a live object without a heap allocation. `reset()` reclaims
it all at once (and — contract note — does *not* run destructors, so keep
payloads trivially destructible).

```cpp
struct Point { int x, y; };
fp::Arena a;
auto* p = a.make<Point>(3, 4);
assert(p->x == 3 && p->y == 4);
a.reset();
```

### 3. SIMD reduction · Easy

Sum a vector with `fp::reduce`; compare to `std::accumulate`.

**Why `reduce`:** it sums several lanes at once (`native_simd`), then does one
horizontal add at the end. Same result as `std::accumulate` — which the
compiler *also* vectorizes, so this is partly about correctness and the API,
with the speed win showing on wider data or when `-march` unlocks AVX512.

```cpp
std::vector<double> v = {1.0, 2.0, 3.0, 4.0};
assert(fp::reduce(v) == std::accumulate(v.begin(), v.end(), 0.0));
```

### 4. SIMD map in place · Easy

Double every element in place with `map_inplace`.

**Why a SIMD lambda:** your lambda receives a whole `fp::vec<double>` (several
lanes) and returns one — you write `x * 2.0` once and the vector arithmetic
applies it to all lanes. The scalar tail is handled for you. It's the same
`map` idea, but the function operates on vectors instead of scalars.

```cpp
std::vector<double> v = {1.0, 2.0, 3.0};
fp::map_inplace(v, [](fp::vec<double> x){ return x * 2.0; });
assert(v == std::vector<double>({2.0, 4.0, 6.0}));
```

### 5. SIMD dot product · Medium

Dot product of `{1,2,3}` and `{4,5,6}` (32).

**Why `dot`:** multiply-accumulate is the hottest numeric loop there is, and the
compiler can't always fuse it well. `fp::dot` does vectorized multiply then a
horizontal sum — same formula as the naive loop, expressed in lanes.

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

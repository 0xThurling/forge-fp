# 01 — Warm-up

**Headers:** `<fp/vec.hpp>`, `<fp/ops.hpp>` (or `<fp/all.hpp>`).

## What this module is about

These are the four verbs you'll use constantly: `map` transforms each element,
`filter` keeps a subset, `fold`/`sum` collapse a collection to one value, and
`fp::ops` supplies named functions so you don't write a lambda for `+`, `>`,
etc. The point of solving these *with the library* instead of raw loops:

- **Intent is explicit.** `fp::map(v, f)` says "apply f to every element" with
  no index, no `push_back`, no off-by-one.
- **Pure.** The input is never mutated; you always get a fresh collection.
- **Composable.** Each result feeds the next call, so you build pipelines
  instead of nested loops.

Most of the library is just these four verbs applied to other types (`Result`,
`optional`, ranges, grids) — learn them here first.

---

### 1. Double every element · Easy

Apply `x -> x*2` to every element of `{1,2,3,4}` to get `{2,4,6,8}`.

**Why `map`:** you want "same shape, new values" — every element transformed,
none dropped. A loop would need an index and a `push_back`; `map` is the whole
idea in one call, and the lambda is the only thing you actually write.

```cpp
assert(fp::map(std::vector<int>{1,2,3,4}, [](int x){ return x*2; })
       == std::vector<int>({2,4,6,8}));
```

### 2. Sum of squares · Easy

Compute `1²+2²+3²+4² = 30`.

**Why transform-then-reduce:** instead of one loop that squares *and* adds,
split it: `map` (square) produces the squares, `sum` (or `fold_left`) reduces
them. Each step is independently testable, and the two pieces are reusable
elsewhere. `fold_left(v, init, op)` is the general reduce; `sum` is its most
common special case.

```cpp
assert(fp::sum(fp::map(std::vector<int>{1,2,3,4}, [](int x){ return x*x; })) == 30);
```

### 3. Keep the positives · Easy

Filter `{-2,-1,0,1,2}` down to `{1,2}`.

**Why `filter` + `fp::gt(0)`:** `filter` is "keep elements where `pred(x)`".
`fp::gt(0)` is the named operator "greater than 0" — it reads as a predicate
and, being a value, it composes (`filter(v, gt(0))` reads like a sentence).
No lambda needed for something this common.

```cpp
assert(fp::filter(std::vector<int>{-2,-1,0,1,2}, fp::gt(0))
       == std::vector<int>({1,2}));
```

### 4. Count the evens · Easy

How many of `{1,2,3,4,5,6}` are even? (3)

**Why `count_if`:** counting is a fold wearing a specific hat — "how many match
a predicate". `count_if(v, pred)` is clearer than a loop with a hand-managed
`int n = 0; if (...) ++n;`, and it can't drift from the predicate.

```cpp
assert(fp::count_if(std::vector<int>{1,2,3,4,5,6}, [](int x){ return x%2==0; }) == 3);
```

### 5. First even number · Easy

Find the index of the first even in `{1,3,5,6,7}` (index 3).

**Why `find` returns `optional<size_t>`:** the "no match" case is real, and
`find` makes it explicit as `nullopt` instead of a sentinel like `-1` (which
silently collides with a real index). You're forced to handle "not found" —
that's the `Maybe` idea showing up early.

```cpp
assert(fp::find(std::vector<int>{1,3,5,6,7}, [](int x){ return x%2==0; })
       == std::optional<size_t>(3));
```

### 6. Reverse it · Easy

Turn `{1,2,3}` into `{3,2,1}`.

**Why `fp::reverse` over `std::reverse`:** `std::reverse` mutates its argument
in place; `fp::reverse` returns a new vector and leaves the input intact. That
purity is what lets you drop it into a pipeline (`reverse(sort(v))`) without
worrying about what got clobbered.

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

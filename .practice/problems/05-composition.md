# 05 — Function composition

**Headers:** `<fp/compose.hpp>`, `<fp/curry.hpp>`, `<fp/combinators.hpp>`,
`<fp/ops.hpp>`, `<fp/memoize.hpp>`.

Combine functions instead of data.

---

### 1. Compose two functions · Easy

`f(x) = x + 1`, `g(x) = x * 2`. Build `h = f ∘ g` with `fp::compose` and
evaluate `h(3) = 7`.

```cpp
auto h = fp::compose([](int x){ return x+1; }, [](int x){ return x*2; });
assert(h(3) == 7);
```

### 2. Pipe it · Easy

Same functions, left-to-right with `fp::pipe`: `(3+1)*2 = 8`.

```cpp
auto p = fp::pipe([](int x){ return x+1; }, [](int x){ return x*2; });
assert(p(3) == 8);
```

### 3. The `|` operator · Easy

Thread `3` through two lambdas and `out` the result, using `into` / `|` / `out`.

```cpp
int result = fp::out(fp::into(3)
    | [](int x){ return x * x; }
    | [](int x){ return x + 33; });
assert(result == 42);
```

### 4. Curry a function · Medium

Given `add3(a, b, c) = a + b + c`, curry it and call it as `c(1)(2)(3) == 6`.

```cpp
auto add3 = [](int a, int b, int c){ return a + b + c; };
auto c = fp::curry(add3);
assert(c(1)(2)(3) == 6);
```

### 5. Point-free filter + map · Medium

Given `{1,2,3,4,5}`, filter to `x > 2` then add 1 — using only `fp::ops`
(no lambdas): result `{4,5,6}`.

```cpp
auto v = fp::map(fp::filter(std::vector<int>{1,2,3,4,5}, fp::gt(2)), fp::plus(1));
assert(v == std::vector<int>({4,5,6}));
```

### 6. Memoize Fibonacci · Medium

Write fib with `fp::fix` (anonymous recursion) and `fp::memoize`, and confirm
`fib(20) == 6765`.

```cpp
auto fib = fp::memoize<int>(fp::fix([](auto recur, int n) -> long long {
    return n < 2 ? n : recur(n - 1) + recur(n - 2);
}));
assert(fib(20) == 6765);
```

### 7. Flip a comparison · Easy

`f(a, b) = a - b`. Use `fp::flip` so `flip(f)(1, 2) == f(2, 1) == 1`.

```cpp
auto f = [](int a, int b){ return a - b; };
assert(fp::flip(f)(1, 2) == 1);
```

---

## Solutions

<details>
<summary>1. Compose two functions</summary>

```cpp
auto h = fp::compose(f, g);   // right-to-left: f(g(x))
```
</details>

<details>
<summary>2. Pipe it</summary>

```cpp
auto p = fp::pipe(f, g);      // left-to-right: g(f(x))
```
</details>

<details>
<summary>3. The `|` operator</summary>

```cpp
int result = fp::out(fp::into(3) | f | g);
```
</details>

<details>
<summary>4. Curry a function</summary>

```cpp
auto c = fp::curry(add3);
int six = c(1)(2)(3);
```
</details>

<details>
<summary>5. Point-free filter + map</summary>

```cpp
auto v = fp::map(fp::filter(xs, fp::gt(2)), fp::plus(1));
```
</details>

<details>
<summary>6. Memoize Fibonacci</summary>

```cpp
auto fib = fp::memoize<int>(fp::fix([](auto recur, int n) -> long long {
    return n < 2 ? n : recur(n - 1) + recur(n - 2);
}));
```
</details>

<details>
<summary>7. Flip a comparison</summary>

```cpp
auto flipped = fp::flip(f);   // flipped(a, b) == f(b, a)
```
</details>

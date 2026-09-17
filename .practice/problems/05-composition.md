# 05 — Function composition

**Headers:** `<fp/compose.hpp>`, `<fp/curry.hpp>`, `<fp/combinators.hpp>`,
`<fp/ops.hpp>`, `<fp/memoize.hpp>`.

## What this module is about

So far you've applied functions to *data*. This module is about treating
*functions* as data: combining them (`compose`/`pipe`), partially applying them
(`curry`), naming the tiny ones (`fp::ops`), and remembering their results
(`memoize`). The payoff is code that reads as a pipeline of named pieces rather
than a nest of call sites.

---

### 1. Compose two functions · Easy

`f(x)=x+1`, `g(x)=x*2`; build `h = f ∘ g`, evaluate `h(3)=7`.

**Why `compose`:** function composition is the glue that avoids intermediate
variables. `compose(f, g)` is "do g, then f" — you describe a transformation
once and apply it everywhere. (Note the right-to-left order: `f(g(x))`.)

```cpp
auto h = fp::compose([](int x){ return x+1; }, [](int x){ return x*2; });
assert(h(3) == 7);
```

### 2. Pipe it · Easy

Same two functions, left-to-right: `(3+1)*2 = 8`.

**Why `pipe`:** `compose` reads right-to-left (math order); `pipe` reads
left-to-left (execution order), which most people find more natural for a
sequence of steps. Same idea, friendlier direction — and variadic.

```cpp
auto p = fp::pipe([](int x){ return x+1; }, [](int x){ return x*2; });
assert(p(3) == 8);
```

### 3. The `|` operator · Easy

Thread `3` through two lambdas using `into` / `|` / `out`.

**Why the pipe syntax:** `into(x) | f | g | out` is the ergonomic spelling of
`g(f(x))`. The value flows left-to-right, one stage per line, which is the
library's flagship "read it like a pipeline" idiom. `tap` lets a side effect
ride along without breaking the chain.

```cpp
int result = fp::out(fp::into(3)
    | [](int x){ return x * x; }
    | [](int x){ return x + 33; });
assert(result == 42);
```

### 4. Curry a function · Medium

Curry `add3(a,b,c) = a+b+c`; call it as `c(1)(2)(3) == 6`.

**Why `curry`:** partial application lets you fix some arguments now and supply
the rest later — `add3(1)` is "a function that adds 1 to two more numbers".
That's exactly how `fp::ops` operators get their `plus(1)` form. Currying turns
a multi-arg function into a chain of single-arg ones.

```cpp
auto add3 = [](int a, int b, int c){ return a + b + c; };
auto c = fp::curry(add3);
assert(c(1)(2)(3) == 6);
```

### 5. Point-free filter + map · Medium

Filter `{1,2,3,4,5}` to `>2`, then add 1 — no lambdas, only `fp::ops`.

**Why point-free:** for common operations (`>2`, `+1`), a lambda is noise.
`fp::gt(2)` and `fp::plus(1)` *are* the operation, named, so the pipeline reads
as "filter greater-than-2, then map add-1". Named operators also autocomplete
and refactor (a lambda's body can't).

```cpp
auto v = fp::map(fp::filter(std::vector<int>{1,2,3,4,5}, fp::gt(2)), fp::plus(1));
assert(v == std::vector<int>({4,5,6}));
```

### 6. Memoize Fibonacci · Medium

Write fib with `fp::fix` + `fp::memoize`; check `fib(20) == 6765`.

**Why `fix` + `memoize`:** recursion needs the function to name itself —
`fix` provides that name (`recur`) so you don't need `std::function` plumbing.
`memoize` then trades memory for time: each `fib(n)` is computed once, turning
an exponential recursion into linear. Two orthogonal concerns (self-reference,
caching) solved by two combinators.

```cpp
auto fib = fp::memoize<int>(fp::fix([](auto recur, int n) -> long long {
    return n < 2 ? n : recur(n - 1) + recur(n - 2);
}));
assert(fib(20) == 6765);
```

### 7. Flip a comparison · Easy

`f(a,b)=a-b`; show `flip(f)(1,2) == f(2,1) == 1`.

**Why `flip`:** argument order is a frequent mismatch (e.g. a function expects
`(acc, x)` but you have `(x, acc)`). `flip` adapts an existing function rather
than forcing you to rewrap it in a lambda — a one-word fix for a one-argument
swap.

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

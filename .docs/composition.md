# Function composition — `compose`, `pipe`, `curry`, `combinators`, `ops`, `memoize`

This layer is about *combining functions* rather than data. Headers:
`compose.hpp`, `curry.hpp`, `combinators.hpp`, `ops.hpp`, `memoize.hpp`.

The idea: once functions are values, you can compose them, partially apply
them, name the common ones, and cache their results — the same way the
collection layer composes data transforms. A pipeline of named functions reads
like a description of the computation, not a nest of call sites.

## `compose` and `pipe`

```cpp
#include <fp/compose.hpp>

// compose: right-to-left, f(g(x))
auto f = fp::compose([](int x) { return x + 1; }, [](int x) { return x * 2; });
f(3);   // (3*2)+1 = 7

// pipe: left-to-right, variadic
auto g = fp::pipe([](int x) { return x + 1; }, [](int x) { return x * 2; });
g(3);   // (3+1)*2 = 8
```

`compose` is also variadic (`fp::compose(f, g, h) = f(g(h(x)))`) via
`combinators.hpp`. The only difference between the two is direction:
`compose(f, g)` = "do `g` then `f`" (math order), `pipe(f, g)` = "do `f` then
`g`" (execution order). Pick the one that reads naturally; they're the same
under a reversal.

## The `|` pipe (`into` / `out` / `tap`)

`into(x) | f | g | out` threads a value through unary functions, left to right:

```cpp
auto result = fp::out(fp::into(3)
    | [](int x) { return x * x; }     // 9
    | [](int x) { return x + 33; });  // 42

// tap: run a side effect and pass the value through
fp::out(fp::into(3) | fp::tap([](int x) { log(x); }) | [](int x) { return x + 1; });
```

This is the library's flagship ergonomic: the value flows one stage per line.
If a stage returns `void`, the value passes through unchanged (useful with
`tap`). `into` decays its argument; `out` moves the final value out.

**`|` vs `pipe` vs `compose`:** `|` is for *applying* to a value now; `pipe`/
`compose` are for building a reusable function to apply later. `into(x) | f | g`
is sugar for `pipe(f, g)(x)`.

## `curry` and `uncurry`

```cpp
#include <fp/curry.hpp>

auto add = [](int a, int b, int c) { return a + b + c; };
auto c = fp::curry(add);
c(1)(2)(3);       // 6 — one arg at a time
c(1, 2)(3);       // 6 — or a few at a time

auto un = fp::uncurry(c);
un(1, 2, 3);      // 6
```

`curry` returns a callable that either invokes `f` (once it has enough
arguments) or binds more arguments. This is how `fp::ops` gets its `plus(1)`
form — currying is partial application made general.

## `combinators.hpp` — function plumbing

```cpp
#include <fp/combinators.hpp>

fp::identity(x);          // x
auto always7 = fp::const_(7);   // always7(anything) -> 7

fp::flip(f)(a, b);        // f(b, a)
fp::on(f, g)(x, y);       // f(g(x), g(y))  — e.g. compare by projection

fp::apply(f, a, b, c);    // f(a, b, c)  — variadic invoke

// fix: anonymous recursion (the Y combinator). f receives `recur`.
auto fib = fp::fix([](auto recur, int n) -> long long {
    return n < 2 ? n : recur(n - 1) + recur(n - 2);
});
fib(20);   // 6765

// when / unless: run f only if cond (identity otherwise)
fp::into(x) | fp::when(debug, fp::tap(log)) | /* ... */

// first / second: map over a pair's elements
fp::map(pairs, fp::second(fp::str::to_upper));

// pipe_with: lift a variadic consumer into a pipe
fp::pipe_with(fp::into(v), fp::fold_left, 0, fp::plus);
```

`fix` is the one that deserves a second look: it gives a lambda a name for
itself (`recur`), so recursion doesn't need `std::function` plumbing — pair it
with `memoize` for cached recursion (see below).

## `ops.hpp` — named operators (replace tiny lambdas)

```cpp
#include <fp/ops.hpp>

fp::plus(1, 2);      // 3      (binary form)
fp::plus(1)(2);      // 3      (curried: plus(1) is "add 1")
fp::minus(5, 2);     // 3
fp::times(2, 3);     // 6
fp::divide(8, 2);    // 4

fp::gt(3, 2);        // true   (binary)
fp::gt(0)(5);        // true   (curried predicate: "x > 0")

fp::and_(true, true);   // true
fp::or_(false, true);   // true
fp::not_(false);        // true

fp::negate(3);       // -3
fp::increment(3);    // 4
fp::decrement(3);    // 2
```

Full list:

| Kind | Operators |
|---|---|
| arithmetic | `plus`, `minus`, `times`, `divide` |
| comparison | `eq`, `ne`, `lt`, `le`, `gt`, `ge` |
| logic | `and_`, `or_`, `not_` |
| unary | `negate`, `increment`, `decrement` |

**Why named operators over lambdas:** `fp::plus(1)` is a *value* with a name —
it autocompletes, refactors, and reads as a sentence. A lambda
`[](int x){ return x+1; }` is a body you have to parse. For the small,
recurring operations, the name wins; for one-off logic, use a lambda.

**Argument order** is the one subtle rule:

- **Arithmetic/logic curry on the left operand:** `plus(1)(x) == 1 + x`.
  `plus(1)` is "add 1", and bare `plus` is a fold function.
- **Comparisons curry in predicate order:** `gt(0)(x) == x > 0` (not `0 > x`).
  The captured value is the *right* operand, matching `filter(v, gt(0))`.

Usage with the collections:

```cpp
fp::map(v, fp::plus(1));           // add 1 to every element
fp::filter(v, fp::gt(0));          // keep x where x > 0
fp::fold_left(v, 0, fp::plus);     // sum
fp::map(v, fp::negate);            // flip signs
```

## `memoize`

```cpp
#include <fp/memoize.hpp>

auto slow = [](int n) { /* expensive */ return n * n; };
auto fast = fp::memoize<int>(slow);
fast(5);      // computed
fast(5);      // cached
```

`memoize<Arg>(f)` caches results in an `unordered_map<Arg, Ret>` keyed on the
single argument. Note: it must wrap the *recursive* callable itself if you want
to memoize a recursion — combine with `fix` manually:

```cpp
auto fib = fp::memoize<int>(fp::fix([](auto recur, int n) -> long long {
    return n < 2 ? n : recur(n - 1) + recur(n - 2);
}));
```

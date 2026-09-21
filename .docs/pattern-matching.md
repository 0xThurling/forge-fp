# Pattern matching — `adt.hpp`

`adt.hpp` gives you exhaustive, compiler-checked dispatch over `std::variant`,
ordered guards over predicates, and `match` overloads for the wrapper types
(`optional`, `Result`, `Either`, `Validation`, `Outcome`).

```cpp
#include <fp/adt.hpp>
```

## Two different tools (don't confuse them)

| Tool | Question | Checked by |
|---|---|---|
| `match(v, case_<T>(f), …)` | "which *type* is in this variant?" | the compiler: every alternative must be covered |
| `cond(v, when(p, f), …, otherwise(f))` | "which *predicate* matches first?" | you: ordered, first match wins, not exhaustive |

The names are deliberately separate so the two mental models never blur.
`match` is about **shape** (types); `cond` is about **order** (predicates).

## `match` + `case_` — exhaustive variant dispatch

```cpp
#include <variant>

struct Circle { double radius; };
struct Rect   { double w, h; };
using Shape = std::variant<Circle, Rect>;

double area(Shape const &s) {
  return fp::match(s,
      fp::case_<Circle>([](Circle const &c) { return 3.14159 * c.radius * c.radius; }),
      fp::case_<Rect>  ([](Rect const &r)   { return r.w * r.h; }));
}
```

- `match(variant, arms…)` is a `std::visit` wrapper. Miss an alternative and
  it is a **compile error** — add a type to `Shape` and every `match` over it
  breaks until it is handled.
- `case_<T>(f)` binds an arm to alternative `T`; `f` receives `T const&`.
  Writing `case_<Circle>` instead of `[](Circle const&)` is what lets the
  compiler point at the *arm* rather than at a generic lambda.
- Dispatch is **O(1)**: a jump on the variant's index, not a linear chain of
  `holds_alternative` tests.

### Why not `std::visit` directly?

You can, and `match` is built on it — but a hand-written visitor needs
`overload` boilerplate, and the compiler's "no matching call" error for a
missing alternative is far less clear than "no match for `case_<Circle>`".

```cpp
// equivalent, more ceremony:
std::visit(fp::overload{
    [](Circle const &c) { return c.radius; },
    [](Rect const &r)   { return r.w * r.h; },
}, s);
```

### `overload` on its own

`fp::overload{...}` is just a callable that inherits from each lambda, so it is
useful anywhere a visitor is expected:

```cpp
auto describe = fp::overload{
    [](int x)                { return "int " + std::to_string(x); },
    [](std::string const &s) { return "string " + s; },
};
std::visit(describe, value);
```

## `match` for the wrapper types

The ADT overloads take **two** arms: success first, failure second.

```cpp
// optional: some(T) / none()
std::string label = fp::match(std::optional<int>{3},
    [](int x) { return "got " + std::to_string(x); },
    []        { return std::string("nothing"); });

// Result / Either / Validation / Outcome: ok_f(T) / err_f(error)
auto message = fp::match(fp::err<int>("boom"),
    [](int)                { return "ok"; },
    [](std::string const &) { return "failed"; });

// Result<void>: the success arm takes no arguments
auto code = fp::match(fp::ok<void>(),
    []                     { return 0; },
    [](std::string const &) { return 1; });
```

Both arms must return the **same type** (or both `void`), because `match`
returns a value:

```cpp
auto n = fp::match(fp::ok(2), [](int x) { return x; }, [](std::string const &) { return -1; });
// n == 2
```

## `cond` / `when` / `otherwise` — ordered guards

When the question is a predicate chain rather than a type, use `cond`:

```cpp
std::string kind = fp::cond(x,
    fp::when(fp::gt(0),  [](auto) { return "positive"; }),
    fp::when(fp::lt(0),  [](auto) { return "negative"; }),
    fp::otherwise([](auto)      { return "zero"; }));
```

- `when(pred, f)` — arm taken only if `pred(value)` is true.
- `otherwise(f)` — catch-all; always matches.
- Arms are evaluated **in order**; the first match wins. `cond` is a linear
  scan, not a jump — that is the price of arbitrary predicates.
- Without `otherwise`, `cond` throws `std::runtime_error("cond: no arm
  matched")` if nothing matches. End with `otherwise` unless the throw is
  genuinely a bug path.

Predicates compose with `fp::ops`, which makes the guards read as sentences:

```cpp
auto grade = fp::cond(score,
    fp::when(fp::ge(90), fp::const_("A")),
    fp::when(fp::ge(80), fp::const_("B")),
    fp::when(fp::ge(70), fp::const_("C")),
    fp::otherwise(fp::const_("F")));
```

Every arm receives the value, so `fp::const_("A")` (a function ignoring its
argument) is the natural fit when the result does not depend on it.

## Worked example: an expression tree

Variants nest naturally; `match` handles the recursion:

```cpp
struct Num { double value; };
struct Add;
struct Mul;
using Expr = std::variant<Num, Add, Mul>;

struct Add { std::shared_ptr<Expr> lhs, rhs; };
struct Mul { std::shared_ptr<Expr> lhs, rhs; };

double evaluate(Expr const &e) {
  return fp::match(e,
      fp::case_<Num>([](Num const &n) { return n.value; }),
      fp::case_<Add>([&](Add const &a) {
        return evaluate(*a.lhs) + evaluate(*a.rhs);
      }),
      fp::case_<Mul>([&](Mul const &m) {
        return evaluate(*m.lhs) * evaluate(*m.rhs);
      }));
}
```

Adding a `Div` alternative makes this function fail to compile until it is
handled — exactly what you want from a tree walk.

## `value_or` and `unpack`

```cpp
fp::value_or(std::optional<int>{}, 42);          // 42
fp::value_or(fp::err<int>("x"), 0);              // 0
fp::value_or(fp::ok(7), 0);                      // 7

// unpack: call a binary function on a pair's elements
auto add = fp::unpack([](int a, int b) { return a + b; });
fp::map(fp::zip(as, bs), add);                   // {a0+b0, a1+b1, …}
```

`value_or` is the "collapse the wrapper to a plain value" escape hatch; prefer
`match`/`and_then` when the two cases deserve different behaviour.

## Which tool when

| Situation | Tool |
|---|---|
| `std::variant` with a fixed set of alternatives | `match` + `case_` |
| A visitor you want to name and reuse | `overload` |
| `optional` / `Result` / `Either` / `Validation` | `match` (two arms) |
| Ordered predicate guards | `cond` + `when` / `otherwise` |
| A default value, no branching | `value_or` |
| A pair as two arguments | `unpack` |

## Gotchas

- **Arms must agree on the return type.** `match` returns a value; all arms
  must return the same type (or all `void`). If you need different types per
  arm, wrap them in a common variant or return `void` and act inside.
- **`case_<T>` must name the exact alternative type.** `case_<Circle>` matches
  `Circle`; it does not match a type convertible to it.
- **`match` on an `optional`/`Result` is by value.** The arms receive the
  contained value; if you need to mutate, capture by reference and return a
  reference-free type.
- **`cond` is not exhaustive.** Forgetting `otherwise` is legal and only
  surfaces at runtime as a throw. Make it a habit to end with `otherwise`.
- **`cond` arms see the value, not the index.** Predicates that need the index
  should use `fp::enumerate` first.
- **Don't use `match` for predicates.** A `cond` over `if/else` is clearer than
  a one-arm `match` with `std::monostate`.
- **`variant` alternatives should be distinct.** Two identical alternatives
  make `case_` ambiguous; use distinct wrapper types.

# Pattern matching — `adt.hpp`

`adt.hpp` gives you exhaustive, O(1) dispatch over `std::variant`, plus ordered
guards, and `match` overloads for `std::optional` and `Result`.

```cpp
#include <fp/adt.hpp>
```

## Why `match` instead of `if (holds_alternative(...))`

Extracting a value from `std::variant` without help means either `std::visit`
(with a hand-written visitor) or a chain of `holds_alternative` + `get` — both
easy to get wrong, and the compiler can't check you've covered every case.
`match` fixes both:

- **Exhaustive, by the compiler.** Miss an alternative and it won't compile —
  add a variant to the type and every `match` over it breaks until you cover it.
- **O(1).** Dispatch is a jump on the variant index, not a linear `if` chain.

`cond` is the *different* tool: an ordered, first-match-wins predicate chain.
The names are deliberately separate so you never confuse "exhaustive over
types" (`match`) with "ordered over predicates" (`cond`).

## `match` + `case_` — exhaustive variant dispatch

```cpp
#include <variant>

struct Circle { double radius; };
struct Rect   { double w, h; };
using Shape = std::variant<Circle, Rect>;

Shape s = Circle{2.0};

double area = fp::match(s,
    fp::case_<Circle>([](auto c) { return 3.14159 * c.radius * c.radius; }),
    fp::case_<Rect>  ([](auto r) { return r.w * r.h; }));
```

- `match(variant, arms...)` is a `std::visit` wrapper — the compiler *checks*
  that every alternative is covered (missing one = compile error).
- `case_<T>(f)` binds an arm to alternative type `T` and gives it a name to
  write `f(c)` instead of `[](Circle const& c){...}`.

## `overload` — a callable combining several lambdas

`match` is built on `overload`, which you can use directly:

```cpp
auto visitor = fp::overload{
    [](Circle const& c) { return c.radius; },
    [](Rect const& r)   { return r.w * r.h; },
};
std::visit(visitor, s);
```

## `match` for `std::optional` and `Result`

```cpp
// optional: some(T) / none()
std::string s = fp::match(std::optional<int>{3},
    [](int x) { return "got " + std::to_string(x); },
    []       { return std::string("nothing"); });

// Result: ok_f(T) / err_f(std::string)
auto msg = fp::match(fp::err<int>("boom"),
    [](int x)              { return "ok"; },
    [](std::string const&) { return "failed"; });
```

## `cond` / `when` / `otherwise` — ordered guards

When you need first-match-wins *predicates* (not exhaustive types), use `cond`:

```cpp
std::string kind = fp::cond(x,
    fp::when(fp::gt(0),  [](auto) { return "positive"; }),
    fp::when(fp::lt(0),  [](auto) { return "negative"; }),
    fp::otherwise([](auto)      { return "zero"; }));
```

- `when(pred, f)` — arm taken only if `pred(v)`.
- `otherwise(f)` — catch-all, always matches.
- `cond(v, arms...)` — evaluates arms in order, first match wins; throws if none
  match (so end with `otherwise`).

Every arm receives the value. Unlike `match`, `cond` is a *linear scan* and is
*not* exhaustive — that's the trade for being able to ask arbitrary predicates.

## `value_or` and `unpack`

```cpp
fp::value_or(std::optional<int>{}, 42);          // 42

// unpack: call a binary function on a pair's elements
auto add = fp::unpack([](int a, int b) { return a + b; });
fp::map(fp::zip(as, bs), add);
```

## Summary

| Tool | Use for |
|---|---|
| `match(variant, case_<T>(f)...)` | exhaustive, O(1) dispatch |
| `match(optional, some, none)` | optionals |
| `match(result, ok_f, err_f)` | results |
| `cond(v, when(p,f)..., otherwise(f))` | ordered, first-match guards |
| `overload{...}` | build a visitor by hand |
| `unpack(f)` | call `f(a, b)` on a pair |
| `value_or(opt, fallback)` | default an optional |

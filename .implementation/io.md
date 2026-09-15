# io.md — IO, grids, and ergonomics sugar — backlog

Three independent additions that don't yet have a module file of their own.
Once a header lands, its entries move to a dedicated doc:

| Area | Landed as |
|---|---|
| A. File IO + CLI helpers | new `io.hpp` |
| B. ND-array / nested-loop combinators | new `grid.hpp` |
| C. Match / pattern sugar | `adt.hpp` |
| D. Point-free operators | new `ops.hpp` |
| E. Macros (opt-in) | new `macros.hpp` |

All entries are **to do**; nothing below is implemented yet.

---

## A. `io.hpp` — file IO

### A1. Read / write helpers [P0]

```
Result<std::string>                read_file(std::string path);
Result<std::vector<std::string>>   read_lines(std::string path);
Result<void>                       write_file(std::string path, std::string_view content);
Result<void>                       write_lines(std::string path, std::vector<std::string> const&);
```

**Why:** The library has no IO at all. Users parsing a file into
`Result<vector<T>>` re-write the same `ifstream` + `getline` boilerplate,
and failures (missing file, permissions, disk full) silently escape the
`Result` system. These four functions give file IO a `Result` entry point
that composes with everything in `result.md` (`map`, `and_then`, `sequence`,
`try_`) — `read_lines(path) | and_then(parse_csv)` is the whole pipeline.
P0: no `Result`-compatible way to touch a file exists today.

**Implementation** — `#include <fstream>`; `err`/`ok` from `result.hpp`

```cpp
inline Result<std::string> read_file(std::string const &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return err<std::string>("cannot open " + path);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  if (in.bad())
    return err<std::string>("read failed: " + path);
  return ok(std::move(content));
}

inline Result<std::vector<std::string>> read_lines(std::string const &path) {
  std::ifstream in(path);
  if (!in)
    return err<std::vector<std::string>>("cannot open " + path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line))
    lines.push_back(std::move(line));
  return ok(std::move(lines));
}

inline Result<void> write_file(std::string const &path,
                               std::string_view content) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    return err<void>("cannot open " + path);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out)
    return err<void>("write failed: " + path);
  return ok();
}
```

### A2. `interact` / `lift_io` [P1]

```
template <class F> Result<R> lift_io(F f);   // wrap a throwing IO callable
void interact(F f);                          // f: string -> string, stdin -> stdout
```

**Why:** Compositions of A1. `lift_io` turns any throwing IO function into a
`Result`-returning one (generalizes the existing `result::try_`, which only
takes zero-arg callables). `interact` is the classic read-all/transform/
write-all main-loop shape (Haskell `interact`) that makes small CLI tools
one-liners: `interact(compose(parse, transform, render))`.

**Implementation** — `lift_io` mirrors `result.hpp`'s `try_`

```cpp
template <class F>
auto lift_io(F f) {
  return [f = std::move(f)](auto &&...args)
      -> Result<std::invoke_result_t<F, decltype(args)...>> {
    using R = std::invoke_result_t<F, decltype(args)...>;
    try {
      return ok(f(std::forward<decltype(args)>(args)...));
    } catch (std::exception const &e) {
      return err<R>(e.what());
    } catch (...) {
      return err<R>("unknown IO error");
    }
  };
}

inline void interact(std::function<std::string(std::string)> f) {
  std::string in((std::istreambuf_iterator<char>(std::cin)),
                 std::istreambuf_iterator<char>());
  std::cout << f(std::move(in));
}
```

---

## B. `grid.hpp` — ND arrays and nested loops

### B1. 2D helpers [P0]

```
template <class T, class F> auto map2d(std::vector<std::vector<T>> const&, F);
template <class T> std::vector<std::vector<T>> transpose(std::vector<std::vector<T>> const&);
template <class T> std::vector<T> flatten(std::vector<std::vector<T>> const&);
template <class F> void for_each_index(size_t rows, size_t cols, F f);   // f(i, j)
template <class T, class F> std::vector<T> tabulate(size_t n, F f);      // f(0..n-1)
template <class A, class B>
std::vector<std::pair<A, B>> cartesian_product(std::vector<A> const&, std::vector<B> const&);
```

**Why:** 3D/graphics/image work in C++ is nested `for (i...) for (j...)`
loops over `vector<vector<T>>`, and nothing in the library addresses index
space — `vec.hpp`/`ranges.hpp` are flat. These helpers turn render passes
into `map2d(pixels, shade)` and index iteration into
`for_each_index(h, w, ...)`, composable with `concurrent.hpp`'s
`par_for_each`/`par_map` for parallel scanlines. P0: the most common
"real-world" C++ shape (2D grids) has zero support.

**Implementation** — `cartesian_product` is the nested loop made explicit

```cpp
template <class T, class F>
auto map2d(std::vector<std::vector<T>> const &g, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<std::vector<R>> out;
  out.reserve(g.size());
  for (auto const &row : g)
    out.push_back(fp::map(row, f));
  return out;
}

template <class T>
std::vector<std::vector<T>> transpose(std::vector<std::vector<T>> const &g) {
  if (g.empty())
    return {};
  size_t rows = g.size(), cols = g[0].size();
  std::vector<std::vector<T>> out(cols, std::vector<T>(rows));
  for (size_t i = 0; i < rows; ++i)
    for (size_t j = 0; j < cols; ++j)
      out[j][i] = g[i][j];
  return out;
}

template <class T>
std::vector<T> flatten(std::vector<std::vector<T>> const &g) {
  std::vector<T> out;
  size_t n = 0;
  for (auto const &row : g)
    n += row.size();
  out.reserve(n);
  for (auto const &row : g)
    out.insert(out.end(), row.begin(), row.end());
  return out;
}

template <class F>
void for_each_index(size_t rows, size_t cols, F f) {
  for (size_t i = 0; i < rows; ++i)
    for (size_t j = 0; j < cols; ++j)
      f(i, j);
}

template <class T, class F>
std::vector<T> tabulate(size_t n, F f) {
  std::vector<T> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.push_back(f(i));
  return out;
}

template <class A, class B>
std::vector<std::pair<A, B>> cartesian_product(std::vector<A> const &as,
                                               std::vector<B> const &bs) {
  std::vector<std::pair<A, B>> out;
  out.reserve(as.size() * bs.size());
  for (auto const &a : as)
    for (auto const &b : bs)
      out.emplace_back(a, b);
  return out;
}
```

`cartesian_product` + the `unpack` sugar in C1 give the declarative nested
loop: `map(cartesian_product(as, bs), unpack(f))`.

### B2. 3D and windows [P1]

```
template <class T, class F> auto map3d(std::vector<std::vector<std::vector<T>>> const&, F);
template <class T> std::vector<std::vector<T>> window2d(std::vector<std::vector<T>> const&,
                                                        size_t h, size_t w);   // overlapping
```

**Why:** Volumes/voxels need the third axis (`map3d`); image kernels need
overlapping windows (`window2d` — the base of convolution and blur, the
`ranges::windows` analogue for grids). P1: 2D covers most users; these round
out graphics/vision workloads.

**Implementation** — `map3d` is `map2d` on the middle axis

```cpp
template <class T, class F>
auto map3d(std::vector<std::vector<std::vector<T>>> const &g, F f) {
  std::vector<std::vector<std::vector<std::invoke_result_t<F, T>>>> out;
  out.reserve(g.size());
  for (auto const &plane : g) {
    std::vector<std::vector<std::invoke_result_t<F, T>>> p;
    p.reserve(plane.size());
    for (auto const &row : plane)
      p.push_back(fp::map(row, f));
    out.push_back(std::move(p));
  }
  return out;
}
```

---

## C. Match / pattern sugar (`adt.hpp`)

### C1. `case_<T>` — named, O(1) exhaustive dispatch [P1]

```
template <class T, class F> auto case_(F f);   // case_<Circle>(f) == [](Circle const& c){ return f(c); }
```

**Why:** `match` + `overload` is already O(1) (`std::visit` jumps on the
variant index) and compiler-checked (missing alternative = error). `case_`
is the ergonomic spelling — it drops the `[](Circle const& c)` noise and
gives a named binding, while preserving exhaustiveness. This is the *default*
match form; guards are branches inside the arm, not separate syntax.

**Implementation** — sugar over `overload`; the non-generic lambda keeps
overload resolution exact

```cpp
template <class T, class F>
auto case_(F f) {
  return [f = std::move(f)](T const &v) -> decltype(auto) { return f(v); };
}
```

Usage:

```cpp
double area = fp::match(shape,
  fp::case_<Circle>([](auto const &c) { return 3.14159 * c.radius * c.radius; }),
  fp::case_<Rect>  ([](auto const &r) { return r.w * r.h; }));
```

### C2. `cond` / `when` / `otherwise` — ordered guards [P1]

```
template <class P, class F> auto when(P pred, F f);   // arm taken only if pred(v)
template <class F>          auto otherwise(F f);       // catch-all, always matches
template <class T, class... Arms> auto cond(T const&, Arms&&...);
```

**Why:** For "evaluate predicates in order, first match wins" — guards that
span values (and types) with fallthrough. Deliberately **not** `match`:
ordered first-match is a *conditional chain* (a Lisp `cond`), and calling it
`match` would invite users to drop exhaustiveness checking. `cond` is the
opt-in expressive case; `match` stays exhaustive. Every arm (including
`otherwise`) receives the value.

**Implementation** — linear scan; arms must return the same type; throws if
none match (the last arm should be `otherwise`)

```cpp
template <class P, class F> struct Arm {
  P pred;
  F fn;
};
template <class P, class F> Arm<P, F> when(P p, F f) { return {std::move(p), std::move(f)}; }

struct always_t {
  template <class T> bool operator()(T const &) const { return true; }
};
inline constexpr always_t always{};
template <class F> auto otherwise(F f) { return when(always, std::move(f)); }

template <class T, class P, class F, class... Rest>
auto cond(T const &v, Arm<P, F> a, Rest &&...rest) {
  if (a.pred(v))
    return a.fn(v);
  if constexpr (sizeof...(Rest) > 0)
    return cond(v, std::forward<Rest>(rest)...);
  throw std::runtime_error("cond: no arm matched");
}
```

Usage:

```cpp
fp::cond(x,
  fp::when(fp::gt(0), [](auto) { return "positive"; }),
  fp::when(fp::lt(0), [](auto) { return "negative"; }),
  fp::otherwise([](auto) { return "zero"; }));
```

### C3. `match` overloads for `std::optional` and `Result` [P1]

```
T match(std::optional<T>, F some, G none);   // some(T) / none()
R match(Result<T>, F ok_f, G err_f);         // ok_f(T) / err_f(std::string)
```

**Why:** `adt.hpp`'s `match` covers `std::variant` only; `optional` and
`Result` consumers still write `if (x.has_value())` / `if (r.is_ok())`
blocks — exactly the branching `match` exists to remove. One name, one idiom
across all three ADTs.

**Implementation** — mirrors `adt.hpp`'s variant `match`; the `Result` error
arm receives the message string

```cpp
template <class T, class F, class G>
auto match(std::optional<T> const &o, F some, G none) {
  return o ? some(*o) : none();
}

template <class T, class F, class G>
auto match(Result<T> const &r, F ok_f, G err_f) {
  return r.is_ok() ? ok_f(r.value()) : err_f(r.error());
}
```

### C4. `value_or` — pipe-friendly defaulting [P2]

**Lands in:** `src/fp/compose.hpp` (with the `Piped` helpers)

```
Piped<T> value_or(Piped<std::optional<T>>, T fallback);
```

**Why:** `into(maybe_x) | map(f) | value_or(def)` reads better than
`maybe_x ? f(*maybe_x) : def` and composes with the `into`/`operator|`
surface in `compose.hpp`. P2 — sugar only; it is the pipe counterpart of
`maybe.hpp`'s `or_else` (function-based).

**Implementation** — free function (Piped overload once the pattern is established)

```cpp
template <class T>
T value_or(std::optional<T> const &o, T fallback) {
  return o.value_or(std::move(fallback));
}
```

### C5. `unpack` — call a binary function on a pair's elements [P1]

**Lands in:** `src/fp/combinators.hpp` (with `identity`/`const_`/`flip`/`on`)

```
template <class F> auto unpack(F f);   // f(a, b) called with the pair's elements
```

**Why:** The library produces pairs everywhere (`zip`, `cartesian_product`,
`group_by`, `partition`, `enumerate`), and consuming them is `.first`/
`.second` noise or a boilerplate lambda every time. `unpack(f)` is the
point-free consumer: `map(pairs, unpack([](a, b){ ... }))`.

**Implementation**

```cpp
template <class F>
auto unpack(F f) {
  return [f = std::move(f)](auto p) {
    return f(std::move(p.first), std::move(p.second));
  };
}
```

---

## D. Point-free operators (`ops.hpp`)

Named, curried function objects replacing the `_1`/`_2` placeholder idiom.
Each binary operator has **two call forms**:

1. **Binary** — `op(a, b)` applies the operator to both arguments at once.
2. **Curried** — `op(a)` captures the first argument and returns a unary
   function `op(a)(b)`. This is the partial-application form that feeds
   `map`/`filter`/`fold`.

Full surface:

| Operator | Kind | Binary form | Curried form |
|---|---|---|---|
| `plus`, `minus`, `times`, `divide` | arithmetic | `plus(a, b)` = `a + b` | `plus(a)(b)` = `a + b` |
| `eq`, `ne`, `lt`, `le`, `gt`, `ge` | comparison | `gt(a, b)` = `a > b` | `gt(a)(b)` = `b > a` |
| `and_`, `or_` | logic (binary) | `and_(a, b)` = `a && b` | `and_(a)(b)` = `a && b` |
| `not_`, `negate`, `increment`, `decrement` | unary | — | `not_(a)` = `!a` |

**Argument order** is the one sharp edge, resolved deliberately:

- **Arithmetic and logic curry on the left operand**: `plus(1)(x) == 1 + x`.
  The captured value is the *first* (left) argument, so `plus(1)` is "add 1"
  and bare `plus` is the fold function (`fold_left(v, 0, plus)`).
- **Comparisons curry in predicate order**: `gt(0)(x) == x > 0`, *not*
  `0 > x`. The captured value is the *second* (right) operand, so the
  partial form reads like the predicate it feeds — `filter(v, gt(0))` keeps
  every `x` with `x > 0`.

**Why:** `map`/`filter`/`fold` generate a lot of tiny lambdas; named
operators are the FP-idiomatic, macro-free replacement for placeholders
(`_1`/`_2`). `fp::plus` autocompletes and refactors — placeholders can't.

**Implementation** — binary ops are function objects with two overloaded
`operator()`s; the comparison overloads flip the curried form so the
predicate reading holds:

```cpp
// arithmetic — left-curried: plus(1)(x) == 1 + x
struct plus_t {
  template <class A> auto operator()(A a) const {
    return [a = std::move(a)](auto b) { return a + b; };   // plus(1) -> add 1
  }
  template <class A, class B> auto operator()(A a, B b) const { return a + b; }
};
inline constexpr plus_t plus{};
// minus_t, times_t, divide_t — identical shape with -, *, /

// comparison — predicate-curried: gt(0)(x) == x > 0
struct gt_t {
  template <class A> auto operator()(A a) const {
    return [a = std::move(a)](auto b) { return b > a; };   // gt(0) -> "> 0"
  }
  template <class A, class B> auto operator()(A a, B b) const { return a > b; }
};
inline constexpr gt_t gt{};
// eq_t, ne_t, lt_t, le_t, ge_t — identical shape; curried form is `b OP a`

// logic — left-curried like arithmetic (trailing _ avoids the keyword)
struct and_t {
  template <class A> auto operator()(A a) const {
    return [a = std::move(a)](auto b) { return a && b; };
  }
  template <class A, class B> auto operator()(A a, B b) const { return a && b; }
};
inline constexpr and_t and_{};
// or_t — identical shape with ||

// unary — plain lambdas, no curried form
inline constexpr auto negate    = [](auto a) { return -a; };
inline constexpr auto not_      = [](auto a) { return !a; };
inline constexpr auto increment = [](auto a) { return a + 1; };
inline constexpr auto decrement = [](auto a) { return a - 1; };
```

Usage:

```cpp
fp::map(v, fp::plus(1));              // add 1 to every element
fp::map(v, fp::times(0.5));           // halve every element
fp::filter(v, fp::gt(0));             // keep x where x > 0
fp::fold_left(v, 0, fp::plus);        // sum
fp::fold_left(bools, true, fp::and_); // all true
fp::map(v, fp::negate);               // flip signs
```

### D1. Per-operator notes

- **Arithmetic** (`plus`, `minus`, `times`, `divide`) — left-curried.
  `minus(1)(x)` is `1 - x` (captured value is the minuend); `divide(2)(x)`
  is `2 / x`. For "subtract *from* `x`" or "divide *into* `x`" write the
  lambda; there is deliberately no flipped arithmetic form.
- **Comparison** (`eq`, `ne`, `lt`, `le`, `gt`, `ge`) — predicate-curried.
  `lt(10)(x) == x < 10`. The captured value is the right operand, matching
  `filter`'s "keep if `predicate(x)`" reading. This is the *only* family
  whose curried and binary forms disagree on operand order.
- **Logic** (`and_`, `or_`, `not_`) — value-level, over `bool`. `and_`/`or_`
  are left-curried like arithmetic and take booleans, *not* predicates:
  `fold_left(bools, true, and_)` is "all true". Predicate *composition*
  (e.g. "x > 0 && x < 10") is out of scope here — `and_` will not call two
  lambdas. Trailing underscore is required because `and`, `or`, `not` are
  C++ alternative-token keywords.
- **Unary** (`negate`, `increment`, `decrement`) — plain lambdas, no partial
  form. `negate` is `-x`, `increment` is `x + 1`, `decrement` is `x - 1`.

> **Naming:** `eq`/`ne`/`lt`/`le`/`gt`/`ge` are used because operators can't
> be named (`==`, `!=`, `<`, `<=`, `>`, `>=` are not identifiers) and to
> avoid shadowing `std::equal_to` et al. in `<functional>`.

---

## E. Macros (`macros.hpp`, opt-in — NOT in `all.hpp`)

Two macros survive the "prefer named functions over macros" reframe: they do
things the type system and expressions cannot. They live in a dedicated
`macros.hpp` that `all.hpp` deliberately does **not** include — macros
pollute, so they're opt-in.

### E1. `FP_TRY` — the `?` operator [P1]

```
Result<T> f(...) { int x = FP_TRY(parse(y)); ... }
```

**Why:** The single highest-value ergonomic in a `Result` library: early
return on error without the `if (!r.is_ok()) return err(...)` dance. This
*is* do-notation — no `FP_DO` macro needed. GCC/Clang use a statement
expression; MSVC needs the lambda form (both behind `_MSC_VER`).

**Implementation**

```cpp
#if defined(_MSC_VER)
#define FP_TRY(expr)                                                     \
  [&](auto &&_r) -> decltype((_r).value()) {                             \
    if (!(_r).is_ok()) return err<decltype((_r).value())>((_r).error()); \
    return std::move((_r).value());                                      \
  }(expr)
#else
#define FP_TRY(expr)                                                     \
  ({ auto _r = (expr);                                                   \
     if (!_r.is_ok()) return err<decltype(_r.value())>(_r.error());      \
     std::move(_r.value()); })
#endif
```

### E2. `FP_VARIANT` — sum-type definition [P1]

```
FP_VARIANT(Shape, (Circle, double radius), (Rect, double w, double h));
```

**Why:** The real "match is hard" problem is *defining* the sum type —
payload structs, the `std::variant` alias, constructor functions. `match`
itself is already clean; this kills the definition boilerplate. The one
macro that generates types (which only a macro can do).

**Target expansion** — lowercase factories avoid colliding with the payload
type names:

```cpp
// FP_VARIANT(Shape, (Circle, double radius), (Rect, double w, double h))
// expands to:
struct Shape {
  struct Circle { double radius; };
  struct Rect   { double w; double h; };
  std::variant<Circle, Rect> value;

  static Shape circle(double radius) { return {{Circle{radius}}}; }
  static Shape rect(double w, double h) { return {{Rect{w, h}}}; }
};

// usage:
Shape s = Shape::circle(2.0);
double area = fp::match(s.value,
  fp::case_<Shape::Circle>([](auto c) { return 3.14159 * c.radius * c.radius; }),
  fp::case_<Shape::Rect>  ([](auto r) { return r.w * r.h; }));
```

**Implementation note:** the macro internals are non-trivial — comma-in-type
handling (`double w, double h`), the struct/`variant`/factory expansion, and
tag-type registration for `case_`. Nail the exact preprocessor expansion
against the test suite before relying on it; the *usage* above is the API
contract.

---

## Design decisions (resolved)

1. **`io.hpp` and `grid.hpp` land as new headers.** IO is a distinct
   concern (side effects, `fstream`) and would bloat `result.hpp`; index-
   space helpers deserve their own file even though they build on `vec.hpp`'s
   `map`. Both join `all.hpp`.
2. **IO errors are `Result` strings, never exceptions.** The library's
   error channel is `err<std::string>`; `lift_io` catches and converts
   throwing IO so callers see one failure mode.
3. **`interact` takes `std::function<std::string(std::string)>`** — a
   concrete type keeps error messages readable and accepts `compose`d
   pipelines directly.
4. **Sugar goes in existing headers, not new files.** `unpack` joins
   `identity`/`const_`/`flip`/`on` in `combinators.hpp`; `value_or` joins the
   `Piped` helpers in `compose.hpp`; `case_`/`cond`/`when`/`otherwise` and the
   `optional`/`Result` `match` overloads join `overload` in `adt.hpp`.
5. **`match` is exhaustive + O(1); guards are `cond`.** `case_<T>` keeps
   `std::visit`'s compiler-enforced exhaustiveness and jump-table dispatch;
   ordered guard fallthrough is a *different primitive* (`cond`), named
   separately so users never silently lose exhaustiveness.
6. **`fp::ops` replaces placeholders.** Named, partial-application-capable
   function objects (`plus`, `gt`, ...) over `_1`/`_2` macros: autocomplete,
   refactoring, no namespace pollution. Placeholders are demoted to an
   optional last-resort header if ever needed.
7. **Macros are opt-in and isolated.** Only `FP_TRY` and `FP_VARIANT`
   survive; both live in `macros.hpp`, which `all.hpp` deliberately does not
   include (macros pollute). `FP_TRY` handles GCC/Clang (`({ ... })`) and
   MSVC (lambda) behind `_MSC_VER`.
8. **Grid shapes are `vector<vector<T>>`, no new ND container.** A
   `grid<T>` class with strides is a bigger design (ownership, slicing,
   row-major vs column-major); the flat-nested-vector representation is what
   users already write, so combinators work on it directly.
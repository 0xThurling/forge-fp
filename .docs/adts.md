# ADTs — `Either`, `Result`, `Maybe`, `Validation`

Four types model *"a value that might not be there / might be wrong"*. They all
share the same combinator vocabulary — `map`, `and_then`, `or_else` — so once
you learn one you know all four.

| Type | Header | Failure side | Success side | Use for |
|---|---|---|---|---|
| `Either<E, T>` | `either.hpp` | `E` (any) | `T` | generic two-outcome value |
| `Result<T>` | `result.hpp` | `std::string` | `T` | functions that can fail with a message |
| `std::optional<T>` | `maybe.hpp` | *nothing* | `T` | a value that may be absent |
| `Validation<T>` | `validation.hpp` | `std::vector<std::string>` | `T` | *accumulating* many errors |

`Result<T>` is literally `Either<std::string, T>` and `Validation<T>` is
`Either<std::vector<std::string>, T>` — so the `Either` combinators work on all
of them.

## Why error-as-value instead of exceptions

Exceptions are a *separate* control flow: a thrown error jumps out of your code
to some distant `catch`. `Result`/`Either` make the failure a *value* in the
normal flow:

```cpp
// exceptions: the happy path is invisible; where does the error go?
int n = std::stoi(s);            // may throw, caught who-knows-where

// error as value: the failure is part of the return type
fp::Result<int> n = fp::str::to_int(s);   // you *must* look at is_ok()/value()
```

Consequences you get for free:

- **The type documents failure.** A function returning `Result<int>` advertises
  that it can fail; a `int` function cannot.
- **Errors are first-class.** You can `map` over them, collect them into
  vectors, log them, forward them — same tools as any value.
- **No control-flow surprises.** There is no hidden `throw`; the only way to
  get a bare `T` is `unwrap`/`expect`, which are explicitly the escape hatches.
- **Composes into pipelines.** `a >>= b >>= c` reads as a straight line.

The trade-off: you write `>>=`/`and_then` where exceptions would let you write
plain calls. ForgeFP's answer is that this is *worth it* — the boilerplate is
one operator, and the failure paths become explicit.

## The three combinators (the whole vocabulary)

Every ADT has these; learn them once:

| Combinator | Signature shape | What it does |
|---|---|---|
| `map(x, f)` | `F<T> -> F<U>` | transform the value, keep the wrapper/failure |
| `and_then(x, f)` | `F<T> -> F<U>` (f returns `F<U>`) | like `map`, but the *next* step can also fail |
| `or_else(x, f)` | `F<T> -> T` | recover from failure with a default/function |

The distinction between `map` and `and_then` is the one thing to internalize:

```cpp
fp::Result<int> r = fp::ok(21);

// map: f is a *total* function (always succeeds)
fp::map(r, [](int x) { return x * 2; });              // ok(42)

// and_then: f is *partial* (returns a Result, may fail)
fp::and_then(r, [](int x) { return fp::ok(x + 1); }); // ok(22)

// what map can't do: a step that itself fails
//   map would nest the Results; and_then flattens them.
```

Two useful laws to sanity-check your code:

- `map(x, id) == x` — mapping the identity changes nothing.
- `and_then(ok(a), f) == f(a)` and `and_then(err(e), f) == err(e)` — bind
  unwraps a success and short-circuits a failure.

## The bind operator `>>=`

`x >>= f` is exactly `and_then(x, f)`. It exists so fallible chains read
left-to-right:

```cpp
fp::Result<int> r = fp::str::to_int(s)
    >>= [](int x) { return x >= 0 ? fp::ok(x) : fp::err<int>("negative"); }
    >>= [](int x) { return fp::ok(x * 2); };
```

Each `>>=` feeds the previous value into the next step, short-circuiting on the
first error. This is the library's "do-notation".

## Which ADT when?

| Situation | Use |
|---|---|
| A value or a message (the default) | `Result<T>` |
| Two outcomes of arbitrary types | `Either<E, T>` |
| A value that may be absent (no error message) | `std::optional<T>` |
| Collect *every* problem at once (forms, config) | `Validation<T>` |
| No success value, just "worked or failed" | `Result<void>` |

Rule of thumb: **`Result` unless you have a reason not to.** `optional` for pure
absence, `Validation` for aggregation, `Either` for generic/two-typed cases.

## `Either<E, T>` — a generic two-outcome value

```cpp
#include <fp/either.hpp>

using E = fp::Either<std::string, int>;

E ok_val   = E::ok(42);                       // success side
E err_val  = E::err(std::string("boom"));     // failure side

ok_val.is_ok();          // true
ok_val.value();          // 42
err_val.error();         // "boom"
```

`Either` exposes `value_type` / `error_type` aliases and `value()`/`error()`
accessors. The underlying `std::variant<E, T>` is public as `.v` if you need
`std::visit`.

```cpp
fp::map(ok_val, [](int x) { return x * 2; });              // Either<string,int> ok(84)
fp::and_then(ok_val, [](int x) { return E::ok(x + 1); });  // ok(43)
fp::or_else(err_val, [](std::string const&) { return 0; }); // 0 (default)
fp::map_error(err_val, [](std::string e) { return "wrapped: " + e; });
```

Full `Either` surface (all free functions in `fp`):

- `map`, `and_then`, `or_else`, `map_error` — the core chain.
- `flatten(Either<E, Either<E,T>>)` — collapse a nested `Either`.
- `to_optional(e)` — `Either<E,T>` → `std::optional<T>` (drops the error).
- `rights(vector<Either<E,T>>)` / `lefts(...)` — collect the ok / err sides.
- `bimap(e, on_err, on_ok)` — map both sides at once.
- `swap(e)` — `Either<E,T>` → `Either<T,E>`.
- `expect(e, "msg")` — value or throw `std::runtime_error` (returns `T const&`).
- `ok_or(opt, err)` — `std::optional<T>` → `Either<E,T>`.
- `operator>>=` — `e >>= f` is `and_then(e, f)`.
- `operator==` / `operator!=` — compare two `Either`s (ok==ok when values match,
  err==err when errors match).
- `operator bool` (explicit) — `if (e)` means `if (e.is_ok())`.
- `value_or(e, fallback)` — the value, or `fallback` on error.
- `match(e, ok_f, err_f)` — see [Pattern matching](pattern-matching.md).

```cpp
fp::ok(3) == fp::ok(3);          // true
fp::ok(3) != fp::err<int>("x");  // true
if (fp::ok(1)) { /* succeeded */ }
fp::value_or(fp::err<int>("x"), 0);   // 0
```

### `Either<E, void>`

When the success side carries no value, use `void`:

```cpp
fp::Either<std::string, void> done = fp::Either<std::string, void>::ok();
fp::Either<std::string, void> bad  = fp::Either<std::string, void>::err("nope");
done.is_ok();  // true
```

This is what `Result<void>` / `write_file` use.

## `Result<T>` — a value or a string error

```cpp
#include <fp/result.hpp>

fp::Result<int> ok_val = fp::ok(42);
fp::Result<int> err_val = fp::err<int>("division by zero");

ok_val.is_ok();           // true
err_val.error();          // "division by zero"
```

Construction helpers: `fp::ok(v)`, `fp::ok<T>()` (for `Result<void>`), and
`fp::err<T>("message")`.

`fp::fail("message")` is a *shortcut* that converts to any `Result<T>` (or
`Validation<T>`), so you don't have to name `T`:

```cpp
fp::Result<Person> parse(std::string const& line) {
    if (bad) return fp::fail("expected 'name,age'");   // no <Person>!
    return fp::ok(Person{...});
}
```

Everything from `Either` applies (it's an alias), plus these `Result`-specific
combinators:

```cpp
// sequence: all succeed -> the vector, else the first error
std::vector<fp::Result<int>> rs = { fp::ok(1), fp::ok(2) };
fp::Result<std::vector<int>> all = fp::sequence(rs);       // ok({1,2})

// traverse: map that may fail, then sequence
fp::Result<std::vector<int>> r2 =
    fp::traverse(std::vector<int>{1,2,3}, [](int x) { return fp::ok(x * 10); });

// from_optional: attach an error message to an optional
fp::Result<int> r3 = fp::from_optional(std::optional<int>{}, "missing");

// transpose: swap the layers
fp::Result<std::optional<int>> r4 = fp::transpose(std::optional<fp::Result<int>>{});

// try_: wrap a throwing callable into a Result
fp::Result<int> r5 = fp::try_([&] { return std::stoi("123"); });   // ok(123)

// combine2: require two successes
fp::Result<std::pair<int, int>> r6 = fp::combine2(fp::ok(1), fp::ok(2));

// context: prefix the error message on failure
fp::Result<int> r7 = fp::context(fp::err<int>("e"), "while loading: ");

// collect_all: gather ALL results, join their errors
fp::Result<std::vector<int>> r8 = fp::collect_all(rs);
```

### Escaping `Result`

```cpp
int v = fp::unwrap(fp::ok(7));          // 7
// fp::unwrap(fp::err<int>("x"));       // throws std::runtime_error("unwrap on error: x")

#if defined(__cpp_lib_expected)          // C++23
auto e  = fp::to_expected(fp::ok(5));   // std::expected<int, std::string>
auto r9 = fp::from_expected(e);
#endif
```

`unwrap` throws `std::runtime_error` with the stored message on failure;
`to_expected` / `from_expected` bridge to `std::expected` on C++23.

### Observing without escaping: taps

Sometimes you want a side effect on one outcome (log the error, record a metric)
without changing the value or the control flow. `tap_ok` / `tap_err` run a
callback on the relevant side and return the container unchanged:

```cpp
auto r = fp::tap_err(fp::err<int>("disk full"),
                     [](std::string const& e) { log(e); });   // still err("disk full")
auto v = fp::tap_ok(fp::ok(42),
                    [](int x) { metrics.record(x); });          // still ok(42)
```

`fp::fail("...")` is a short name for an error of the *inferred* type when the
target is known from context:

```cpp
fp::Result<int> r1 = fp::fail("bad");                 // err("bad")
fp::Validation<int> v1 = fp::fail("bad");             // err({"bad"})
```

## `std::optional<T>` — a value that may be absent

```cpp
#include <fp/maybe.hpp>

fp::map(std::optional<int>{3}, [](int x) { return x + 1; });       // optional{4}
fp::map(std::optional<int>{},  [](int x) { return x + 1; });       // nullopt

fp::and_then(std::optional<int>{3}, [](int x) { return std::optional<int>{x*2}; });

fp::or_else(std::optional<int>{}, 42);               // 42 (value fallback)
fp::or_else(std::optional<int>{}, [] { return 42; }); // 42 (lazy fallback)
fp::value_or_lazy(std::optional<int>{}, [] { return 42; });

fp::filter(std::optional<int>{5}, [](int x) { return x > 3; });    // optional{5}
fp::flatten(std::optional<std::optional<int>>{{5}});                // optional{5}

// apply: applicative — call a maybe-function with a maybe-argument
auto of = std::optional<int(*)(int)>{ [](int x) { return x + 1; } };
fp::apply(of, std::optional<int>{1});                              // optional{2}

// collect: all present -> vector, else nullopt
std::vector<std::optional<int>> os = { std::optional<int>{1}, std::optional<int>{2} };
fp::collect(os);                                                   // optional{{1,2}}

// bind sugar
std::optional<int> bound = std::optional<int>{1} >>= [](int x) { return std::optional<int>{x + 1}; };
```

> Note: `operator>>=` for `std::optional` is defined in `fp`. Because
> `std::optional` lives in `namespace std`, ADL will not find `fp::operator>>=`
> automatically — write `using fp::operator>>=;` or call
> `fp::operator>>=(o, f)` explicitly. For `Either`/`Result`/`Validation` (which
> live in `fp`), `x >>= f` works directly.

## `Validation<T>` — accumulate *all* the errors

Unlike `Result` (which stops at the first error), `Validation` collects every
error and reports them together. The failure side is a
`std::vector<std::string>`.

```cpp
#include <fp/validation.hpp>

fp::Validation<int> good = fp::valid(42);
fp::Validation<int> bad  = fp::invalid("must be positive");
fp::Validation<int> bad2 = fp::invalid(std::vector<std::string>{"a", "b"});
```

```cpp
// check / ensure: build a Validation from a predicate
auto checked = fp::check([](int x) { return x > 0; }, "must be positive", -1);
auto ensured = fp::ensure(x > 0, "must be positive", x);

// combine2: two successes -> one; errors accumulate
fp::Validation<std::pair<int,int>> p =
    fp::combine2(bad, bad2, [](int a, int b) { return std::pair{a, b}; });
// p.error() == {"must be positive", "a", "b"}

// combine: n-ary, variadic
auto c = fp::combine([](auto... xs) { return (xs + ...); }, good, good, good);

// validate_all: accumulate over a vector
std::vector<fp::Validation<int>> vs = { fp::valid(1), bad, fp::valid(3) };
auto va = fp::validate_all(vs);         // error side holds {"must be positive"}

// traverse: map-may-fail over a vector, accumulating errors
// validate_none / validate_any: whole-vector predicates
auto vn = fp::validate_none(std::vector<int>{1,2,3}, [](int x){ return x < 0; }, "negative");

// merge: pick the first success, else combine errors
// to_result: collapse to Result (takes the first error)
fp::Result<int> rr = fp::to_result(good);
```

## Chaining it all together

```cpp
#include <fp/all.hpp>

fp::Result<int> parse_and_double(std::string const& s) {
    auto n = fp::str::to_int(s);                 // Result<int>
    auto positive = fp::and_then(n, [](int x) {
        return x >= 0 ? fp::ok(x) : fp::err<int>("negative");
    });
    return fp::map(positive, [](int x) { return x * 2; });
}
// or, with bind:
fp::Result<int> parse_and_double2(std::string const& s) {
    return fp::str::to_int(s)
        >>= [](int x) { return x >= 0 ? fp::ok(x) : fp::err<int>("negative"); }
        >>= [](int x) { return fp::ok(x * 2); };
}
```

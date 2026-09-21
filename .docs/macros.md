# Macros — `macros.hpp`

ForgeFP is macro-free by design, with **one** opt-in exception: the `FP_TRY`
family (`FP_TRY`, `FP_TRY_VALUE`, `FP_TRY_VOID`). They do the one thing the
type system and expressions cannot express: early `return` from the enclosing
function.

**Opt-in**: `macros.hpp` is *not* in `all.hpp` — include it explicitly.

```cpp
#include <fp/macros.hpp>
```

**Why it's an exception:** the library prefers named functions over macros
(macros pollute the global namespace and have no scope). But `FP_TRY` does
something no function can — `return` from the *caller's* function. That
early-return-on-error is the one ergonomic a function can't provide, so it
earns a macro, kept isolated and opt-in.

## The theory: do-notation for C++

Chaining fallible steps with `>>=` is already a straight line:

```cpp
fp::Result<int> parse_and_add(std::string const &a, std::string const &b) {
  return fp::str::to_int(a) >>= [&](int x) {
    return fp::str::to_int(b) >>= [&](int y) {
      return fp::ok(x + y);
    };
  };
}
```

The nesting grows with every step. `FP_TRY` gives the same short-circuit
semantics with no nesting — the equivalent of Haskell's `do` block or Rust's
`?`:

```cpp
fp::Result<int> parse_and_add(std::string const &a, std::string const &b) {
  int x = FP_TRY(fp::str::to_int(a));   // error -> return err(...) from parse_and_add
  int y = FP_TRY(fp::str::to_int(b));
  return fp::ok(x + y);
}
```

Semantics are exactly `>>=`: on success the value is bound, on failure the
error is returned from the enclosing function, and the remaining steps are
skipped.

## The three forms

| Form | Context | Value |
|---|---|---|
| `FP_TRY(expr)` | expression (`int x = FP_TRY(…)`) | value or propagate — GCC/Clang only |
| `FP_TRY_VALUE(name, expr)` | statement | declares `name` with the value, or propagates |
| `FP_TRY_VOID(expr)` | statement | discards the value, propagates failures |

```cpp
fp::Result<int> parse_and_add(std::string const &a, std::string const &b) {
  FP_TRY_VALUE(x, fp::str::to_int(a));
  FP_TRY_VALUE(y, fp::str::to_int(b));
  return fp::ok(x + y);
}

fp::Result<void> step() {
  FP_TRY_VOID(open_connection());        // propagate a Result<void> failure
  return fp::ok<void>();
}
```

`FP_TRY` is an expression, which needs GCC/Clang *statement expressions*. For
code that must compile everywhere (including MSVC), use the statement forms —
they share the same error-propagation semantics. On MSVC, using `FP_TRY`
produces a diagnostic pointing at `FP_TRY_VALUE`/`FP_TRY_VOID`.

## It works with every error style

On failure the macro returns a small error *propagator* that converts to
whatever the enclosing function returns, so the same code works with `Result`,
`Validation`, and `Outcome`:

```cpp
// Result<T>
fp::Result<int> r() {
  int x = FP_TRY(fp::str::to_int("21"));
  return fp::ok(x * 2);
}

// Validation<T>: failures accumulate into the vector<string> side
fp::Validation<int> v() {
  int x = FP_TRY(fp::str::to_int("21"));
  int y = FP_TRY(fp::str::to_int("bad"));
  return fp::valid(x + y);
}

// Outcome<T>: the error keeps its code and context chain
fp::Outcome<int> o() {
  int x = FP_TRY(fp::str::to_int("21"));
  return fp::with_context(fp::Outcome<int>::ok(x), "parsing");
}
```

That conversion is why `FP_TRY` is more than a `goto on error`: the error type
adapts to the function's declared error channel.

## `FP_TRY` vs `>>=`

| | `>>=` | `FP_TRY` |
|---|---|---|
| Nesting for N steps | grows | flat |
| Needs lambdas | yes (captures) | no |
| Error type adapts | no (must match) | yes (Result/Validation/Outcome) |
| Portability | every compiler | `FP_TRY` needs GCC/Clang; the `_VALUE`/`_VOID` forms are portable |
| Macro in scope | no | yes (opt-in header) |

Use `>>=` for two or three steps and for pipelines you build as values; use
`FP_TRY` for long straight-line functions where the nesting would hurt.

## Implementation notes

- `FP_TRY` is a statement expression `({ ... })` on GCC/Clang.
- `FP_TRY_VALUE`/`FP_TRY_VOID` are ordinary statement macros and work on every
  compiler.
- Everything is `fp::`-qualified, so no `using namespace fp;` is needed.
- Use them only where a `return` is valid (inside a function returning
  `Result<T>` / `Validation<T>` / `Outcome<T>`).

## When *not* to use it

- **In library headers.** A macro leaks into every translation unit that
  includes your header. Keep `FP_TRY` in application `.cpp` files; use `>>=`
  in headers you distribute.
- **When the function returns a plain value.** There is nothing to propagate
  into; handle the error locally.
- **When you want to log and continue.** `FP_TRY` returns immediately; a
  `tap_err` + explicit branch is the right shape when you must observe the
  failure first:

  ```cpp
  auto r = fp::str::to_int(s);
  if (!r.is_ok()) {
    log(r.error());
    return fp::ok(0);                 // recover, do not propagate
  }
  ```

## Gotchas

- **The enclosing function's return type must be convertible from the
  propagator.** In practice: it must return `Result<T>`, `Validation<T>`, or
  `Outcome<T>` (not a plain `T`, not `std::optional`).
- **`FP_TRY` needs GCC/Clang statement expressions.** Use the statement forms
  for portability; do not hide `FP_TRY` inside another macro.
- **Variable names matter for `FP_TRY_VALUE`.** The macro declares `name` in
  the current scope; pick a fresh name (`x`, not `r`, if `r` exists).
- **No cleanup on propagation beyond RAII.** Scope guards
  ([`fp::defer`](scope.md)) and owning types still run — `FP_TRY` is a plain
  `return`, so stack unwinding behaves normally.
- **Don't use it in a lambda returning `void`.** There is nothing to convert
  the error to; use `>>=` or an explicit branch.

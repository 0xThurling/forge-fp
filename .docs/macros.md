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

## `FP_TRY` — the `?` operator

`FP_TRY(expr)` evaluates `expr` (a `Result<T>`); on error it **returns** an
error from the enclosing function, otherwise it yields the value.

```cpp
#include <fp/macros.hpp>

fp::Result<int> parse_and_add(std::string const& a, std::string const& b) {
    int x = FP_TRY(fp::str::to_int(a));   // error -> return err(...) from parse_and_add
    int y = FP_TRY(fp::str::to_int(b));
    return fp::ok(x + y);
}
```

This is do-notation: a chain of fallible steps reads as a straight line instead
of nested `if (!r.is_ok()) return err(...)`. Without it, the equivalent is:

```cpp
Result<int> r = str::to_int(a);
if (!r.is_ok())
    return err<int>(r.error());
int x = r.value();
```

one block *per fallible step* — `FP_TRY` collapses each block to one line.

## `FP_TRY_VALUE` / `FP_TRY_VOID` — the portable forms

`FP_TRY` is an expression, which needs GCC/Clang *statement expressions*. For
code that must compile everywhere (including MSVC), use the statement forms —
they share the same error-propagation semantics:

```cpp
fp::Result<int> parse_and_add(std::string const& a, std::string const& b) {
    FP_TRY_VALUE(x, fp::str::to_int(a));   // declares `x`, or returns the error
    FP_TRY_VALUE(y, fp::str::to_int(b));
    return fp::ok(x + y);
}

fp::Result<int> step() {
    FP_TRY_VOID(open_connection());        // propagate a Result<void> failure
    return fp::ok(0);
}
```

| Form | Context | Value |
|---|---|---|
| `FP_TRY(expr)` | expression (`int x = FP_TRY(…)`) | value or propagate — GCC/Clang only |
| `FP_TRY_VALUE(name, expr)` | statement | declares `name` with the value, or propagates |
| `FP_TRY_VOID(expr)` | statement | discards the value, propagates failures |

## Implementation notes

- `FP_TRY` is a statement expression `({ ... })` on GCC/Clang. On MSVC, using it
  produces a diagnostic pointing at `FP_TRY_VALUE`/`FP_TRY_VOID`.
- `FP_TRY_VALUE`/`FP_TRY_VOID` are ordinary statement macros and work on every
  compiler.
- On failure the macro returns a small error *propagator* that converts to
  whatever the enclosing function returns — `Result<T>`, `Validation<T>`, or
  `Outcome<T>` — so the macros compose with any error style.
- The statement forms cover `Result<void>` steps through `FP_TRY_VOID`.
- Everything is `fp::`-qualified, so no `using namespace fp;` is needed.
- Use them only where a `return` is valid (inside a function returning
  `Result<T>` / `Validation<T>` / `Outcome<T>`).

## When *not* to use it

If you'd rather not pull in the macro, the explicit form above is always
available and is what you should use in headers you distribute. Use `FP_TRY` in
application code where the chaining is worth the macro, and keep it out of
library headers so consumers don't inherit a macro from you.

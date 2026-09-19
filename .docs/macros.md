# Macros — `macros.hpp`

ForgeFP is macro-free by design, with **one** opt-in exception: `FP_TRY`. It's
the only thing the type system and expressions cannot express (early `return`
from the enclosing function).

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

## Implementation notes

- GCC/Clang: a statement expression `({ ... })`. MSVC: an immediately-invoked
  lambda. Both are behind `#if defined(_MSC_VER)`.
- On failure the macro returns a small error *propagator* that converts to
  whatever the enclosing function returns — `Result<T>` or `Validation<T>` —
  so `FP_TRY` composes with either error style.
- `FP_TRY` also accepts a `Result<void>` step: `FP_TRY(step());` propagates the
  error without binding a value.
- Everything is `fp::`-qualified, so no `using namespace fp;` is needed.
- Use it only where a `return` is valid (inside a function returning
  `Result<T>` / `Validation<T>`).

## When *not* to use it

If you'd rather not pull in the macro, the explicit form above is always
available and is what you should use in headers you distribute. Use `FP_TRY` in
application code where the chaining is worth the macro, and keep it out of
library headers so consumers don't inherit a macro from you.

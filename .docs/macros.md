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
using namespace fp;            // FP_TRY expands to unqualified `err`, so bring it in

Result<int> parse_and_add(std::string const& a, std::string const& b) {
    int x = FP_TRY(str::to_int(a));   // error -> return err(...) from parse_and_add
    int y = FP_TRY(str::to_int(b));
    return ok(x + y);
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
- The value type is deduced with `std::remove_cvref_t`, so `FP_TRY` works for
  move-only `T` (e.g. `std::unique_ptr`) and moves out of the temporary.
- `FP_TRY` expands to an unqualified `err<...>(...)`, so it must be used inside
  `namespace fp` or after `using namespace fp;` / `using fp::err;`.
- Use it only where a `return` is valid (inside a function returning
  `Result<T>`, where `T` matches the extracted value).

## When *not* to use it

If you'd rather not pull in the macro, the explicit form above is always
available and is what you should use in headers you distribute. Use `FP_TRY` in
application code where the chaining is worth the macro, and keep it out of
library headers so consumers don't inherit a macro from you.

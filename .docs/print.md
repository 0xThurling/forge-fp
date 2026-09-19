# Printing — `print.hpp`

`operator<<` for the ADTs, so you can drop them straight into a stream when
debugging or logging.

```cpp
#include <fp/print.hpp>
#include <iostream>

std::cout << fp::ok(3) << "\n";                 // ok(3)
std::cout << fp::err<int>("boom") << "\n";       // err(boom)
std::cout << fp::valid(5) << "\n";               // ok(5)
std::cout << fp::invalid<int>("bad") << "\n";    // err(bad)
std::cout << fp::ok<void>() << "\n";             // ok()
```

**Opt-in.** `print.hpp` is *not* in `all.hpp` — include it explicitly. It pulls
in `<ostream>`, which the ADT headers deliberately avoid.

## What it covers

| Type | Output |
|---|---|
| `Result<T>` / `Either<E, T>` | `ok(value)` or `err(error)` |
| `Result<void>` / `Either<E, void>` | `ok()` or `err(error)` |
| `Validation<T>` | `ok(value)` or `err(msg1; msg2; …)` |

The value/error are streamed with `<<`, so the payload must itself be
printable. `Validation`'s error side (a `vector<string>`) is joined with `; `.

## Notes

- To stringify instead of printing, send it through `std::ostringstream`:

  ```cpp
  std::ostringstream os;
  os << fp::ok(42);
  std::string s = os.str();   // "ok(42)"
  ```

- There's no overload for `std::optional` (it lives in `std`); use `fp::match`
  or `value_or` to render it.

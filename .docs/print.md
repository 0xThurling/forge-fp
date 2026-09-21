# Printing — `print.hpp`

`operator<<` for the ADT family (`Result`, `Either`, `Validation`, `Outcome`,
`Error`), so an error value drops straight into a stream when debugging or
logging.

```cpp
#include <fp/print.hpp>
#include <iostream>

std::cout << fp::ok(3) << "\n";                 // ok(3)
std::cout << fp::err<int>("boom") << "\n";      // err(boom)
std::cout << fp::valid(5) << "\n";              // ok(5)
std::cout << fp::invalid<int>("bad") << "\n";   // err(bad)
std::cout << fp::ok<void>() << "\n";            // ok()
```

**Why this exists:** a `Result` is a `std::variant` underneath, so printing it
without an overload gives you `variant(3)` — or fails to compile. The whole
point of error-as-value is that you can *observe* the value, and observation
starts with being able to print it.

## What it covers

| Type | Output |
|---|---|
| `Result<T>` / `Either<E, T>` | `ok(value)` or `err(error)` |
| `Result<void>` / `Either<E, void>` | `ok()` or `err(error)` |
| `Validation<T>` | `ok(value)` or `err(msg1; msg2; …)` |
| `Outcome<T>` (`Either<Error, T>`) | `ok(value)` or `err(<message chain>)` |
| `Error` | the flattened context chain (`to_string`) |

The value and error are themselves streamed with `<<`, so the payload must be
printable. `Validation`'s error side (a `vector<string>`) is joined with `"; "`.
`Outcome`'s error side uses `fp::to_string`, so the whole cause chain is shown
in one line.

## Examples

### 1. Debugging a fallible chain

```cpp
auto r = fp::str::to_int("12")
    >>= [](int x) { return fp::ok(x * 2); }
    >>= [](int x) { return x < 0 ? fp::err<int>("negative") : fp::ok(x); };

std::cout << r << "\n";       // ok(24)

std::cout << (fp::str::to_int("x")) << "\n";   // err(not a number)
```

One `<<` shows both the success value and the error message — no `.is_ok()`
branch needed for diagnostics.

### 2. Logging a failure with its context chain

```cpp
fp::Outcome<int> load_config(std::string const &path) {
  return fp::with_context(
      fp::from_result(fp::read_file(path), fp::errc::not_found),
      "loading config");
}

auto config = load_config("server.conf");
if (!config.is_ok())
  std::cerr << "fatal: " << config << "\n";
// fatal: err(loading config: cannot open server.conf)
```

Because `Outcome`'s printer calls `fp::to_string(error)`, the chain arrives in
the log already formatted.

### 3. Validation: every problem at once

```cpp
auto v = fp::invalid<int>(std::vector<std::string>{"name required", "age invalid"});
std::cout << v << "\n";     // err(name required; age invalid)
```

### 4. Stringifying instead of printing

```cpp
std::ostringstream os;
os << fp::ok(42);
std::string s = os.str();   // "ok(42)"
```

Useful for building log lines with `fp::str::join` or for tests that assert on
the rendered form.

### 5. Tests read better

```cpp
std::ostringstream os;
os << fp::err<int>("boom");
EXPECT_EQ(os.str(), "err(boom)");
```

## Custom payload types

The overloads are templates: if your payload has an `operator<<`, the ADT
printer picks it up.

```cpp
struct Point { int x, y; };
std::ostream &operator<<(std::ostream &os, Point const &p) {
  return os << "(" << p.x << ", " << p.y << ")";
}

std::cout << fp::ok(Point{1, 2}) << "\n";     // ok((1, 2))
std::cout << fp::err<Point>("no point") << "\n";  // err(no point)
```

For `Validation<T>`, the *error* type is fixed (`vector<string>`), so only the
value needs to be printable.

## Where it is (and isn't)

`print.hpp` **is** included by `all.hpp`; the ADT headers themselves avoid
`<ostream>`, so if you include only `either.hpp`/`result.hpp` and want `<<`,
include `print.hpp` explicitly.

## Gotchas

- **The payload must be streamable.** `fp::Result<std::vector<Widget>>` prints
  only if `Widget` has an `operator<<`. Otherwise you get a compile error deep
  in the template — define the operator or print the unwrapped value.
- **There is no overload for `std::optional`.** It lives in `std`, so the
  library cannot add one portably. Use `fp::match`, `fp::value_or`, or a small
  helper:

  ```cpp
  std::cout << fp::match(maybe_name,
      [](std::string const &s) { return s; },
      [] { return std::string("<none>"); });
  ```

- **Formatting is for humans, not parsing.** `err(boom)` is not a stable wire
  format; use `fp::serialize`/`fp::str::join` when a machine reads it.
- **`operator<<` does not evaluate anything.** Printing a `Result` does not
  run the chain or touch side effects; it only renders the current state.
- **Include order does not matter**, but the operator lives in namespace `fp`,
  so ADL finds it for `fp::` types without `using namespace fp`.

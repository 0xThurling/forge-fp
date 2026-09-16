# Getting started

## Requirements

- C++20 (the library uses concepts, ranges, `std::variant`, designated
  initializers-free aggregates).
- A C++20 compiler (GCC ≥ 10, Clang ≥ 10, MSVC ≥ 19.28).
- Nothing else — no CMake package, no third-party dependency.

## Include

The headers live in `src/fp/` (or the installed mirror `include/forgefp/fp/`).

```cpp
#include <fp/all.hpp>     // everything except simd.hpp and macros.hpp
```

Individual modules are also fine and keep compile times down:

```cpp
#include <fp/result.hpp>
#include <fp/vec.hpp>
#include <fp/string.hpp>
```

Everything is in `namespace fp` except the string helpers, which are in
`namespace fp::str`.

## Build

Compile directly:

```bash
g++ -std=c++20 -I src -o app app.cpp
```

or use CMake (installs `include/forgefp/fp/`):

```bash
cmake -B build && cmake --build build
```

## First program

```cpp
#include <fp/all.hpp>
#include <iostream>

int main() {
    // The pipe: into -> transform -> transform -> out
    auto answer = fp::out(fp::into(3)
        | [](int x) { return x * x; }        // 9
        | [](int x) { return x + 33; }       // 42
        | fp::tap([](int x) { std::cout << x << "\n"; }));  // prints 42
}
```

## The two idioms you will use most

### 1. Pipe values through unary functions

`fp::into(x) | f | g | fp::out` threads a value left-to-right through unary
functions (see [Function composition](composition.md)).

### 2. Chain fallible values through `map` / `and_then`

```cpp
fp::Result<int> r = fp::ok(21);
auto doubled = fp::map(r, [](int x) { return x * 2; });   // ok(42)
auto chained = r >>= [](int x) { return fp::ok(x + 1); }; // ok(22), monadic bind
```

`map` transforms the success value; `and_then` (or `>>=`) lets the *next* step
also fail.

## Conventions to remember

- `fp::map(v, f)` / `fp::filter(v, p)` / `fp::fold_left(v, init, op)` work on
  both `std::vector` and any range — see [Collections](collections.md).
- `fp::ok(x)` / `fp::err<T>("why")` construct `Result<T>`; `fp::valid(x)` /
  `fp::invalid("why")` construct `Validation<T>`.
- Errors never throw unless you call `fp::unwrap` / `fp::expect`.
- Named operators (`fp::plus`, `fp::gt`, …) replace tiny lambdas — see
  [Function composition](composition.md).

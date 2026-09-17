# Getting started

## Requirements

- C++20 (the library uses concepts, ranges, `std::variant`, and `std::span`).
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

## The mental model

ForgeFP is **small, pure functions you compose**. There is no class hierarchy,
no inheritance, no runtime polymorphism — values flow through free functions.

Three shapes recur everywhere:

1. **Collections** (`std::vector<T>`, any range). Transformed by
   `map`/`filter`/`fold_left`/`zip`, always returning a *fresh* collection.
   Nothing is mutated in place unless the name says `*_inplace`.

2. **Wrappers** (`Result<T>`, `Either<E,T>`, `std::optional<T>`,
   `Validation<T>`). Chained with `map`/`and_then`/`or_else`, short-circuiting
   on the "failure" side. The value *may not be there*, and that's part of the
   type, not a special case.

3. **Functions.** Combined with `compose`/`pipe`/`curry`/`|` and the named
   operators in `fp::ops`.

The single biggest idea — the one that makes the library hang together — is
**error (or absence) as a value**. Instead of exceptions and `nullptr`
checks, failures are ordinary values you pass through `map`/`and_then`, so a
whole computation reads as a straight-line chain with no error-checking
branches.

```cpp
// Every step may fail; no `if` anywhere.
fp::Result<int> r = fp::str::to_int(s)
    >>= [](int x) { return x > 0 ? fp::ok(x) : fp::err<int>("negative"); }
    >>= [](int x) { return fp::ok(x * 2); };
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

## Naming conventions

| Pattern | Meaning | Example |
|---|---|---|
| `ok` / `err` | build a `Result` | `fp::ok(42)`, `fp::err<int>("why")` |
| `valid` / `invalid` | build a `Validation` | `fp::valid(x)`, `fp::invalid("why")` |
| trailing `_` | avoid a C++ keyword/`std` name | `fp::and_`, `fp::or_`, `fp::not_`, `fp::char_`, `fp::string_` |
| `*_inplace` | mutates its argument | `map_inplace`, `par_map_inplace` |
| `try_` | wraps a throwing call in a `Result` | `fp::try_([&]{ ... })` |
| `map`/`and_then`/`or_else` | the shared ADT vocabulary | (works on `Result`, `optional`, `Either`, `Validation`) |

## Conventions to remember

- `fp::map(v, f)` / `fp::filter(v, p)` / `fp::fold_left(v, init, op)` work on
  both `std::vector` and any range — see [Collections](collections.md).
- Errors never throw unless you call `fp::unwrap` / `fp::expect`.
- Named operators (`fp::plus`, `fp::gt`, …) replace tiny lambdas — see
  [Function composition](composition.md).
- `simd.hpp` and `macros.hpp` are opt-in (not in `all.hpp`): include them
  explicitly.

## Reading the API

Because everything is a free function, you can infer most signatures:

- A function starting with `map`/`filter`/`fold`/`zip`/`scan` takes a
  collection (or range) and a function.
- A function returning `Result<T>`/`Validation<T>`/`optional<T>` is fallible;
  chain it with `>>=`/`and_then`.
- A function taking an `Either<E,T>`/`Result<T>`/`optional<T>` is a combinator
  on the *wrapper*, not the value.

When in doubt, the doc for each module lists every function and its return
type — see the table in [the index](README.md).

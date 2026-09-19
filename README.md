# ForgeFP

A header-only, C++20 functional programming library with zero external
dependencies. ForgeFP brings typical functional tools to modern C++: algebraic
data types (`Either`, `Result`, `Validation`), optional composition, vector/range
combinators, parser combinators, function composition and currying, named
operators, string utilities, file & input I/O, an arena allocator, SIMD mapping,
and concurrency helpers (thread pool, channels, actors, streams).

```cpp
#include <fp/all.hpp>
#include <iostream>

int main() {
    // pipe a value through unary functions
    auto answer = fp::out(fp::into(3)
        | [](int x) { return x * x; }                     // 9
        | [](int x) { return x + 33; }                    // 42
        | fp::tap([](int x) { std::cout << x << "\n"; })); // prints 42
}
```

```cpp
// chain fallible steps without a single `if`
fp::Result<int> r = fp::str::to_int("21")
    >>= [](int x) { return x > 0 ? fp::ok(x) : fp::err<int>("negative"); }
    >>= [](int x) { return fp::ok(x * 2); };   // ok(42)
```

---

## Requirements

| Requirement | Version |
|---|---|
| C++ standard | C++20 |
| Compiler | GCC 11+ or Clang 14+ |
| Threading | `-pthread` when using `concurrent.hpp` |
| `simd.hpp` | a target with `native_simd` (x86-64 / ARM64, GCC libstdc++) |

The library is header-only — nothing to link.

---

## Getting the headers

Two byte-identical trees, one header per module:

- **`src/fp/`** — the full library. Include as `#include <fp/all.hpp>` (umbrella)
  or one module (`#include <fp/vec.hpp>`), with `-I src`.
- **`include/forgefp/fp/`** — the installed mirror (same files, byte-identical).
  Include as `#include <forgefp/fp/all.hpp>`.

```bash
g++ -std=c++20 -I src my_program.cpp -pthread
```

Everything lives in `namespace fp` (string helpers in `namespace fp::str`).

`all.hpp` covers **every** module except the two opt-ins — `simd.hpp` and
`macros.hpp` — which you include explicitly.

---

## Building / installing

```bash
# Forge
forge build

# Or plain CMake
cmake -S . -B build && cmake --build build
```

There are no `.cpp` files in `src/`, so ForgeFP installs as a header-only
**INTERFACE** library:

```cmake
find_package(forgefp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE forgefp)
```

…or just copy `src/fp/` into your project.

---

## Modules

| Header | Provides |
|---|---|
| `all.hpp` | umbrella — everything below except `simd.hpp` / `macros.hpp` |
| `adt.hpp` | `match`, `case_`, `cond`, `when`, `otherwise`, `overload`, `unpack`, `value_or` |
| `arena.hpp` | `Arena` bump allocator + `with_arena` |
| `combinators.hpp` | `identity`, `const_`, `flip`, `on`, `compose`, `fix`, `apply`, `when`/`unless`, `first`/`second`, `pipe_with` |
| `compose.hpp` | `compose`, `pipe`, `into`/`out`, `operator\|`, `tap` |
| `concurrent.hpp` | `ThreadPool`, `par_map`/`par_for_each`/`par_reduce`, `Channel`, `RingBuffer`, `Actor`, `Async`, `async_map`/`async_sequence`, `race`/`timeout`/`retry` |
| `curry.hpp` | `curry`, `uncurry` |
| `either.hpp` | `Either<E, T>` (incl. `Either<E, void>`) + `map`/`and_then`/`or_else`/`map_error`/`flatten`/`bimap`/`swap`/`to_optional`/`rights`/`lefts`/`expect`/`ok_or`/`tap_err`/`tap_ok`/`>>=` |
| `grid.hpp` | `map2d`/`map3d`, `transpose`, `flatten`, `for_each_index`, `tabulate`, `cartesian_product` |
| `input.hpp` | `read_line`/`read_all`/`read_lines`/`read_char`/`read_chars`/`feed_lines`, POSIX `raw_mode`/`read_key` |
| `io.hpp` | `read_file`, `read_lines`, `write_file` |
| `macros.hpp` | `FP_TRY` *(opt-in)* |
| `map.hpp` | `lookup`, `map_values`, `filter_values`, `merge_with`, `keys`, `values`, `to_map`, `to_unordered_map` (all generic over `std::map`/`std::unordered_map`) |
| `maybe.hpp` | `std::optional` combinators: `map`/`and_then`/`or_else`/`filter`/`flatten`/`apply`/`collect`/`value_or_lazy`/`>>=` |
| `memoize.hpp` | `memoize<Arg>(f)`, `memoize2<A,B>(f)`, `memoizeN<Args...>(f)` |
| `ops.hpp` | named operators: `plus`/`minus`/`times`/`divide`, `eq`/`ne`/`lt`/`le`/`gt`/`ge`, `and_`/`or_`/`not_`, `negate`/`increment`/`decrement` |
| `parse.hpp` | `Parser<T>` + primitives/sequencing/choice/lexemes (`eof`/`peek`/`not_followed`/`label`/`context`/`many1`/`chainl1`), position-carrying errors, operators (`>>`, `<<`, `\|`, `>>=`, `*`, `%`) |
| `print.hpp` | `operator<<` for `Result`/`Either`/`Validation` |
| `ranges.hpp` | range-generic `map`/`filter`/`fold_left`/`fold_right`/`scan`/`zip`/`enumerate`/`group_by`/`chunk`/`windows`/`flat_map`/`filter_map`/`take_while`/`drop_while`/`unique`/`sort`/`sort_by`/`partition`/`span` + curried stages for `into(…) \| …` pipelines |
| `result.hpp` | `Result<T>` + `ok`/`err`, `sequence`/`traverse`/`transpose`/`try_`/`combine2`/`context`/`collect_all`/`unwrap`, `std::expected` bridge (C++23) |
| `simd.hpp` | `vec<T>`, `map_inplace`/`map_to`/`map_inplace_fixed`, `reduce`/`dot`, `map_sqrt`/`map_exp`, `clamp_inplace`/`normalize`/`threshold_inplace`, `par_map_inplace` *(opt-in)* |
| `stream.hpp` | `Stream<T>` (pull/push `map`/`filter`/`subscribe`/`collect`/`take`/`take_while`/`scan`/`fold_left`/`concat`) |
| `string.hpp` | `fp::str`: `split`/`split_view`/`join`/`trim`/`to_lower`/`to_upper`/`to_int`/`to_double`/… |
| `validation.hpp` | `Validation<T>` + `valid`/`invalid`/`validate_all`/`combine`/`combine2`/`check`/`ensure`/`traverse`/`to_result` |
| `vec.hpp` | `map`/`filter`/`zip`/`zip_with`/`group_by`/`partition`/`chunk`/`sort`/`sort_by`/`sum`/`product`/`scan`/`range`/… |

---

## Core ideas

Three shapes recur through the whole library:

1. **Collections** (`std::vector<T>`, any range) — transformed by
   `map`/`filter`/`fold_left`/`zip`, always returning a fresh collection.
2. **Wrappers** (`Result<T>`, `Either<E,T>`, `std::optional<T>`,
   `Validation<T>`) — chained with `map`/`and_then`/`or_else`, short-circuiting
   on the failure side. Failure is a *value*, not an exception.
3. **Functions** — combined with `compose`/`pipe`/`|`/`curry` and the named
   operators in `fp::ops`.

The one idea that ties them together: **error (or absence) as a value**. Instead
of exceptions and `nullptr` checks, failures are ordinary values you pass
through `map`/`and_then`, so a fallible computation reads as a straight-line
chain with no error branches.

---

## Documentation, practice, and guided projects

- **[`.docs/`](.docs/README.md)** — the usage reference: how everything works,
  worked examples for every module.
- **[`.practice/`](.practice/README.md)** — ~40 assertion-based exercises,
  one combinator at a time.
- **[`.guided/`](.guided/README.md)** — 10 full projects built stage by stage,
  from a CSV analyzer up to a rotating 3D shape in the terminal.

---

## Notes

- **Header-only, no ABI** — everything is templates or `inline`.
- **Opt-ins** — `simd.hpp` and `macros.hpp` are *not* in `all.hpp`; include
  them explicitly.
- **Threads** — `concurrent.hpp` needs `-pthread`.
- **`memoize`** is not thread-safe.
- **`Either` accessors** (`value()`/`error()`) assume the right alternative is
  held; guard with `is_ok()` first, or destructure with `match`.

---

## License

See repository metadata. Built with Forge (see `forge.lua`).

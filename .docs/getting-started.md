# Getting started

## Requirements

- C++20 (concepts, ranges, `std::variant`, `std::span`, `std::source_location`).
- A C++20 compiler (GCC 11+ or Clang 14+).
- Nothing else — header-only, no CMake package required, no third-party
  dependency.
- `-pthread` when you use `concurrent.hpp`/`task.hpp`; a native-SIMD target
  when you use `simd.hpp`.

## Include

The headers live in `src/fp/` (or the installed mirror `include/forgefp/fp/`).

```cpp
#include <fp/all.hpp>     // everything except simd.hpp, autodiff.hpp, macros.hpp
```

Individual modules keep compile times down and make dependencies explicit:

```cpp
#include <fp/result.hpp>
#include <fp/vec.hpp>
#include <fp/string.hpp>
```

Everything is in `namespace fp`, except the string helpers (`fp::str`) and the
autodiff elementary functions (`fp::ad`).

## Build

Compile directly:

```bash
g++ -std=c++20 -I src -o app app.cpp
```

or use CMake (installs `include/forgefp/fp/`):

```bash
cmake -B build && cmake --build build
```

## Module tour

| Module | Header | One-liner |
|---|---|---|
| [ADTs](adts.md) | `either.hpp`, `result.hpp`, `maybe.hpp`, `validation.hpp` | `Result`, `Either`, `optional`, `Validation` + `map`/`and_then`/`or_else` |
| [Structured errors](adts.md#outcomet--structured-errors-with-codes-and-context) | `error.hpp` | `Error`/`Outcome<T>` with codes, context chains, source locations |
| [Pattern matching](pattern-matching.md) | `adt.hpp` | `match`, `case_`, `cond`, `when`, `otherwise`, `overload` |
| [Collections](collections.md) | `vec.hpp`, `ranges.hpp`, `views.hpp` | `map`/`filter`/`fold`/`zip`/`sort` over any range; lazy views |
| [Iteration & in-place](inplace.md) | `inplace.hpp` | allocation-free loops and algorithms |
| [Maps & grids](maps-grids.md) | `map.hpp`, `grid.hpp` | associative helpers, 2D/3D grids |
| [Strings](strings.md) | `string.hpp` | `split`/`join`/`trim`/`to_int`/`to_string` in `fp::str` |
| [Parsing](parsing.md) | `parse.hpp` | parser combinators with position-carrying errors |
| [I/O](io.md) | `io.hpp` | files, lines, bytes, directories as `Result` |
| [Input](input.md) | `input.hpp` | stdin as `Result`/`Stream`/`Channel`, raw keys |
| [Printing](print.md) | `print.hpp` | `operator<<` for the ADT family |
| [Memory & ownership](memory.md) | `memory.hpp`, `arena.hpp` | `Buffer`/`Box`/`Shared`, scoped allocation, bump allocator |
| [Scopes](scope.md) | `scope.hpp` | `defer`/`scope_exit`/`scope_success`/`scope_fail` |
| [Numerics](numerics.md) | `numerics.hpp` | stable `softmax`/`sigmoid`, `linspace`, `central_difference` |
| [Linear algebra](linalg.md) | `linalg.hpp` | `matmul`/`solve`/norms/reductions |
| [Random](random.md) | `random.hpp` | seedable `Rng`, sampling, shuffling |
| [Time](time.md) | `time.hpp` | `Stopwatch`, monotonic seconds |
| [Serialization](serialization.md) | `serialize.hpp` | generic text/binary round-trip |
| [Composition](composition.md) | `compose.hpp`, `curry.hpp`, `combinators.hpp`, `ops.hpp`, `memoize.hpp` | pipes, currying, named operators |
| [Concurrency](concurrency.md) | `concurrent.hpp`, `task.hpp`, `stream.hpp` | thread pool, channels, actors, cancellation, streams |
| [SIMD](simd.md) *(opt-in)* | `simd.hpp` | vectorized map/reduce/dot/math |
| [Autodiff](autodiff.md) *(opt-in)* | `autodiff.hpp` | forward-mode `Dual<T>`, `derivative` |
| [Macros](macros.md) *(opt-in)* | `macros.hpp` | `FP_TRY` early-return |

## First program

```cpp
#include <fp/all.hpp>
#include <iostream>

int main() {
    // pipe a value through unary functions
    auto answer = fp::out(fp::into(3)
        | [](int x) { return x * x; }                       // 9
        | [](int x) { return x + 33; }                      // 42
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
**error (or absence) as a value**. Instead of exceptions and `nullptr` checks,
failures are ordinary values you pass through `map`/`and_then`, so a whole
computation reads as a straight-line chain with no error-checking branches.

```cpp
// Every step may fail; no `if` anywhere.
fp::Result<int> r = fp::str::to_int(s)
    >>= [](int x) { return x > 0 ? fp::ok(x) : fp::err<int>("negative"); }
    >>= [](int x) { return fp::ok(x * 2); };
```

## The error model in one table

| Situation | Type | Chains with |
|---|---|---|
| A value or a message (the default) | `fp::Result<T>` | `>>=`, `map`, `and_then`, `or_else` |
| Two outcomes of arbitrary types | `fp::Either<E, T>` | same vocabulary |
| A value that may be absent, no message | `std::optional<T>` | `fp::map`, `and_then`, `value_or` |
| Collect *every* problem at once | `fp::Validation<T>` | `validate_all`, `combine`, `traverse` |
| Codes, context chains, source locations | `fp::Outcome<T>` | `with_context`, `root_cause`, `to_result` |
| A bug (broken invariant) | `ML_ASSERT` (domain layer) / assert | throws, or aborts in release |

Rule of thumb: **`Result` unless you have a reason not to.** Nothing throws
unless you call `fp::unwrap`/`fp::expect`.

## The two idioms you will use most

### 1. Pipe values through unary functions

`fp::into(x) | f | g | fp::out` threads a value left-to-right through unary
functions (see [Function composition](composition.md)). The pipe is
**type-directed**: for a `Result`/`optional` it maps over the value and
propagates the failure; for a plain value or a `vector` it applies the function
directly.

### 2. Chain fallible values through `map` / `and_then`

```cpp
fp::Result<int> r = fp::ok(21);
auto doubled = fp::map(r, [](int x) { return x * 2; });   // ok(42)
auto chained = r >>= [](int x) { return fp::ok(x + 1); }; // ok(22)
```

`map` transforms the success value; `and_then` (or `>>=`) lets the *next* step
also fail.

## Naming conventions

| Pattern | Meaning | Example |
|---|---|---|
| `ok` / `err` | build a `Result` | `fp::ok(42)`, `fp::err<int>("why")` |
| `valid` / `invalid` | build a `Validation` | `fp::valid(x)`, `fp::invalid("why")` |
| trailing `_` | avoid a C++ keyword/`std` name | `fp::and_`, `fp::or_`, `fp::not_`, `fp::min_`, `fp::max_` |
| `*_inplace` | mutates its argument, allocates nothing | `transform_inplace`, `map_inplace`, `sort_by_inplace` |
| `try_` | wraps a throwing call in a `Result` | `fp::try_([&]{ ... })` |
| `map`/`and_then`/`or_else` | the shared ADT vocabulary | (works on `Result`, `optional`, `Either`, `Validation`) |

## Opt-in headers

`all.hpp` deliberately stops short of three headers:

| Header | Why opt-in |
|---|---|
| `simd.hpp` | needs `<experimental/simd>` and a native-SIMD target |
| `autodiff.hpp` | a specialized numeric tool, not part of the everyday surface |
| `macros.hpp` | macros should never arrive uninvited |

Include them explicitly when you want them.

## The zero-cost contract

The library is built so that "functional style" does not mean "slower":

- `*_inplace` functions, `for_each`, and the lazy `fp::views` adaptors
  **allocate nothing** and compile to the same code as hand-written loops.
- `fp::linalg` kernels are loop-ordered for cache behavior (`matmul` is ~1.75x
  faster than the naive loop order) and `fp::simd` covers the math functions
  the compiler cannot auto-vectorize.
- Everything is benchmarked in `bench/` (`scripts/run_bench.sh`); a primitive
  that does not match its hand-written baseline is a bug.

## Testing with ForgeFP

- Assert on `Result` values with `is_ok()`/`value()`; use `fp::unwrap` in tests
  when a failure should fail the test.
- Compare floats with `fp::approx_equal` (scale-aware) or a tolerance.
- Verify gradients with `fp::central_difference` or `fp::ad::derivative`.
- Use `fp::to_string` for exact-value round-trips (precision 17).
- Seed `fp::Rng` with a literal so every run is reproducible.

```cpp
TEST(MySuite, ParseDoubles) {
  auto xs = fp::str::parse_numbers<double>("1, 2.5 3");
  ASSERT_TRUE(xs.is_ok());
  EXPECT_EQ(xs.value(), (std::vector<double>{1.0, 2.5, 3.0}));
}
```

## Where to go next

- New to the library? [Collections](collections.md) and
  [ADTs](adts.md) are the two core ideas.
- Building a hot loop? [Iteration & in-place](inplace.md) and
  [SIMD](simd.md).
- Building a model or simulation? [Numerics](numerics.md),
  [Linear algebra](linalg.md), [Random](random.md).
- Building a service? [I/O](io.md), [Concurrency](concurrency.md),
  [Serialization](serialization.md), [Scopes](scope.md).
- Need the full map? [the documentation index](README.md).

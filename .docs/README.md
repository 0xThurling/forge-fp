# ForgeFP — Documentation

ForgeFP is a header-only, zero-dependency C++20 functional-programming library.
This directory is the usage reference: what's in the library, how the pieces fit
together, and worked examples for everything.

Everything lives in the `fp` namespace (string utilities in `fp::str`). Include
one header for a single module, or `fp/all.hpp` for everything except the
opt-in `simd.hpp` and `macros.hpp`.

## Modules

| Doc | Header(s) | What it covers |
|---|---|---|
| [Getting started](getting-started.md) | `fp/all.hpp` | building, including, first program, conventions |
| [ADTs: `Either` / `Result` / `Maybe` / `Validation`](adts.md) | `either.hpp`, `result.hpp`, `maybe.hpp`, `validation.hpp` | the four error/optionality types and their combinators |
| [Structured errors](adts.md#outcomet--structured-errors-with-codes-and-context) | `error.hpp` | `Error` / `Outcome<T>`: codes, context chains, source locations |
| [Collections](collections.md) | `vec.hpp`, `ranges.hpp`, `views.hpp` | `map`/`filter`/`fold`/`zip`/… over vectors and ranges, lazy `fp::views` |
| [Maps & grids](maps-grids.md) | `map.hpp`, `grid.hpp` | associative-container helpers, 2D/3D grids |
| [Strings](strings.md) | `string.hpp` | `split`/`join`/`trim`/parsing/… in `fp::str` |
| [Parsing](parsing.md) | `parse.hpp` | parser combinators |
| [File I/O](io.md) | `io.hpp` | `read_file` / `read_lines` / `write_file` |
| [Input](input.md) | `input.hpp` | reading stdin/streams as `Result`/`Stream`/`Channel` |
| [Printing](print.md) | `print.hpp` | `operator<<` for the ADTs |
| [Concurrency](concurrency.md) | `concurrent.hpp`, `task.hpp`, `stream.hpp` | thread pool, channels, actors, futures, cancellation, `Stream` |
| [Memory](memory.md) | `arena.hpp` | the bump allocator |
| [SIMD](simd.md) | `simd.hpp` | vectorized `map`/`reduce`/`dot`/math |
| [Function composition](composition.md) | `compose.hpp`, `curry.hpp`, `combinators.hpp`, `ops.hpp`, `memoize.hpp` | pipes, currying, named operators |
| [Pattern matching](pattern-matching.md) | `adt.hpp` | `match`, `case_`, `cond`, `when`, `otherwise` |
| [Macros](macros.md) | `macros.hpp` | `FP_TRY` |

## The mental model

ForgeFP is a set of **small, pure functions** you compose. There is no class
hierarchy and no inheritance — values flow through free functions:

```cpp
std::vector<std::string> names = {"ada", "bob", "cy"};

auto result = fp::pipeline(names,
    fp::filter([](std::string const& s) { return s.size() > 2; }),  // curried stage
    fp::map(fp::str::to_upper),                                    // curried stage
    [](std::vector<std::string> const& v) { return fp::str::join(v, ", "); });
// "ADA, BOB"
```

Three recurring shapes dominate the library:

1. **Collections** (`std::vector<T>`, any range) — transformed by
   `map`/`filter`/`fold_left`/`zip`/…, always returning a fresh collection.
2. **Wrappers** (`Result<T>`, `Either<E,T>`, `std::optional<T>`,
   `Validation<T>`) — chained with `map`/`and_then`/`or_else`, short-circuiting
   on the "failure" side.
3. **Functions** — combined with `compose`/`pipe`/`curry`/`|` and the named
   operators in `fp::ops`.

## Conventions

- **Headers are the API.** Everything is a free function or a tiny aggregate
  (`Either`, `Arena`, `Stream`, …). There are no `virtual`s, no macros (except
  the opt-in `FP_TRY`), no dependencies.
- **Pure by default.** Combinators take `const&`/by-value and return new values;
  mutating `*_inplace` variants (`map_inplace`, `par_map_inplace`) are the
  documented escape hatches for hot loops.
- **`Result` is the error channel.** Errors are `std::string` messages; nothing
  throws unless you call `unwrap`/`expect` (the sanctioned escape hatches).
- **Two trees.** `src/fp/` and `include/forgefp/fp/` are byte-identical mirrors;
  the installed (`include/`) tree is what CMake ships.

## Where each header lives

| Header | Namespace | Opt-in? |
|---|---|---|
| `adt.hpp`, `either.hpp`, `result.hpp`, `maybe.hpp`, `validation.hpp` | `fp` | no |
| `vec.hpp`, `ranges.hpp`, `map.hpp`, `grid.hpp` | `fp` | no |
| `string.hpp` | `fp::str` | no |
| `parse.hpp`, `io.hpp`, `input.hpp`, `arena.hpp`, `stream.hpp` | `fp` | no |
| `compose.hpp`, `curry.hpp`, `combinators.hpp`, `ops.hpp`, `memoize.hpp` | `fp` | no |
| `concurrent.hpp` | `fp` | no |
| `simd.hpp` | `fp` | yes — `#include` it explicitly (not in `all.hpp`) |
| `macros.hpp` | (macros) | yes — `#include` it explicitly (not in `all.hpp`) |
| `print.hpp` | `fp` | yes — `#include` it explicitly (not in `all.hpp`) |

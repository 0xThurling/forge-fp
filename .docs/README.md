# ForgeFP — Documentation

ForgeFP is a header-only, zero-dependency C++20 functional-programming library.
This directory is the usage reference: what's in the library, how the pieces fit
together, and worked examples for everything.

Everything lives in the `fp` namespace (string utilities in `fp::str`, GPU in
`fp::gpu`). Include one header for a single module, or `fp/all.hpp` for
everything except the opt-ins: `simd.hpp`, `gpu.hpp`, `autodiff.hpp` and
`macros.hpp`.

## Modules

| Doc | Header(s) | What it covers |
|---|---|---|
| [Getting started](getting-started.md) | `fp/all.hpp` | building, including, first program, conventions |
| [ADTs: `Either` / `Result` / `Maybe` / `Validation`](adts.md) | `either.hpp`, `result.hpp`, `maybe.hpp`, `validation.hpp` | the four error/optionality types and their combinators |
| [Structured errors](adts.md#outcomet--structured-errors-with-codes-and-context) | `error.hpp` | `Error` / `Outcome<T>`: codes, context chains, source locations |
| [Collections](collections.md) | `vec.hpp`, `ranges.hpp`, `views.hpp` | `map`/`filter`/`fold`/`zip`/… over vectors and ranges, lazy `fp::views` (incl. `chunk`/`slide`/`stride`/`zip_with`/`zip3`) |
| [Iteration & in-place](inplace.md) | `inplace.hpp` | `for_each`, `transform_inplace`, `map_to`, `sort_inplace`, `remove_if_inplace`, … — the allocation-free counterparts of the eager combinators |
| [Maps & grids](maps-grids.md) | `map.hpp`, `grid.hpp` | associative-container helpers, 2D/3D grids, indexed/patch mapping |
| [Strings](strings.md) | `string.hpp` | `split`/`join`/`trim`/parsing/`to_string`/… in `fp::str` |
| [Parsing](parsing.md) | `parse.hpp` | parser combinators |
| [File I/O](io.md) | `io.hpp` | `read_file` / `read_lines` / `write_file`, plus bytes, line writing, directories |
| [Input](input.md) | `input.hpp` | reading stdin/streams as `Result`/`Stream`/`Channel` |
| [Printing](print.md) | `print.hpp` | `operator<<` for the ADTs |
| [Concurrency](concurrency.md) | `concurrent.hpp`, `task.hpp`, `stream.hpp` | thread pool, channels, actors, futures, cancellation, `Stream`, `par_for`/`par_map_to` |
| [Memory & ownership](memory.md) | `memory.hpp`, `arena.hpp` | `Buffer`/`Box`/`Shared`, move/ptr/ref, scoped allocation, the bump allocator |
| [Scopes](scope.md) | `scope.hpp` | `defer`, `scope_exit`, `scope_success`, `scope_fail` — RAII as a value |
| [Numerics](numerics.md) | `numerics.hpp` | `softmax`/`log_softmax`/`logsumexp`/`sigmoid`, `linspace`/`arange`, `approx_equal`, `central_difference` |
| [Linear algebra](linalg.md) | `linalg.hpp` | `matmul`/`matvec`/`solve`, norms, `argmax`, `mean`/`variance`, row/column reductions |
| [Random](random.md) | `random.hpp` | seedable `Rng`, sampling, shuffling, weighted choice |
| [Time](time.md) | `time.hpp` | `now_seconds`, `elapsed_seconds`, `Stopwatch` |
| [Serialization](serialization.md) | `serialize.hpp` | generic text/binary round-trip for numeric ranges |
| [Autodiff](autodiff.md) | `autodiff.hpp` | forward-mode `Dual<T>`, `derivative` *(opt-in)* |
| [SIMD](simd.md) | `simd.hpp` | vectorized `map`/`reduce`/`dot`/math, `axpy_inplace` |
| [GPU kernels (SYCL)](gpu.md) | `gpu.hpp` | device buffers + kernels (`Buffer`/`HostBuffer`/`Scratch`, elementwise, row/column, `softmax_rows`, tiled `matmul`/`transpose`, `reduce`/`dot`) with a CPU fallback — design and measurements in [`GPU.md`](../GPU.md) *(opt-in)* |
| [Function composition](composition.md) | `compose.hpp`, `curry.hpp`, `combinators.hpp`, `ops.hpp`, `memoize.hpp` | pipes, currying, named operators (incl. `abs`/`sqrt`/`exp`/`log`/`min_`/`max_`/`pow`/`clamp`) |
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
| `vec.hpp`, `ranges.hpp`, `views.hpp`, `inplace.hpp`, `map.hpp`, `grid.hpp` | `fp` | no |
| `string.hpp` | `fp::str` | no |
| `parse.hpp`, `io.hpp`, `input.hpp`, `memory.hpp`, `arena.hpp`, `scope.hpp`, `serialize.hpp`, `stream.hpp` | `fp` | no |
| `numerics.hpp`, `linalg.hpp`, `random.hpp`, `time.hpp` | `fp` | no |
| `compose.hpp`, `curry.hpp`, `combinators.hpp`, `ops.hpp`, `memoize.hpp` | `fp` | no |
| `concurrent.hpp`, `task.hpp` | `fp` | no |
| `print.hpp` | `fp` | no — included by `all.hpp` |
| `simd.hpp` | `fp` | yes — `#include` it explicitly (not in `all.hpp`) |
| `gpu.hpp` | `fp::gpu` | yes — `#include` it explicitly (not in `all.hpp`); CPU fallback built in |
| `autodiff.hpp` | `fp`, `fp::ad` | yes — `#include` it explicitly (not in `all.hpp`) |
| `macros.hpp` | (macros) | yes — `#include` it explicitly (not in `all.hpp`) |

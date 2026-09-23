# ForgeFP

A header-only, C++20 functional programming library with zero external
dependencies. ForgeFP brings typical functional tools to modern C++: algebraic
data types (`Either`, `Result`, `Validation`), optional composition, vector/range
combinators, parser combinators, function composition and currying, named
operators, string utilities, file & input I/O, an arena allocator, SIMD mapping,
opt-in GPU kernels (SYCL, with a CPU fallback), concurrency helpers (thread
pool, channels, actors, streams), bit-level access to values, and macro-free
dynamic reflection.

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
| `gpu.hpp` | a SYCL 2020 implementation (AdaptiveCpp or oneAPI DPC++) to run on a device; without one every kernel falls back to the CPU |

The library is header-only — nothing to link.

---

## Getting the headers

Two byte-identical trees, one header per module:

- **`src/fp/`** — the full library. Include as `#include <fp/all.hpp>` (umbrella)
  or one module (`#include <fp/vec.hpp>`), with `-I src`.
- **`include/forgefp/fp/`** — the installed mirror (same files, byte-identical).
  Include as `#include <forgefp/fp/all.hpp>`.

Headers include their siblings with relative paths, so every header is
self-contained: it parses on its own (no `-I` flags needed for the library's
own includes), which also keeps editors and static analyzers happy.

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
| `autodiff.hpp` | forward-mode AD: `ad::Dual<T>` (value + derivative) and `ad::sin`/`cos`/`exp`/`log`/`sqrt`/`tanh`/`pow`/`relu` *(opt-in)* |
| `bits.hpp` | **values** `bit`/`set_bit`/`clear_bit`/`toggle_bit`, `low_mask`/`bit_mask`/`extract_bits`/`insert_bits`, `popcount`/`leading_zeros`/`trailing_zeros`/`bit_width`, `rotl`/`rotr`/`byteswap`/`bit_reverse`/`sign_bit`, `to_little_endian`/`to_big_endian`, whole-value `bit_and`/`bit_or`/`bit_xor`/`bit_not`; **bytes** `bytes_of`/`as_bytes`/`word_at`; **sections** `bit_span`/`const_bit_span` (read/write, indexing, iteration, `section`/`split`, memmove `copy_from`, `fill`/`flip`/`popcount`/`any`/`all`/`none`, `find_first_set`/`first_clear`/`next_set`/`next_clear`/`last_set`) and compile-time `bit_field`/`bit_split`; **streams** `BitWriter`/`MsbWriter` + `BitReader`/`MsbReader` (`BitOrder`, per-call override, `align_to_byte`, `write(span)`/`read_into`/`peek_bits`/`release`, bounded readers); **text** `to_binary`/`to_hex` |
| `combinators.hpp` | `identity`, `const_`, `flip`, `on`, `compose`, `fix`, `apply`, `when`/`unless`, `first`/`second`, `pipe_with` |
| `compose.hpp` | `compose`, `pipe`, `into`/`out`, `operator\|`, `tap` |
| `concurrent.hpp` | `ThreadPool`, `par_map`/`par_for_each`/`par_reduce`, `Channel`, `RingBuffer`, `Actor`, `Async`, `async_map`/`async_sequence`, `race`/`timeout`/`retry`, cancellable overloads + `spawn`/`async_task` |
| `curry.hpp` | `curry`, `uncurry` |
| `either.hpp` | `Either<E, T>` (incl. `Either<E, void>`) + `map`/`and_then`/`or_else`/`map_error`/`flatten`/`bimap`/`swap`/`to_optional`/`rights`/`lefts`/`expect`/`ok_or`/`tap_err`/`tap_ok`/`>>=` |
| `error.hpp` | `Error` (code + message + cause chain + `source_location`), `Outcome<T>` = `Either<Error,T>`, `error`/`with_context`/`root_cause`/`to_string`, `to_result`/`from_result`, `fp::errc::*` |
| `grid.hpp` | `map2d`/`map3d`, `transpose`, `flatten`, `for_each_cell`, `tabulate`, `cartesian_product` |
| `inplace.hpp` | in-place algorithms on any range: `for_each`/`for_each_index`/`transform_inplace`/`fill`/`map_to`/`zip_for_each`/`zip3_for_each`/`sort_inplace`/`sort_by_inplace`/`sort_by_cached_inplace`/`stable_sort_by_inplace`/`reverse_inplace`/`unique_inplace`/`remove_if_inplace` |
| `input.hpp` | `read_line`/`read_all`/`read_lines`/`read_char`/`read_chars`/`feed_lines`, POSIX `raw_mode`/`read_key` |
| `io.hpp` | `read_file`, `read_lines`, `write_file` |
| `linalg.hpp` | dense kernels over nested ranges and spans: `matmul` (nested and flat-buffer), `batched_matmul`, `matvec`/`outer`/`hadamard`, `dot`/`norm_l1`/`norm_l2`/`mean`/`variance`/`scale`, `row_sums`/`col_sums`/`row_means`/`col_means`/`argmax_rows`/`argmin_rows`, `solve` |
| `macros.hpp` | `FP_TRY` *(opt-in)* |
| `map.hpp` | `lookup`, `map_values`, `filter_values`, `merge_with`, `keys`, `values`, `to_map`, `to_unordered_map` (all generic over `std::map`/`std::unordered_map`) |
| `maybe.hpp` | `std::optional` combinators: `map`/`and_then`/`or_else`/`filter`/`flatten`/`apply`/`collect`/`value_or_lazy`/`>>=` |
| `memoize.hpp` | `memoize<Arg>(f)`, `memoize2<A,B>(f)`, `memoizeN<Args...>(f)` |
| `memory.hpp` | `move`/`forward`, pointer helpers (`ptr`/`ref`/`deref`/`as_const`), lifetime (`construct_at`/`destroy_at`/`destroy`), `Buffer<T>` (aligned owning buffer), `Box<T>` (unique owner), `Shared<T>`/`make_shared`/`share` |
| `numerics.hpp` | `is_finite`/`nan_to_num`, `approx_equal`, `clamp`, `relu`/`sigmoid`/`softmax`/`softmax_rows`/`log_softmax`/`logsumexp`, `linspace`/`arange`, `central_difference` |
| `ops.hpp` | named operators: `plus`/`minus`/`times`/`divide`, `eq`/`ne`/`lt`/`le`/`gt`/`ge`, `and_`/`or_`/`not_`, `negate`/`increment`/`decrement`, elementwise math (`abs`/`sqrt`/`exp`/`log`/`log1p`/`sin`/`cos`/`tanh`/`sign`), `min_`/`max_`/`pow`/`clamp` |
| `parse.hpp` | `Parser<T>` + primitives/sequencing/choice/lexemes (`eof`/`peek`/`not_followed`/`label`/`context`/`many1`/`chainl1`), position-carrying errors, operators (`>>`, `<<`, `\|`, `>>=`, `*`, `%`) |
| `print.hpp` | `operator<<` for `Result`/`Either`/`Validation` |
| `random.hpp` | `Rng`: `next_u64`/`randint`/`uniform`/`normal`/`bernoulli`/`shuffle`/`sample_indices`/`categorical`/`weighted_choice` |
| `ranges.hpp` | range-generic `map`/`filter`/`fold_left`/`fold_right`/`scan`/`zip`/`enumerate`/`group_by`/`chunk`/`windows`/`flat_map`/`filter_map`/`take_while`/`drop_while`/`unique`/`sort`/`sort_by`/`partition`/`span` + curried stages for `into(…) \| …` pipelines |
| `reflect.hpp` | macro-free, RTTI-free dynamic reflection: `arity`/`field_at`/`field_type`/`field_name` + compile-time `has_field`/`field_index`, `fields`/`describe`/`describe_shape`/`type_info` with a lock-free registry, `AnyRef` with `field`/`get_field`/`set_field` and `for_each_field`/`for_each_field_ref`, whole-object `to_string`/`equal`/`copy_fields`, enum names, `FieldAccess` (+ `members<...>`) opt-in |
| `result.hpp` | `Result<T>` + `ok`/`err`, `sequence`/`traverse`/`transpose`/`try_`/`combine2`/`context`/`collect_all`/`unwrap`, `std::expected` bridge (C++23) |
| `scope.hpp` | scope guards: `defer`, `scope_exit`, `scope_success`, `scope_fail`, `ScopeGuard` |
| `serialize.hpp` | `to_text`/`from_text`, `to_bytes`/`from_bytes` for flat numeric ranges |
| `simd.hpp` | `vec<T>`, `map_inplace`/`map_to`/`map_inplace_fixed`, `reduce`/`dot`, `map_sqrt`/`map_exp`, `clamp_inplace`/`normalize`/`threshold_inplace`, `par_map_inplace` *(opt-in)* |
| `gpu.hpp` | SYCL device buffers + kernels (`Buffer<T>`, pinned `HostBuffer<T>`, `Scratch<T>`, `map_to`, `transform_inplace`, `transform_inplace_indexed`, `zip_transform_inplace`/`zip3_transform_inplace`, `map`, `axpy_inplace`, `softmax_rows`/`softmax_rows_wg`, `row_sums`/`row_means`/`col_sums`, `add_row_broadcast`, `transpose`, `reduce`, `dot`, tiled `matmul`/`batched_matmul`) with a CPU fallback *(opt-in, see `GPU.md`)* |
| `task.hpp` | `Task<T>` (cancellable `AsyncResult`): `cancel`/`token`/`then`/`and_then`/`recover`/`join`, `std::stop_token` helpers (`cancel_after`, `cancelled`) |
| `stream.hpp` | `Stream<T>` (pull/push `map`/`filter`/`subscribe`/`collect`/`take`/`take_while`/`scan`/`fold_left`/`concat`) |
| `string.hpp` | `fp::str`: `split`/`split_view`/`join`/`trim`/`to_lower`/`to_upper`/`to_int`/`to_double`/… |
| `time.hpp` | `now_seconds`/`elapsed_seconds` + `Stopwatch` (`elapsed`/`lap`) |
| `validation.hpp` | `Validation<T>` + `valid`/`invalid`/`validate_all`/`combine`/`combine2`/`check`/`ensure`/`traverse`/`to_result` |
| `vec.hpp` | `map`/`filter`/`zip`/`zip_with`/`group_by`/`partition`/`chunk`/`sort`/`sort_by`/`sum`/`product`/`scan`/`range`/… |
| `views.hpp` | lazy `fp::views::` adaptors: `map`/`filter`/`take`/`drop`/`take_while`/`drop_while`/`reverse`/`join`/`filter_map`/`flat_map`/`enumerate`/`zip` |

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
- **Opt-ins** — `simd.hpp`, `autodiff.hpp`, `gpu.hpp` and `macros.hpp` are
  *not* in `all.hpp`; include
  them explicitly. `simd.hpp` requires `<experimental/simd>` (GCC/Clang); the
  `FP_TRY_VALUE`/`FP_TRY_VOID` macros are portable.
- **Threads** — `concurrent.hpp` needs `-pthread`.
- **`memoize`** is not thread-safe; neither is `Rng` (one generator per
  thread).
- **Error-valued returns are `[[nodiscard]]`** — `Result`, `Outcome`,
  `Validation`, and optional-returning lookups (`head`, `lookup`, ...). Writing
  `fp::read_file("x");` as a statement is a compiler warning, because the whole
  point is to check it.
- **`sort_by` evaluates its key per comparison.** That is allocation-free and
  right for cheap keys; for keys that allocate or compute, use
  `sort_by_cached`/`sort_by_cached_inplace` (keys computed once).
- **`reflect.hpp`** needs no macros or RTTI. It reflects aggregates (up to 32
  fields), C arrays and tuple-like types; field names are recovered at compile
  time on GCC/Clang and fall back to `fieldN` elsewhere. The registry is
  thread-safe.
- **`bits.hpp`** is `constexpr` throughout; bit-index preconditions are
  `assert`s, and the object-representation helpers use native byte order.
- **`Either` accessors** (`value()`/`error()`) assume the right alternative is
  held; guard with `is_ok()` first, or destructure with `match`.

---

## License

See repository metadata. Built with Forge (see `forge.lua`).

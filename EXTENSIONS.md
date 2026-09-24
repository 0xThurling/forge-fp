# ForgeFP Extensions Plan

ForgeFP is the shared substrate for `forge-gl` and `ml` (ForgeML). Both projects
are written in fp as much as possible, and anything they need that is
**general** gets added to fp instead of duplicated in the domain code.

Every gap listed here is **in scope**. "Wave" only describes implementation
order, not optionality. Two modules are marked optional because they are large
and independent (`serialize`, `autodiff`).

## Status

- **Wave 1 landed** (`src/fp/` + `include/forgefp/fp/` mirror):
  `inplace.hpp`, `scope.hpp`, `memory.hpp`, `Arena` extensions
  (`alloc_bytes`, `alloc_span`, `mark`/`reset_to`, `with_arena_scope`).
  Tests: `test/inplace_test.cpp`, `test/scope_test.cpp`,
  `test/memory_test.cpp`, plus new cases in `test/arena_test.cpp`.
  Benchmarks: `bench/inplace_bench.cpp` (run via `scripts/run_bench.sh`) show
  `for_each`, `transform_inplace`, `sort_inplace`, and
  `Buffer`/`with_buffer` within noise of hand loops / `std::vector` / raw
  `new[]`.
- **Wave 2 landed**: `views` extensions (`chunk`, `slide`, `stride`,
  `take_last`, `zip_with`, `zip3`, `iota` — the lazy integer range added
  during the forge-gl re-audit, plus a `pipe_closure` so the new adaptors
  work as `range | closure` as well as `into(x) | closure`), `grid`
  extensions (`map2d_indexed`, `column`, `windows2d`), `ops` extensions
  (`abs`, `sqrt`, `exp`, `log`, `log1p`, `sign`, `min_`, `max_`, `pow(k)`,
  `clamp(lo, hi)`), and `numerics.hpp` (`softmax`, `log_softmax`,
  `logsumexp`, `sigmoid`, `relu`, `clamp`, `linspace`, `arange`,
  `approx_equal`, `is_finite`, `nan_to_num`, `central_difference`).
  Tests: `test/ops_test.cpp`, `test/numerics_test.cpp`, plus new cases in
  `test/views_test.cpp` and `test/map_grid_test.cpp`. Benchmark:
  `bench/numerics_bench.cpp` (`fp::softmax` matches/beats the hand loop).
- **Wave 3 landed**: `linalg.hpp` (`matmul` i-k-j, `batched_matmul`, `matvec`,
  `outer`, `solve` with partial pivoting, `hadamard`, `scale`,
  `add_row_broadcast`, `dot`, `norm_l1/l2`, `argmax/argmin`, `mean`,
  `variance`, `row_sums/col_sums`, `row_means/col_means`, `argmax_rows`,
  `argmin_rows`); `simd.hpp` `axpy_inplace`; `numerics.hpp` `softmax_rows`;
  `grid.hpp` `map2d_inplace`. Tests: `test/linalg_test.cpp` plus new cases in
  `test/simd_test.cpp`, `test/numerics_test.cpp`, `test/map_grid_test.cpp`.
  Benchmark: `bench/linalg_bench.cpp` (`fp::matmul` 1.74x faster than the
  naive i-j-k loop; `matvec` matches the hand loop).
- **Wave 4 landed**: `random.hpp` (`Rng`, `randint`, `uniform`, `normal`,
  `bernoulli`, `shuffle`, `sample_indices`, `categorical`/`weighted_choice`),
  `time.hpp` (`now_seconds`, `elapsed_seconds`, `Stopwatch`), `str`
  extensions (`to_string(value, precision)`, `split_any`, `parse_numbers<T>`),
  `io` extensions (`exists`, `read_bytes`, `write_bytes`, `write_lines`,
  `ensure_directory`). Tests: `test/random_test.cpp`, `test/time_test.cpp`,
  plus new cases in `test/string_test.cpp` and `test/io_input_test.cpp`.
- **Wave 5 landed**: `concurrent.hpp` extensions (`par_for(pool, begin, end,
  f)` tiling primitive, `par_for_each_index(pool, v, f)`,
  `par_map_to(pool, src, dst, f)` writing into a caller-owned buffer).
  Test: new case in `test/concurrent_test.cpp`.
- **Wave 6 landed (optional modules)**: `serialize.hpp` (`to_text`/`from_text`
  for arithmetic ranges, `to_bytes`/`from_bytes` for trivially copyable
  types), and `autodiff.hpp` (forward-mode `fp::Dual<T>`, elementary functions
  in `fp::ad` so they never collide with `fp::ops`, `fp::derivative`; opt-in
  like `simd.hpp`, not in `all.hpp`). Tests: `test/serialize_test.cpp`,
  `test/autodiff_test.cpp`.
- **Wave 7 landed**: `bits.hpp` (`bytes_of`/`as_bytes`/`word_at`; `bit`,
  `set_bit`, `clear_bit`, `toggle_bit`; `low_mask`, `bit_mask`,
  `extract_bits`, `insert_bits`; `popcount`, `count_zeros`, `leading_zeros`,
  `trailing_zeros`, `bit_width`, `has_single_bit`; `rotl`/`rotr`, `byteswap`,
  `sign_bit`; `to_little_endian`/`from_little_endian`/`to_big_endian`/
  `from_big_endian`; whole-value `bit_not`/`bit_and`/`bit_or`/`bit_xor`;
  `bit_reverse`, `to_binary`, `to_hex`; `bit_span`/`const_bit_span` (views of a
  bit range of an object or of bytes — including `unsigned char`/`char`
  buffers — with `read`/`write`, indexing, iteration, `section`/`split`,
  memmove-semantics `copy_from`, `fill`/`flip`/`popcount`/`any`/`all`/`none`,
  and `find_first_set`/`first_clear`/`next_set`/`next_clear`/`last_set`);
  `bit_field<Lo,Hi>` and `bit_split<Widths...>` compile-time fields; and the
  packed streams — `BitWriter`/`MsbWriter` and `BitReader`/`MsbReader` with a
  per-type `BitOrder` plus a per-call override (so formats that mix orders,
  like DEFLATE, are expressible), `align_to_byte`, `write(span)`,
  `read_into`, `peek_bits`, `release`, and readers bounded to a section) and
  `reflect.hpp` (macro-free, RTTI-free dynamic reflection: `arity`, `field_at`,
  `field_type`, `field_name`, `fields`, `describe`, `type_info` +
  `TypeRegistry` with lock-free reads, `AnyRef` with `field`/`get_field`/
  `set_field`/`for_each_field`/`for_each_field_ref`, whole-object `to_string`/
  `equal`/`copy_fields`, compile-time `has_field`/`field_index`,
  `describe_shape<T>()` for name-free metadata, enum name/value mapping, and the
  `FieldAccess` opt-in for non-aggregates plus the `members<...>` helper). Tests: `test/bits_test.cpp`, `test/reflect_test.cpp`;
  benchmark: `bench/bits_bench.cpp`. Both modules are in `all.hpp` and the
  `include/forgefp/fp/` mirror; neither uses macros or RTTI, and field names are
  recovered at compile time on GCC/Clang (Boost.PFR's technique) with a
  `fieldN` fallback elsewhere.

**All planned waves are landed.** Every module in the map above is implemented
in both header trees, with tests; the zero-cost claims are backed by
`bench/inplace_bench.cpp`, `bench/numerics_bench.cpp`, and
`bench/linalg_bench.cpp`.

## Next: GPU (SYCL)

The fourth execution tier — opt-in SYCL kernels with a CPU fallback — has its
own plan in [`GPU.md`](GPU.md). Phases 0–3 are landed (`gpu.hpp`: detection,
`usable()`, USM `Buffer<T>` device + shared, pinned `HostBuffer<T>` staging,
`nd_range` elementwise kernels, `axpy_inplace`, `softmax_rows` and its
work-group variant, row/column kernels, chunked `reduce`/`dot` with a reusable
`Scratch<T>`, and tiled `matmul`/`batched_matmul`). The fallback path runs in
the gtest suite and the SYCL path is compiled and exercised on CPU by the stub
harness (`scripts/run_gpu_stub_test.sh`), which models the SYCL item types so
kernel signatures are type-checked. The same code is verified on a real device
(AdaptiveCpp `--acpp-targets=generic` on an RTX 3060 under WSL2) by
`scripts/run_gpu_build.sh` — see
[GPU.md](GPU.md#measured-crossovers-rtx-3060-wsl2) for the measured crossovers,
the pinned-transfer numbers, and the JIT cold-start caveat.

## Boundary rule

fp may know about **numbers, ranges, functions, memory, and time**. fp must not
know about **datasets, labels, losses, layers, epochs, or classes**.

| Belongs in fp | Stays in the domain project |
|---|---|
| dense linear algebra (`matmul`, `solve`, norms, reductions) | losses, metrics, models, layers |
| `Rng`, distributions, sampling | optimizers (they are written *with* fp primitives) |
| `softmax`, `logsumexp`, `sigmoid`, `linspace`, elementwise math | gradient-check harness for domain layers |
| zero-cost iteration, in-place algorithms, lazy views | datasets, tokenizers, checkpoints |
| ownership, buffers, borrowing, arenas, scopes | reverse-mode parameter graphs |
| byte/text IO, directories, time, stopwatch | serialization *format* (ml keeps its schema) |
| forward-mode dual numbers (optional) | ML semantics of any kind |

If a name mentions a domain concept, it does not go in fp. If it is pure math,
pure iteration, pure memory, or pure time, it does.

## Performance contract

"I want fp everywhere" is only acceptable if fp costs nothing in hot paths.
Every new fp primitive must satisfy:

1. **No allocation** unless the name says otherwise (`to_vector`, factories,
   eager combinators). `*_inplace`, `for_each`, `views::*` never allocate.
2. **No `std::function`, no virtual, no type erasure** in per-element paths.
3. **No bounds checks** in release; `at`-style checks only in debug.
4. **Compiles to the same code as a hand-written loop.** Verified with the
   existing `bench/` harness: fp primitive vs hand loop within noise.
5. **Any range works.** Constraints are `std::ranges::range`; a domain
   container only needs `begin()`/`end()` (and `data()` for SIMD paths).
6. **Scalar fallback is always correct.** SIMD paths are additive, never
   required for correctness.

## Module map

| Module | Wave | Contents | Consumers |
|---|---|---|---|
| `fp/inplace.hpp` | 1 | `for_each`, `for_each_index`, `transform_inplace`, `zip_for_each`, `zip3_for_each`, `fill`, `map_to`, `sort_inplace`, `sort_by_inplace`, `stable_sort_by(_inplace)`, `reverse_inplace`, `unique_inplace`, `remove_if_inplace` | both |
| `fp/scope.hpp` | 1 | `defer`, `scope_exit`, `scope_success`, `scope_fail` | both |
| `fp/memory.hpp` | 1 | `move`/`forward`/`exchange`, `ptr`/`ref`/`as_const`, `construct_at`/`destroy_at`, `Buffer<T>`, `Box<T>`, `Shared<T>`, `with_buffer`, `with_ptr`, `copy_bytes`, `fill_bytes` | both |
| `fp/arena.hpp` (extend) | 1 | `alloc_bytes`, `alloc_span`, `mark`/`reset_to`, `with_arena_scope` | both |
| `fp/views.hpp` (extend) | 2 | `zip_with`, `zip3`, `chunk(n)`, `slide(n)`, `stride(n)`, `take_last(n)` | both |
| `fp/grid.hpp` (extend) | 2 | `map2d_indexed`, `map2d_inplace`, `windows2d`, `column` | ml (CNN, scaling), forge-gl (pixels) |
| `fp/ops.hpp` (extend) | 2 | `abs`, `sqrt`, `exp`, `log`, `log1p`, `sin`, `cos`, `tanh`, `pow(k)`, `sign`, `min_`, `max_`, `clamp(lo, hi)` | both |
| `fp/numerics.hpp` | 2 | `softmax`, `softmax_rows`, `log_softmax`, `logsumexp`, `sigmoid`, `relu`, `clamp`, `linspace`, `arange`, `approx_equal`, `is_finite`, `nan_to_num`, `central_difference` | both |
| `fp/linalg.hpp` | 3 | `matmul`, `batched_matmul`, `matvec`, `outer`, `solve`, `norm_l1/l2`, `argmax/argmin`, `argmax_rows`, `mean`, `variance`, `row_sums/col_sums`, `row_means/col_means`, `hadamard`, `scale`, `add_row_broadcast` | both |
| `fp/simd.hpp` (extend) | 3 | `axpy_inplace` | both |
| `fp/random.hpp` | 4 | `Rng`, `uniform`, `normal`, `randint`, `bernoulli`, `shuffle`, `sample_indices`, `categorical`, `weighted_choice` | both |
| `fp/str` (extend) | 4 | `to_string(value, precision)`, `split_any`, `parse_numbers` | both |
| `fp/io.hpp` (extend) | 4 | `read_bytes`, `write_bytes`, `write_lines`, `exists`, `ensure_directory` | both |
| `fp/time.hpp` | 4 | `now`, `Stopwatch`, `elapsed_seconds` | both |
| `fp/concurrent.hpp` (extend) | 5 | `par_for`, `par_for_each_index`, `par_map_to` | both |
| `fp/serialize.hpp` (optional) | 6 | generic range/arithmetic text+binary round-trip | ml |
| `fp/autodiff.hpp` (optional) | 6 | forward-mode `Dual<T>`, `derivative` | ml |

No owning `Matrix`/`Tensor` in fp: domain containers stay in `ml/core/`; fp
algorithms work on any range, and ml containers expose `begin()/end()/data()` so
fp composes with them directly.

---

## Wave 1 — iteration, scopes, memory

### `fp/inplace.hpp`

Iteration and mutation as free functions. These remove the last reason to write
a raw loop outside a numeric kernel.

```cpp
namespace fp {

// --- iteration ---
template <std::ranges::range R, class F> void for_each(R &&r, F f);
template <std::ranges::range R, class F> void for_each_index(R &&r, F f);   // f(i, x)
template <std::ranges::range R, class F> void transform_inplace(R &r, F f); // x = f(x)
template <std::ranges::range A, std::ranges::range B, class F>
void zip_for_each(A &&a, B &&b, F f);                                       // f(a_i, b_i)
template <std::ranges::range A, std::ranges::range B, std::ranges::range C, class F>
void zip3_for_each(A &&a, B &&b, C &&c, F f);
template <std::ranges::range R, class T> void fill(R &r, T const &value);

// --- writing a transform into a destination (no result allocation) ---
template <std::ranges::range Src, std::ranges::range Dst, class F>
void map_to(Src &&src, Dst &dst, F f);
template <std::ranges::range A, std::ranges::range B, class F>
void zip_transform_inplace(A &a, B const &b, F f);   // a_i = f(a_i, b_i)

// --- in-place algorithms (no copy) ---
template <std::ranges::range R> void reverse_inplace(R &r);
template <std::ranges::range R> void sort_inplace(R &r);
template <std::ranges::range R, class Key> void sort_by_inplace(R &r, Key key);
template <std::ranges::range R, class Key> void stable_sort_by_inplace(R &r, Key key);
template <std::ranges::range R> void unique_inplace(R &r);        // adjacent dedup
template <std::ranges::range R, class Pred> void remove_if_inplace(R &r, Pred pred);
}
```

Rules:

- Plain loops / `std::ranges` algorithms; `constexpr` where possible.
- `remove_if_inplace` requires a container with `erase` (vector, string);
  the constraint is expressed in a `requires` clause.
- `stable_sort_by_inplace` uses `std::stable_sort`; the non-stable variant is
  the default because `forge-gl`'s painter's algorithm and top-k sorting do not
  need stability, but it is available.
- In-place variants return `void`; chaining is done with the owning container.

### `fp/scope.hpp`

Functional RAII: run a cleanup when a scope exits.

```cpp
namespace fp {

// Runs f on scope exit, always.
template <class F> auto defer(F f);

// Success/failure variants based on std::uncaught_exceptions().
template <class F> auto scope_success(F f);
template <class F> auto scope_fail(F f);

// The generic form: runs f() unless released.
template <class F> class ScopeGuard {
public:
  explicit ScopeGuard(F f);
  ScopeGuard(ScopeGuard &&) noexcept;
  ~ScopeGuard();                 // calls f() unless released
  void release() noexcept;       // cancel
};
}
```

Rules:

- `defer` is `ScopeGuard` with always-run semantics; the guard is movable, not
  copyable.
- `scope_success` / `scope_fail` capture `std::uncaught_exceptions()` at
  construction and compare on destruction.
- No allocation; the callable is stored inline (`F` template parameter, no
  `std::function`).
- Used for locks, file handles, arena checkpoints, temporary buffer releases.

### `fp/memory.hpp`

Ownership as a value, borrowing as a value, movement as a named operation. All
wrappers are thin (`constexpr`/`noexcept` one-liners or a single-allocation
RAII block); nothing here adds overhead over `unique_ptr` / raw `new[]`.

```cpp
namespace fp {

// --- movement and access (compile away) ---
template <class T> constexpr std::remove_reference_t<T> &&move(T &&x) noexcept;
template <class T> constexpr T &&forward(std::remove_reference_t<T> &x) noexcept;
template <class T, class U> T exchange(T &obj, U &&replacement);

template <class T> constexpr T *ptr(T &x) noexcept;            // address-of
template <class T> constexpr T const *ptr(T const &x) noexcept;
template <class T> constexpr T &ref(T *p) noexcept;            // *p, named
template <class T> constexpr T &deref(T *p) noexcept;          // alias of ref
template <class T> constexpr T const &as_const(T &x) noexcept;

// --- object lifetime (C++20 wrappers with named intent) ---
template <class T, class... Ts> T *construct_at(T *p, Ts &&...ts);
template <class T> void destroy_at(T *p);
template <class T> void destroy(std::span<T> s);

// --- owning contiguous block (a range, so fp combinators work on it) ---
template <class T> class Buffer {
public:
  Buffer() noexcept;
  Buffer(Buffer &&) noexcept;
  Buffer &operator=(Buffer &&) noexcept;
  Buffer(Buffer const &) = delete;
  ~Buffer();

  std::size_t size() const noexcept;
  bool empty() const noexcept;
  T *data() noexcept;  T const *data() const noexcept;
  T &operator[](std::size_t i) noexcept;
  std::span<T> span() noexcept;  std::span<const T> span() const noexcept;
  T *begin() noexcept;  T *end() noexcept;

  void fill(T const &value);
  Result<Buffer<T>> clone() const;
  Result<Buffer<T>> resized(std::size_t n) const;

  static Result<Buffer<T>> alloc(std::size_t n);      // uninitialized
  static Result<Buffer<T>> zeros(std::size_t n);
  static Result<Buffer<T>> copy_of(std::span<const T> src);
  static Result<Buffer<T>> from(std::initializer_list<T> init);

  T *release() noexcept;                              // explicit leak
  static Buffer<T> adopt(T *raw, std::size_t n) noexcept;   // explicit takeover
};

// --- single object ---
template <class T> class Box {
public:
  Box() noexcept; Box(Box &&) noexcept; Box &operator=(Box &&) noexcept;
  T &operator*();  T *operator->();  T *get();
  T *release();  void reset();
  template <class... Ts> static Result<Box<T>> make(Ts &&...ts);
};

// --- shared ownership ---
template <class T> using Shared = std::shared_ptr<T>;
template <class T, class... Ts> Result<Shared<T>> make_shared(Ts &&...ts);
template <class T> Shared<T> share(Box<T> box);

// --- scoped allocation: the owner cannot escape ---
template <class T, class F> auto with_buffer(std::size_t n, F f);
//   f(std::span<T>) -> R ; buffer freed after ; R returned by value
template <class T, class F> auto with_ptr(std::size_t n, F f);
//   f(T* p, std::size_t n) -> R

// --- raw copying (no allocation) ---
template <class T> void copy_bytes(std::span<T> dst, std::span<const T> src);
template <class T> void fill_bytes(std::span<T> dst, std::byte value);
template <class T> Result<void> copy_into(std::span<T> dst, std::span<const T> src);
}
```

Rules:

- Allocation failure is a value: `Buffer::alloc` / `Box::make` / `make_shared`
  catch `std::bad_alloc` (or use `new (std::nothrow)`) and return
  `fp::err("allocation failed")`. No exceptions cross the API.
- `ptr`/`ref`/`deref` never null-check; they are address-of and dereference,
  named for readability. Nullable ownership uses `Box`/`Shared`; nullable
  borrows use `std::optional<T*>` or `fp::Maybe`.
- `release` and `adopt` are the only raw-pointer boundary, so ownership
  transfers are greppable.
- `Buffer` is a range (`begin`/`end`/`data`), so `fp::for_each`,
  `fp::transform_inplace`, `fp::map`, and `fp::simd::map_inplace` work on it
  with no adapter.
- `with_buffer` / `with_arena` are the scoped forms: return a *value*, never a
  pointer into the scope.

### `fp/arena.hpp` extensions

```cpp
class Arena {
  // existing alloc/make/reset/used ...
  std::span<std::byte> alloc_bytes(std::size_t n, std::size_t align = alignment);
  template <class T> std::span<T> alloc_span(std::size_t n = 1);  // uninitialized
  std::size_t mark() const;             // checkpoint
  void reset_to(std::size_t mark);      // rollback to checkpoint
};

template <class F> auto with_arena_scope(Arena &a, F f);   // mark + reset_to
```

- Arena paths never fail (a bump is infallible), so they stay `Result`-free and
  exception-free; only owning `Buffer`/`Box` factories return `Result`.
- `mark`/`reset_to` give nested scopes without destroying the arena, so a
  long-lived per-frame arena serves many short-lived stages.
- `alloc_span` replaces `alloc` in new code; `alloc` stays for compatibility.

---

## Wave 2 — views, grids, operators, numerics

### `fp/views.hpp` extensions

```cpp
namespace fp::views {
inline constexpr auto chunk(std::size_t n);        // lazy, non-overlapping
inline constexpr auto slide(std::size_t n);        // lazy, overlapping windows
inline constexpr auto stride(std::size_t n);       // every n-th element
inline constexpr auto take_last(std::size_t n);
template <class R, class F> auto zip_with(R &&other, F f);   // f(a_i, b_i)
template <class R1, class R2> auto zip3(R1 &&a, R2 &&b);     // pairs of triples
}
```

- All borrowing and lazy; `fp::to_vector` materializes.
- `zip_with` fuses the zip and the transform, so a hot pipeline does not
  allocate a pair vector.
- `stride(n)` is the column primitive: over a flat row-major buffer, `drop(j) |
  stride(cols)` is column `j` without a strided view type.

### `fp/grid.hpp` extensions

```cpp
namespace fp {
template <class T, class F> auto map2d_indexed(std::vector<std::vector<T>> const &g, F f);
//   f(i, j, x) -> y ; broadcasting-friendly
template <class T, class F> void map2d_inplace(std::vector<std::vector<T>> &g, F f);
template <class T> std::vector<T> column(std::vector<std::vector<T>> const &g,
                                         std::size_t j);
template <class T> auto windows2d(std::vector<std::vector<T>> const &g,
                                  std::size_t kh, std::size_t kw);
//   vector of kh x kw patches, row-major, non-overlapping by default
}
```

- `map2d_indexed` enables bias add, positional weighting, and index-dependent
  masks without leaving fp.
- `windows2d` is the lazy alternative to `im2col` for CNN patches; it returns
  copies (documented) unless a view variant is added later.

### `fp/ops.hpp` extensions

Named elementwise operators so pipelines stay point-free:

```cpp
namespace fp {
inline constexpr auto abs      = [](auto x) { return x < 0 ? -x : x; };
inline constexpr auto sqrt     = [](auto x) { return std::sqrt(x); };
inline constexpr auto exp      = [](auto x) { return std::exp(x); };
inline constexpr auto log      = [](auto x) { return std::log(x); };
inline constexpr auto log1p    = [](auto x) { return std::log1p(x); };
inline constexpr auto sign     = [](auto x) { return (x > 0) - (x < 0); };
inline constexpr auto min_     = detail::left_curried<std::less<>>{};
inline constexpr auto max_     = detail::left_curried<std::greater<>>{};
template <class T> auto pow(T k);            // curried: pow(2)(x) == x^2
template <class T> auto clamp(T lo, T hi);   // curried: clamp(0,1)(x)
}
```

- `min_`/`max_` avoid the `std::min` macro/name clash; `pow(k)` and `clamp(lo,hi)`
  are curried like the rest of `ops.hpp`.
- `clamp` is also available in scalar and range form in `numerics.hpp`; the
  `ops` version is for pipelines.

### `fp/numerics.hpp`

```cpp
namespace fp {
template <std::ranges::range R> std::vector<double> softmax(R const &r);
template <std::floating_point T> void softmax_rows(std::vector<std::vector<T>> &g);
template <std::ranges::range R> std::vector<double> log_softmax(R const &r);
template <std::ranges::range R> double logsumexp(R const &r);
inline double sigmoid(double z);
inline double relu(double z);
template <class T> T clamp(T value, T lo, T hi);
inline std::vector<double> linspace(double from, double to, std::size_t n);
inline std::vector<double> arange(double from, double to, double step);
inline bool approx_equal(double a, double b, double eps = 1e-9);
inline bool is_finite(double x);
inline double nan_to_num(double x, double replacement = 0.0);
template <class F> double central_difference(F f, double x, double h = 1e-6);
}
```

- Stability: subtract max before `exp`; `logsumexp = max + log(sum)`; `sigmoid`
  finite at extremes; `softmax`/`log_softmax` allocate once.
- `central_difference` is the general finite-difference primitive used by
  gradient checks in `ml` and by any numeric experiment.
- `linspace(from, to, n)` includes both endpoints; `arange` is half-open.

---

## Wave 3 — linear algebra

### `fp/linalg.hpp`

fp's matrix shape is a nested range (`std::vector<std::vector<T>>`); spans are
used for flat vectors. Everything is generic over arithmetic `T`.

```cpp
namespace fp {

// --- products ---
template <class T> std::vector<std::vector<T>>
matmul(std::vector<std::vector<T>> const &a, std::vector<std::vector<T>> const &b);
template <class T> std::vector<std::vector<T>>
batched_matmul(std::vector<std::vector<std::vector<T>>> const &a,
               std::vector<std::vector<std::vector<T>>> const &b);   // (B,m,k)x(B,k,n)
template <class T> std::vector<T>
matvec(std::vector<std::vector<T>> const &a, std::vector<T> const &x);
template <class T> std::vector<std::vector<T>>
outer(std::vector<T> const &a, std::vector<T> const &b);

// --- solve ---
template <class T> Result<std::vector<T>>
solve(std::vector<std::vector<T>> const &a, std::vector<T> const &b);

// --- elementwise ---
template <class T> std::vector<T> hadamard(std::vector<T> const &a,
                                           std::vector<T> const &b);
template <class T> std::vector<T> scale(std::vector<T> const &v, T k);
template <class T> std::vector<std::vector<T>>
add_row_broadcast(std::vector<std::vector<T>> const &g, std::vector<T> const &row);

// --- reductions ---
template <class T> T dot(std::span<const T> a, std::span<const T> b);
template <class T> T norm_l1(std::span<const T> v);
template <class T> T norm_l2(std::span<const T> v);
template <std::ranges::range R> auto argmax(R const &r) -> std::optional<std::size_t>;
template <std::ranges::range R> auto argmin(R const &r) -> std::optional<std::size_t>;
template <std::ranges::range R> double mean(R const &r);
template <std::ranges::range R> double variance(R const &r, std::size_t ddof = 0);
template <class T> std::vector<T> row_sums(std::vector<std::vector<T>> const &g);
template <class T> std::vector<T> col_sums(std::vector<std::vector<T>> const &g);
template <class T> std::vector<T> row_means(std::vector<std::vector<T>> const &g);
template <class T> std::vector<T> col_means(std::vector<std::vector<T>> const &g);
template <class T> std::vector<std::size_t> argmax_rows(std::vector<std::vector<T>> const &g);
template <class T> std::vector<std::size_t> argmin_rows(std::vector<std::vector<T>> const &g);
}
```

- Hot kernels live here so neither project hand-rolls them: `matmul` is
  `i-k-j` ordered with a scalar accumulator, with an optional SIMD path via
  `__has_include(<experimental/simd>)`; `batched_matmul` calls `matmul` per
  batch slice (a single flat implementation comes when a benchmark asks for it).
- `argmax`/`argmin` return `std::optional` (empty range -> `nullopt`); ties
  return the first index.
- `variance` takes `ddof` (`0` population, `1` sample).
- Shape preconditions are documented and asserted in debug; domain code
  validates shapes at its own boundary and then calls fp.
- `transpose` is defined in `fp::grid` (its canonical home); `linalg.hpp`
  includes `grid.hpp`, so `#include <fp/linalg.hpp>` is enough to use it.
  `test/api_linalg_test.cpp` pins that.

### `fp/simd.hpp` extensions

```cpp
namespace fp {
template <class T> void axpy_inplace(std::vector<T> &y, T a, std::vector<T> const &x);
//   y += a * x  (the optimizer update primitive)
}
```

- `axpy_inplace` is what `w -= lr * g` becomes after negation; optimizers stay
  allocation-free.
- `softmax_rows` (in-place row-wise softmax) landed in `fp/numerics.hpp` and
  `map2d_inplace` in `fp/grid.hpp`, where they are available without opting
  into `<experimental/simd>`; neither needs SIMD.

---

## Wave 4 — random, strings, IO, time

### `fp/random.hpp`

```cpp
namespace fp {

class Rng {
public:
  explicit Rng(std::uint64_t seed);
  std::uint64_t next_u64();
  int randint(int lo, int hi);                       // inclusive
  double uniform(double lo = 0.0, double hi = 1.0);
  double normal(double mean = 0.0, double stddev = 1.0);
  bool bernoulli(double p);
  template <class T> void shuffle(std::vector<T> &v);
  std::vector<std::size_t> sample_indices(std::size_t n, std::size_t k); // without replacement
  std::size_t categorical(std::vector<double> const &weights);           // weighted choice
  std::size_t weighted_choice(std::vector<double> const &weights);       // alias
  std::uint64_t seed() const;
};
}
```

- No `std::random_device` anywhere; callers seed explicitly.
- `sample_indices` is Fisher-Yates on `[0,n)` truncated to `k`; `k > n` fails
  via assert (programmer error) or returns all indices (documented).
- `categorical` normalizes internally and fails (assert) on empty/all-zero
  weights; it is the sampling primitive for the LLM.
- Same seed -> same sequence on a platform; tests assert structure, not bits.

### `fp/str` extensions

```cpp
namespace fp::str {
inline std::string to_string(double value, int precision = 17);
inline std::string to_string(int value);
inline std::vector<std::string> split_any(std::string const &s, std::string const &delims);
template <class T> Result<std::vector<T>> parse_numbers(std::string const &s);
}
```

- `to_string(v, 17)` round-trips a `double` exactly.
- `split_any` treats each character in `delims` as a separator.
- `parse_numbers<double>` splits on whitespace/commas and returns `Result`,
  reporting the offending token.

### `fp/io.hpp` extensions

```cpp
namespace fp {
Result<std::vector<std::byte>> read_bytes(std::string const &path);
Result<void> write_bytes(std::string const &path, std::span<const std::byte> data);
Result<void> write_lines(std::string const &path,
                         std::vector<std::string> const &lines);
bool exists(std::string const &path);
Result<void> ensure_directory(std::string const &path);   // like mkdir -p
}
```

- Binary checkpoints use `read_bytes`/`write_bytes`; CSV export and logs use
  `write_lines`.
- `ensure_directory` is what the LLM trainer calls before saving checkpoints;
  it is idempotent.

### `fp/time.hpp`

```cpp
namespace fp {
double now_seconds();                       // monotonic
double elapsed_seconds(double since);
class Stopwatch {
public:
  Stopwatch();
  void reset();
  double elapsed() const;                   // seconds since construction/reset
  double lap();                             // elapsed since last lap, then resets lap
};
}
```

- Monotonic clock only (`std::chrono::steady_clock`); no wall-clock/calendar
  code in fp.
- `Stopwatch` is used for per-step training timing and frame timing in
  `forge-gl`.

---

## Wave 5 — concurrency extras

### `fp/concurrent.hpp` extensions

```cpp
namespace fp {
template <class F> void par_for(std::size_t begin, std::size_t end, F f);
template <class T, class F> void par_for_each_index(std::vector<T> const &v, F f);
template <class T, class R, class F>
void par_map_to(std::vector<T> const &src, std::vector<R> &dst, F f);
}
```

- `par_for(start, end, f)` takes `f(i)`; it is the tiling primitive for
  `linalg::matmul` and per-row transforms.
- `par_map_to` writes into a caller-owned destination, so a per-frame loop does
  not allocate a result vector.
- `par_map` / `par_for_each` / `par_reduce` and `ThreadPool` remain as they are.

---

## Wave 6 — optional modules

### `fp/serialize.hpp` (optional)

Generic text/binary round-trip for ranges of arithmetic types, so `ml` can drop
its hand-written format:

```cpp
namespace fp {
template <std::ranges::range R> std::string to_text(R const &r, int precision = 17);
template <class T> Result<std::vector<T>> from_text(std::string_view text);
template <std::ranges::range R> std::vector<std::byte> to_bytes(R const &r);
template <class T> Result<std::vector<T>> from_bytes(std::span<const std::byte> data);
}
```

- fp owns the encoding; `ml` owns the schema (which vector is which parameter).

### `fp/autodiff.hpp` (optional)

Forward-mode dual numbers, the general AD primitive:

```cpp
namespace fp {
template <class T = double> struct Dual {
  T value{}, deriv{};
  // +, -, *, /, exp, log, sin, cos, pow, sqrt
};
template <class F, class T = double> T derivative(F f, T x);
}
```

- Reverse-mode (parameter graphs, layers) stays in `ml/nn`: it is a domain
  concern, not a general numeric one.

---

## Wave 8 — post-audit polish

A whole-library audit (correctness, performance, ergonomics, process) landed as
a polish wave — no new modules, but one deliberate rename and several measured
changes.

### Correctness

| Fix | Where |
|---|---|
| `fp::unique` passed its third argument in the predicate slot (`std::unique(first, last, last)`), deduplicating with a nonsense predicate whenever `vec.hpp` was the header in scope. | `vec.hpp` |
| `ranges::span` walked the range twice (`find_if_not` + a second pass); input ranges cannot survive that. Now one pass. | `ranges.hpp` |
| `ranges::windows(r, 0)` produced nothing useful and shifted the buffer with `erase(begin())` (O(n) per element). Now empty for `n == 0`, ring buffer otherwise. | `ranges.hpp` |
| `grid.hpp`'s `for_each_index(rows, cols, f)` collided with `inplace.hpp`'s `for_each_index(range, f)` and silently won resolution for nested vectors. **Renamed to `for_each_cell`.** | `grid.hpp` |
| `inplace.hpp` used `std::vector` without including `<vector>` (self-containment). | `inplace.hpp` |

### Performance (measured, not assumed)

| Change | Result |
|---|---|
| `read_file`/`read_all` size a seekable stream and issue one bulk read; `istreambuf_iterator` remains the pipe fallback. | removes one virtual call per byte |
| `Parser`: new `scan_while`/`scan_while1` primitives yield `string_view` slices instead of a `vector<char>` per token (`bench/parse_bench.cpp`). | 137 µs → 31 µs for 2 000 ints (16x → 3.4x a hand-written loop) |
| `matmul` flat-span overload (`matmul(a, m, k, b, n)`). | 2x faster than the nested form at 128³ |
| `sample_indices` samples by rejection when `k < n/4`. | O(n) → O(k) for sparse draws |
| `sort_by_cached`/`sort_by_cached_inplace`: keys computed once. | 1.4x faster for allocating keys, 1.9x slower for `int` keys — hence a separate function, not a change to `sort_by` |
| Reserves where the size is known (`take`/`drop`/`scan`/`zip`/`enumerate`, `str::split`, `read_lines`). | fewer reallocations |

### Ergonomics

- Error-valued returns (`Result`/`Outcome`/`Validation` and optional-returning
  lookups) are `[[nodiscard]]` — 124 declarations; the test suite compiles
  warning-free.
- `Channel::try_send` (non-blocking send; `false` when full or closed).
- `io.hpp` path parameters take `std::string_view`.
- `fp::ops` arithmetic operators are `noexcept`.
- `bench/bits_bench.cpp` and `bench/parse_bench.cpp` wired into the bench run.

### Process

- `test/api_vec_test.cpp`, `test/api_ranges_test.cpp`,
  `test/api_grid_test.cpp` instantiate each module with **only that module
  included**, and `test/parity_test.cpp` asserts the `vec`/`ranges` shared
  combinators agree — the regression net for the `unique` class of bug.
- `.docs/getting-started.md` gained a "Conventions and guarantees" table
  (errors, allocation, thread safety, `noexcept`, preconditions, performance).

### Deliberately not done

- **Collapsing the `vec`/`ranges` duplication into one implementation.** Both
  headers are meant to work standalone; after fixing the divergences, the
  maintenance win did not justify a layering refactor of the two most-used
  headers. Parity tests enforce agreement instead, and `ranges.hpp` stays the
  recommended surface for new code.
- **De-erasing `Parser<T>`.** Templating the parser on its callable is the real
  fix for the remaining ~3.4x gap, but it changes the type of every parser.
  `scan_while` plus the benchmark make the cost visible and avoidable.
- **A blanket `noexcept` sweep.** Generic algorithms call user callables, which
  may throw; only genuinely non-throwing code is annotated.

---

## Wave 9 — measured performance pass

A second, performance-only audit: every change below has a before/after
benchmark in `bench/`, and two audit hypotheses were **rejected by
measurement** and reverted.

### Big wins

| Change | Before | After | Speedup |
|---|---|---|---|
| `linalg::matmul` — 4x4 register tile + `__restrict__` row pointers (the old loop was scalar and L3-bound) | 6.65ms (256²) / 53.8ms (512²) | 1.63ms / 13.6ms | **4.1x / 4.0x** |
| `linalg::matvec` — four accumulators per row | 38.1us | 8.5us | **4.5x** |
| `linalg::row_sums` — four accumulators per row | 725us | 181us | **4.0x** |
| `linalg::norm_l2` / `mean` / `variance` — multi-accumulator | 774/774/1540us | 202/223/499us | **3.8x / 3.5x / 3.1x** |
| `linalg::dot` | 815us | 396us | **2.1x** |
| `str::split(s, char)` — `find` loop instead of `stringstream`+`getline` | 6.27ms (100k fields) | 0.89ms | **7.0x** |
| `str::to_int` / `to_double` — `from_chars`, no exception on bad input | 30ns / 1.10us (invalid) | 7ns / 11ns | **4.2x / 99x** |
| `str::to_lower`/`to_upper` — ASCII range check instead of per-char `tolower` | 1.44ms (600KB) | 0.30ms | **4.8x** |
| `str::to_string(double, p)` — `to_chars` | 263ns | 65ns | **4.0x** |
| `str::join` (range) — direct append + `to_chars`, no ostringstream | 26.6us (1k ints) | 9.4us | **2.8x** |
| `serialize::from_text` — `from_chars` scan, no stream copy | 1.74ms (10k doubles) | 0.20ms | **8.7x** |
| `serialize::to_text` — `to_chars` | 2.49ms | 0.64ms | **3.9x** |
| `Rng::normal` — the distribution is state, so the cached Box-Muller sample survives | 32.6ns | 17.2ns | **1.9x** |
| `Rng::uniform` / `bernoulli` — built from one engine word | 6.5/6.8ns | 2.2/2.5ns | **2.9x / 2.7x** |
| `fp::Categorical` — alias table (Vose) instead of re-summing the weights per draw | 11.5us (k=10k) | 8.4ns | **~1370x** |
| `io::read_bytes` — bulk seek+read instead of `istreambuf_iterator` | ~30ms (8MB) | 1.25ms | **~24x** |
| `simd::par_map_inplace` — maps each slice in place (removed two full-buffer copies) | — | — | traffic halved |
| `ThreadPool`-based `par_*` — dynamic chunks, caller participates, latch instead of a future per chunk | `par_map` 533us, `par_reduce` 153us | 457us, 126us | **1.17x / 1.22x** |

### Rejected by measurement (reverted)

| Hypothesis | Result |
|---|---|
| `RingBuffer`: pad `head_`/`tail_` onto separate cache lines to kill false sharing | **No difference** (14.2 vs 14.3 ns/msg SPSC, `spsc_probe`). The two indices have to cross between cores on every operation anyway, so there is no false sharing to remove. Padding reverted; a lazy (batched) head read was measured **2x slower** as well. |
| `Actor`: drain the mailbox with `try_recv` before blocking again | **~7% slower** for a consumer that keeps up — the extra lock per message costs more than the avoided condvar wait. Reverted. |
| `grid::transpose`: 32x32 blocked transpose | Only **1.13x** at 1024² (the grid is L3-resident; blocking pays off beyond L3). Kept, because it is never slower and scales up. |

### Correctness fix found while measuring

- **`Task::then`/`and_then` only checked the stop token before waiting for the
  upstream stage**, so cancelling a chain while the upstream was still running
  let the continuation execute anyway — contradicting the documented "skipped
  if the chain was stopped". The check now also runs after `fut.get()`, which
  also made the `Cancellation.ThenSkippedWhenCancelled` test deterministic
  (it was racing a 50ms sleep against the test thread and failed ~7/10 under
  TSan). TSan is green again: 389/389, zero data-race warnings.

### Also in this wave

- `str::split`/`lines`/`join`/`chunk`/`split_any` reserve up front; `replace_all`
  is one pass instead of `std::string::replace` per hit; `str::chunk` reserves.
- `serialize` dropped `<sstream>`/`<iomanip>`; `string.hpp` dropped `<iomanip>`.
- `simd::map_inplace` accepts a `std::span` (any contiguous slice, `Buffer`,
  arrays) and the vector overload delegates to it; `simd::gather` checks its
  index precondition with `assert` instead of a throwing `.at()`.
- `map_values` reserves; `memoize` constructs the value in place instead of
  copying it twice; `grid::windows2d` reserves the (known) patch count.
- New benches: `string`, `random`, `serialize`, `ranges`, `io` (plus extra rows
  in `linalg`, `simd`, `concurrent`), all wired into `scripts/run_bench.sh`.

### Plan-drift follow-ups (landed)

- `linalg.hpp` now documents that `transpose` comes with it; `bench/` gained the
  missing `par_for` row (acceptance criterion #1: 1.59ms sequential vs 0.46ms on
  8 threads); the stale "GPU ... beyond `par_for`" out-of-scope line now points
  at `GPU.md`, which owns that tier.
- Coverage for the Wave 9 additions: `test/api_linalg_test.cpp` (new: every
  linalg entry point with only `linalg.hpp` included, including the transpose
  re-export), plus new cases for `par_for`/`submit`/exception propagation through
  the `par_*` helpers, `map_inplace(span)` and `par_map_inplace`, the `windows`
  ring wrap-around, `split(char)` vs `split(string)` parity, strict
  `to_int`/`to_double` (trailing garbage, leading `+`, out of range), non-ASCII
  case mapping, `from_text` edges, multi-accumulator reduction accuracy against
  naive sums, `string_view` paths, and `memoize` with a non-trivial value type.
  Test suite: 405 tests, 0 warnings on GCC and Clang.

### Still open (measured, deliberately deferred)

- **`numerics` softmax/logsumexp are `std::exp`-bound** (~5.8ns/element): a
  vectorized exp lives in the opt-in `simd.hpp` (`map_exp`, ~3x); the docs now
  point there. Making `numerics` depend on `<experimental/simd>` would break its
  "plain C++20" contract.
- **`Parser<T>` erasure** (~3.4x a hand-written loop): fixed in Wave 8 by
  `scan_while`; de-erasing the parser remains an API-breaking change.
- **`Task`/`Async` continuations still use `std::async`** (a thread per step).
  A pool-backed version needs either a global pool (lifetime hazards) or a pool
  parameter (API change), so it is documented rather than changed.

---

## Compatibility rules

- Header-only, `namespace fp`, C++20, zero external dependencies.
- Every new header is added to **both** `src/fp/` and
  `include/forgefp/fp/` (the mirror), and included from `all.hpp` unless it is
  opt-in (`autodiff.hpp` follows `simd.hpp`'s opt-in pattern; `serialize.hpp`
  can be in `all.hpp`).
- No existing API changes. Extensions are additive; if an existing function
  needs a faster path, add an overload or an `*_inplace` sibling.
- New modules get: header, tests in `test/<module>_test.cpp`, and a benchmark
  in `bench/` for anything claiming zero-cost.

## Acceptance

1. `bench/` shows fp primitives within noise of hand-written loops for:
   `for_each`, `transform_inplace`, `sort_by_inplace`, `matmul`, `softmax`,
   `par_for`, and `Buffer`/`with_buffer` vs `std::vector` / raw `new[]` +
   `delete[]`. All of these have a row now: `matmul` is 4x *faster* than the
   naive loop (Wave 9), `par_for` is 3.5x faster than the sequential index loop
   on 8 threads, the rest are within noise.
2. `ml` docs rewritten so every file has a "ForgeFP usage" section and no
   hand-rolled loop where an fp primitive exists.
3. `forge-gl` re-audited: every loop it still owns is either an SDL call or a
   kernel that now lives in `fp::linalg`/`fp::simd`.
4. Existing fp tests still pass; new module tests cover golden values, error
   paths, stability, and (for algorithms) permutation/partition properties.

## Implementation order

1. `fp/inplace.hpp` + `fp/scope.hpp`.
2. `fp/memory.hpp` + `Arena` extensions.
3. `fp/views.hpp` + `fp/grid.hpp` extensions.
4. `fp/ops.hpp` + `fp/numerics.hpp`.
5. `fp/linalg.hpp` + `fp/simd.hpp` extensions (bench first, then optimize).
6. `fp/random.hpp`.
7. `fp/str` + `fp/io.hpp` + `fp/time.hpp`.
8. `fp/concurrent.hpp` extensions.
9. Rewrite `ml/docs/*` against the new surface, adding a "ForgeFP usage" block
   to every spec.
10. Re-audit `forge-gl` against the new surface.
11. Optional: `fp/serialize.hpp`, `fp/autodiff.hpp`.

## Out of scope

- Owning `Matrix`/`Tensor` types in fp (domain containers stay in `ml/core/`).
- Object pools (`Arena::mark`/`reset_to` covers nested scopes; a pool is a
  different lifetime model).
- Distributed linear algebra. GPU work is not out of scope: it has its own
  plan in [`GPU.md`](GPU.md), whose phases 0-3 are landed (phase 4 is next).
- Reverse-mode autodiff.
- Calendar/time-zone code (`fp::time` is monotonic timing only).

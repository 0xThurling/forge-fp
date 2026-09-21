# Memory & ownership — `memory.hpp` and `arena.hpp`

ForgeFP normally hides memory: `std::vector` owns its elements, combinators
return values, and nothing leaks. `memory.hpp` opens the box for the cases
where that is not enough — raw buffers, single objects with custom lifetimes,
scoped scratch space, and byte-level copying — while keeping the same
discipline: **ownership is a value, borrowing is a value, and failure is a
value**.

```cpp
#include <fp/memory.hpp>
#include <fp/arena.hpp>
```

**Why this exists:** low-level work usually degrades into raw `new`/`delete`,
null checks, and ownership comments. The functions here give that work a
vocabulary: `Box<T>` says "this owns one object", `Buffer<T>` says "this owns a
block", `with_buffer` says "this memory dies at the end of the scope", and
`ptr`/`ref` say exactly what they do. None of it costs anything at runtime —
the wrappers are `constexpr` one-liners or a single-allocation RAII block.

## The three questions

Every memory API answers three questions explicitly:

1. **Who owns it?** `Box` (one object), `Buffer` (a block), `Shared` (shared),
   `Arena` (a scope), or a raw pointer you got from `release()`.
2. **How long does it live?** Until the owner dies, until `reset()`, until the
   `with_*` scope ends, or until `release()` — stated in the name.
3. **What happens on failure?** Owning factories return `fp::Result`; arena
   allocation is infallible (a bump cannot fail) and therefore `Result`-free.

## Movement and access

These are named, `constexpr`, and `noexcept`. They compile to nothing:

```cpp
int x = 1;
int old = fp::exchange(x, 5);       // old == 1, x == 5

std::string s = "hello";
std::string t = fp::move(s);        // explicit move (s is left valid-but-unspecified)

int *p = fp::ptr(x);                // address-of: &x, without the & ambiguity
int &r = fp::ref(p);                // named dereference: *p
int &r2 = fp::deref(p);             // alias of ref
int const &c = fp::as_const(x);     // const view of a mutable object
```

`ptr`/`ref`/`deref` never null-check — they are address-of and dereference,
named for readability. Nullable ownership is `Box`/`Shared`; a nullable borrow
is `std::optional<T*>` (or `fp::Maybe` on the value side).

### Object lifetime in raw memory

```cpp
alignas(std::string) std::byte raw[sizeof(std::string)];

auto *s = fp::construct_at(reinterpret_cast<std::string *>(raw), "hi");
// ... use *s ...
fp::destroy_at(s);

std::span<std::string> span{reinterpret_cast<std::string *>(raw), 1};
fp::destroy(span);                  // destroys every element
```

`construct_at` is placement new with a name; `destroy_at`/`destroy` are the
matching destructor calls. They are how you use `Buffer` with non-trivial
payloads (see below).

## `Box<T>` — one object, one owner

`Box` is a `unique_ptr`-shaped owner with value semantics: movable, not
copyable, and constructible through a fallible factory.

```cpp
auto box = fp::Box<Widget>::make(1, 2);   // Result<Box<Widget>>
if (!box.is_ok())
  return box;                             // err("allocation failed")

box.value()->draw();
*box.value() = Widget{3, 4};

Widget *raw = box.value().release();      // give up ownership (explicit)
delete raw;

box.value().reset();                      // destroy now
```

`Box::make` catches `std::bad_alloc` and returns `fp::err("allocation
failed")`, so allocation failure flows through `Result` instead of an
exception. Constructor exceptions other than `bad_alloc` still propagate (they
are bugs in the payload, not allocation failures).

## `Shared<T>` — shared ownership

```cpp
auto shared = fp::make_shared<Model>(config);   // Result<Shared<Model>>
shared.value()->fit(data);

auto box = fp::Box<Model>::make(config).value();
fp::Shared<Model> moved = fp::share(std::move(box));   // Box -> Shared
```

`Shared<T>` is `std::shared_ptr<T>`; `make_shared` is the fallible factory, and
`share` promotes a `Box` without a second allocation.

## `Buffer<T>` — an owning contiguous block

`Buffer<T>` is a raw, contiguous block of `T` with one allocation and one free.
It is **a range** (`begin`/`end`/`data`), so every fp algorithm — including
`fp::simd::map_inplace` — works on it directly.

```cpp
auto buf = fp::Buffer<double>::alloc(1024);   // Result<Buffer<double>>
if (!buf.is_ok())
  return buf;

fp::fill(buf.value(), 0.0);                   // elements are raw until written
buf.value()[0] = 1.5;

double total = fp::fold_left(buf.value(), 0.0, fp::plus);
auto view = buf.value().span();               // std::span<double>
```

Factories (all return `Result` because they allocate):

| Factory | Result |
|---|---|
| `alloc(n)` | uninitialized block of `n` |
| `zeros(n)` | value-initialized block |
| `copy_of(span)` | copy of an existing span |
| `from({...})` | copy of an initializer list |

Operations:

```cpp
buf.value().fill(1.0);
buf.value().clone();                 // Result<Buffer<T>> — deep copy
buf.value().resized(2048);           // Result<Buffer<T>> — new block, prefix copied
```

Ownership boundaries are greppable — there are exactly two:

```cpp
double *raw = buf.value().release();             // explicit leak, caller owns
auto again = fp::Buffer<double>::adopt(raw, 1024);  // explicit takeover
```

**Restriction:** `Buffer<T>` is raw storage and requires
`std::is_trivially_destructible_v<T>` (numbers, bytes, plain structs). For
objects with destructors use `Box<T>` (single) or a `std::vector<T>` (many).
That restriction is what lets `Buffer` be a single `operator new`/`delete`
pair with no per-element bookkeeping.

## Scoped allocation — `with_buffer` / `with_ptr`

The safest way to use raw memory: the owner cannot escape the scope, and the
callback returns a *value*.

```cpp
auto total = fp::with_buffer<double>(n, [&](std::span<double> scratch) {
  fp::map_to(input, scratch, activate);
  fp::transform_inplace(scratch, normalize);
  return fp::fold_left(scratch, 0.0, fp::plus);
});
// total is Result<double>; the buffer is freed here
```

`with_ptr` is the pointer-flavored version when you need raw arithmetic:

```cpp
auto r = fp::with_ptr<float>(3, [](float *p, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i)
    p[i] = static_cast<float>(i);
  return p[n - 1];
});
```

Both return `Result<R>`: if the allocation fails, the callback is not called
and the error is returned. `void` callbacks are supported.

## Raw copying

```cpp
std::vector<int> dst(3), src = {1, 2, 3};

fp::copy_bytes<int>(dst, src);              // element copy, non-overlapping
fp::fill_bytes<int>(dst, std::byte{0});     // memset

auto r = fp::copy_into<int>(std::vector<int>(2), src);
// r.is_ok() == false -> "copy_into: destination too small"
```

`copy_bytes`/`fill_bytes` do not check sizes (you assert them); `copy_into`
does, and returns a `Result`.

## `Arena` — bump allocation for scopes

`Arena` hands out aligned raw memory from growing blocks and frees *everything*
at once with `reset()`. It is the realtime-safe allocation path: no
`malloc`/`free` churn, no fragmentation.

```cpp
fp::Arena frame(64 * 1024);

auto verts = frame.alloc_span<Vertex>(n);   // span, uninitialized
auto *widget = frame.make<Widget>(x, y);    // construct in place

frame.used();    // bytes allocated so far
frame.reset();   // next allocation reuses the same memory
```

Every block is 64-byte aligned, and `alloc<T>`/`alloc_span<T>` honor the type's
own alignment (up to 64). When a block fills up the arena adds a new, larger
block, so previously handed-out pointers are never invalidated — including
across `reset()`, which only rewinds offsets.

### Checkpoints — nested scopes in one arena

`mark()` records the current high-water mark; `reset_to(mark)` rewinds every
allocation made after it and leaves earlier ones untouched.

```cpp
fp::Arena arena(1 << 20);

auto *persistent = arena.make<Mesh>();
const auto mark = arena.mark();

for (auto &job : jobs) {
  auto scratch = arena.alloc_span<double>(job.size);  // per-job scratch
  process(job, scratch);
  arena.reset_to(mark);                               // reclaim just this job
}
```

`with_arena_scope` packages the mark/rollback pair so it also runs on
exceptions:

```cpp
auto result = fp::with_arena_scope(arena, [](fp::Arena &a) {
  auto tmp = a.alloc_span<int>(64);
  return tmp[0];                // return a value, never a pointer into the arena
});
```

`with_arena` creates a fresh arena for a callback (the scoped form):

```cpp
auto scaled = fp::with_arena(1 << 20, [](fp::Arena &a) {
  auto *v = a.alloc<Vertex>(n);
  return fp::map(verts, shade);   // value out; arena dies here
});
```

### The arena contract

- **Scoped, not shared.** The arena lives inside one scope; return *values*,
  never arena pointers.
- **`reset()` does not destroy.** Keep payloads trivially destructible
  (samples, vertices, events) unless you track destructors yourself.
- **Consumer-side.** Combinators still return default-allocator
  `std::vector`; route hot stages through the arena yourself.

## Which tool for which job

| Need | Use | Failure mode |
|---|---|---|
| A growable, value-semantic container | `std::vector<T>` | throws `bad_alloc` |
| A fixed raw block with RAII ownership | `Buffer<T>` | `Result` |
| One object with a custom lifetime | `Box<T>` | `Result` |
| Shared ownership | `Shared<T>` | `Result` |
| Scratch memory that dies at scope end | `with_buffer` / `with_ptr` | `Result` |
| Many short-lived allocations, bulk reset | `Arena` | infallible |
| Byte-level copy | `copy_bytes` / `copy_into` | `Result` (for `copy_into`) |

## Gotchas

- **`Buffer` elements are raw.** Reading before writing is undefined. Use
  `zeros`, `fill`, or write every element first.
- **`release()` is the only leak**, `adopt()` the only takeover. If neither
  appears in your code, ownership never escapes.
- **Arena pointers are only valid until `reset()`** (or `reset_to` below their
  mark). Never store them in a returned value.
- **`reset_to` requires a valid mark.** Marks from a *different* arena, or
  after a `reset()`, are meaningless.
- **`with_buffer` returns values, not views.** A `std::span` returned from the
  callback would dangle — the compiler cannot stop you, the contract does.
- **Alignment.** `Arena::alloc_bytes` clamps alignment requests above the
  arena's own 64-byte alignment; allocate over-aligned types with
  `alloc_span<T>` so the type's `alignof` is honored.

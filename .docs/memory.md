# Memory — `arena.hpp`

`Arena` is a bump allocator: it hands out raw, uninitialized memory from a
growing buffer and frees *everything* with one `reset()`. It's the standard
realtime-safe allocation path (no `malloc`/`free` churn, no fragmentation).

```cpp
#include <fp/arena.hpp>
```

**Why an arena:** hot loops and realtime threads (audio render callbacks, frame
loops) allocate many short-lived objects. `malloc`/`free` per object is the
cost, and fragmentation is the long-term problem. An arena does neither: a
pointer bump allocates, a `reset()` reclaims the whole thing. The same pattern
serves per-frame scratch (3D scenes), per-block buffers (audio), per-batch
intermediates (ML), and `thread_local` arenas so workers never contend on the
global allocator.

## API

```cpp
class Arena {
    explicit Arena(size_t block_size = 64 * 1024);
    template <class T> T* alloc(size_t n = 1);          // aligned, uninitialized
    template <class T, class... Ts> T* make(Ts&&...);   // construct in place
    void reset();                                        // reclaim everything
    size_t used() const;                                 // bytes in use
};

template <class F> auto with_arena(size_t block_size, F f);  // scoped arena
```

## Usage

```cpp
fp::Arena frame;

auto* verts = frame.alloc<Vertex>(n);   // raw, uninitialized — write into it
auto* obj   = frame.make<Widget>(x, y); // constructs Widget{x, y} in place

frame.used();   // bytes allocated so far
frame.reset();  // next alloc reuses the same memory
```

`with_arena` scopes the arena so it can't outlive the block it serves:

```cpp
auto scaled = fp::with_arena(1 << 20, [](fp::Arena& a) {
    auto* v = a.alloc<Vertex>(n);        // scratch intermediates
    return fp::map(verts, shade);        // return a *value*, not arena pointers
});
// arena reset + destroyed here
```

## The contract (read this)

The arena is only safe under a discipline; `with_arena` enforces it for you:

- **Scoped, not shared.** The arena must live inside one scope and never escape
  it. Return *values* (copied out before `reset()`), never arena pointers.
- **`reset()` does not destroy.** `alloc`/`make` construct; nothing is
  destructed until the arena itself dies. Keep payloads trivially destructible
  (samples, vertices, events) — don't store owning types like `std::string`
  unless you track destructors yourself.
- **Consumer-side.** Combinators still return default-allocator
  `std::vector`; you route hot stages through the arena yourself. For
  `std::pmr::vector` consumers, use `std::pmr::monotonic_buffer_resource` over
  the same buffer.

These rules exist so an arena stays a *pure* performance tool: under them, the
enclosing function is still pure (same input → same output), which is what
keeps it composable with `map`/`pipe`/`par_map`.

## When to use it

Per-frame scratch (3D scenes, UI events), per-block buffers (audio), per-batch
intermediates (ML), and `thread_local` arenas so `ThreadPool`/`par_map` workers
never contend on the global allocator. If you need to reuse *individual*
objects at irregular rates instead of bulk-resetting, that's a different
primitive (an object pool) — not provided by `Arena`.

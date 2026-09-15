# foundation.md — generic primitives for a wide domain surface

**Design principle:** ForgeFP is domain-agnostic by contract. The motivating
workloads — ML/AI, 3D, UI/TUI, concurrent programs, audio/video/DSP (possibly
an audio workstation) — decide *what* primitives are worth building, but no
entry below mentions a domain in its API. Domain code is user-side
composition of these primitives; nothing here ships as `dsp.hpp`, `math.hpp`
activations, MIDI, or tensors.

## Implemented

| Feature | Location |
|---|---|
| `scan` (stateful fold, all intermediates) | `vec.hpp` |

## Backlog

> **Where each entry lands:** primary files live in `src/fp/`; existing
> headers are appended to, new headers are created and added to `all.hpp`.
> The two-tree workflow in `.implementation/README.md` requires mirroring
> every change to `include/forgefp/fp/<module>.hpp` (trees must stay
> byte-identical).

## 1. `map.hpp` — associative-container combinators [P0]

**Lands in:** `src/fp/map.hpp` — new header (add to `all.hpp`, mirror to
`include/forgefp/fp/map.hpp`)

```
template <class K, class V> std::optional<V> lookup(std::map<K, V> const&, K const&);
template <class K, class V, class F> std::map<K, V> map_values(std::map<K, V> const&, F);
template <class K, class V, class F> std::map<K, V> filter(std::map<K, V> const&, F);
template <class K, class V, class F> std::map<K, V> merge_with(std::map<K, V> const&,
                                                               std::map<K, V> const&, F);
template <class K, class V> std::vector<K> keys(std::map<K, V> const&);
template <class K, class V> std::vector<V> values(std::map<K, V> const&);
template <class K, class V> std::map<K, V> to_map(std::vector<std::pair<K, V>> const&);
```

**Why:** Every target domain builds keyed tables — hyper-param dicts, node
registries, key-value stores, plugin params — and the library already
*produces* maps (`vec::group_by` returns `unordered_map`) with zero way to
consume them. Lookup/transform/filter/merge on `std::map` (and
`unordered_map` via the same signatures) is the missing half of the
collection story. P0: `group_by` output is currently dead-end data.

**Implementation** — on key collision `merge_with` runs `combine`; callers
wanting "this side wins" pass `[](V a, V) { return a; }`

```cpp
template <class K, class V>
std::optional<V> lookup(std::map<K, V> const &m, K const &k) {
  auto it = m.find(k);
  return it == m.end() ? std::nullopt : std::optional<V>(it->second);
}

template <class K, class V, class F>
std::map<K, V> map_values(std::map<K, V> const &m, F f) {
  std::map<K, V> out;
  for (auto const &[k, v] : m)
    out.emplace(k, f(v));
  return out;
}

template <class K, class V, class F>
std::map<K, V> merge_with(std::map<K, V> const &a, std::map<K, V> const &b,
                          F combine) {
  std::map<K, V> out = a;
  for (auto const &[k, v] : b) {
    auto it = out.find(k);
    if (it == out.end())
      out.emplace(k, v);
    else
      it->second = combine(it->second, v);
  }
  return out;
}
```

## 2. `parse.hpp` — parser combinators [P0]

**Lands in:** `src/fp/parse.hpp` — new header (add to `all.hpp`, mirror to
`include/forgefp/fp/parse.hpp`)

```
template <class T> using Parser =
    std::function<Result<std::pair<T, std::string_view>>(std::string_view)>;

Parser<char> char_(char c);
Parser<std::string> string_(std::string_view s);
template <class T> Parser<std::vector<T>> many(Parser<T>);
template <class T> Parser<std::vector<T>> some(Parser<T>);
template <class T, class D> Parser<std::vector<T>> sep_by(Parser<T>, Parser<D>);
template <class T> Parser<std::optional<T>> optional(Parser<T>);
template <class A, class B, class F> Parser<B> map(Parser<A>, F);
template <class A, class B, class F> Parser<B> and_then(Parser<A>, F);
template <class A> Parser<A> or_(Parser<A>, Parser<A>);
template <class T> Result<T> run(Parser<T>, std::string_view);
```

**Why:** Byte/char streams are everywhere in the target domains — MIDI
bytes, config/INI, CSV datasets, shader snippets, UI markup. The library's
`Result`/`Validation` are exactly the parse-failure carriers, so a small
combinator set turns "parse this format" into `run(sep_by(float_, char_(',')), csv)`
with no hand-rolled state machines. This is the flagship missing FP feature
(parsec/nom analogue) and the strongest argument for `Result` over
exceptions. P0.

**Implementation** — fail = `err` with the remaining input; success =
`ok({value, leftover})`

```cpp
template <class T> using Parser =
    std::function<Result<std::pair<T, std::string_view>>(std::string_view)>;

inline Parser<char> char_(char c) {
  return [c](std::string_view s) -> Result<std::pair<char, std::string_view>> {
    if (s.empty() || s.front() != c)
      return err<std::pair<char, std::string_view>>("expected '" +
                                                    std::string(1, c) + "'");
    return ok(std::pair<char, std::string_view>{c, s.substr(1)});
  };
}

template <class T>
Parser<std::vector<T>> many(Parser<T> p) {
  return [p = std::move(p)](std::string_view s)
      -> Result<std::pair<std::vector<T>, std::string_view>> {
    std::vector<T> out;
    for (;;) {
      auto r = p(s);
      if (!r.is_ok())
        break;
      out.push_back(r.value().first);
      s = r.value().second;
    }
    return ok(std::pair<std::vector<T>, std::string_view>{std::move(out), s});
  };
}

template <class A>
Parser<A> or_(Parser<A> a, Parser<A> b) {
  return [a = std::move(a), b = std::move(b)](std::string_view s) {
    auto r = a(s);
    return r.is_ok() ? r : b(s);
  };
}

template <class T, class D>
Parser<std::vector<T>> sep_by(Parser<T> p, Parser<D> sep) {
  return [p = std::move(p), sep = std::move(sep)](std::string_view s)
      -> Result<std::pair<std::vector<T>, std::string_view>> {
    std::vector<T> out;
    auto first = p(s);
    if (!first.is_ok())
      return ok(std::pair<std::vector<T>, std::string_view>{std::move(out), s});
    out.push_back(first.value().first);
    s = first.value().second;
    for (;;) {
      auto sp = sep(s);
      if (!sp.is_ok())
        return ok(std::pair<std::vector<T>, std::string_view>{std::move(out), s});
      auto item = p(sp.value().second);
      if (!item.is_ok())
        return err<std::pair<std::vector<T>, std::string_view>>(
            "expected item after separator");
      out.push_back(item.value().first);
      s = item.value().second;
    }
  };
}
```

## 3. `stream.hpp` — a `Stream<T>` abstraction [P1]

**Lands in:** `src/fp/stream.hpp` — new header (add to `all.hpp`, mirror to
`include/forgefp/fp/stream.hpp`)

```
template <class T> class Stream {   // transport-agnostic
  // pull:  built from std::function<std::optional<T>()>
  // push:  built from Channel<T>
  template <class F> auto map(F) -> Stream<R>;
  template <class F> auto filter(F) -> Stream<T>;
  void subscribe(F on_item);
  // planned: merge, scan, debounce, throttle
};
```

**Why:** Every target domain is a producer of a *stream of items*: UI events,
audio frames, ML batches, video frames, network messages. `Channel` is the
transport; `Stream` is the transform surface on top of it. One abstraction,
usable synchronously (pull) or across threads (push over `Channel`). P1:
`Channel` already covers the raw plumbing; `Stream` is what makes pipelines
declarative.

**Implementation** — minimal pull-based core (map/filter); `scan` reuses
`vec.hpp`'s when added; `merge`/`debounce`/`throttle` are P2 refinements

```cpp
template <class T> class Stream {
public:
  using Source = std::function<std::optional<T>()>;

  explicit Stream(Source src) : src_(std::move(src)) {}
  Stream(Channel<T> &ch) : src_([&ch] { return ch.try_recv(); }) {}

  template <class F> auto map(F f) const {
    using R = std::invoke_result_t<F, T>;
    return Stream<R>([src = src_, f = std::move(f)]() -> std::optional<R> {
      auto x = src();
      return x ? std::optional<R>(f(*x)) : std::nullopt;
    });
  }

  template <class F> auto filter(F pred) const {
    return Stream<T>([src = src_, pred = std::move(pred)]() -> std::optional<T> {
      for (;;) {
        auto x = src();
        if (!x || pred(*x))
          return x;
      }
    });
  }

  template <class F> void subscribe(F on_item) const {
    for (;;) {
      auto x = src_();
      if (!x)
        break;
      on_item(*x);
    }
  }

private:
  Source src_;
};
```

## 4. `RingBuffer<T>` — lock-free SPSC [P1]

**Lands in:** `src/fp/concurrent.hpp` — existing header (transport
primitives: `Channel`, `ThreadPool` live here)

```
template <class T> class RingBuffer {
  explicit RingBuffer(size_t capacity);          // power-of-two internally
  bool push(T t);                                // false when full (non-blocking)
  std::optional<T> try_pop();
  size_t size() const;
};
```

**Why:** Realtime audio/video threads cannot block on a mutex — a render
thread must never take `Channel`'s lock. A single-producer/single-consumer
lock-free ring is the realtime-safe transport with generic payload `T`.
`Channel` stays the control path (multi-thread, blocking, bounded);
`RingBuffer` is the data path (realtime, non-blocking). P1: only needed
once realtime threads appear.

**Implementation** — classic SPSC: `std::atomic` head/tail, power-of-two
mask, `acquire`/`release` orderings

```cpp
template <class T> class RingBuffer {
public:
  explicit RingBuffer(size_t capacity) : cap_(1) {
    while (cap_ < capacity)
      cap_ <<= 1;
    buf_.resize(cap_);
  }

  bool push(T t) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= cap_)
      return false;
    buf_[tail & (cap_ - 1)] = std::move(t);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  std::optional<T> try_pop() {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail)
      return std::nullopt;
    T t = std::move(buf_[head & (cap_ - 1)]);
    head_.store(head + 1, std::memory_order_release);
    return t;
  }

private:
  size_t cap_;
  std::vector<T> buf_;
  std::atomic<size_t> head_{0}, tail_{0};
};
```

## 5. `Arena` — bump allocator [P1]

**Lands in:** `src/fp/arena.hpp` — new header (add to `all.hpp`, mirror to
`include/forgefp/fp/arena.hpp`)

```
class Arena {
  explicit Arena(size_t block_size = 64 * 1024);
  template <class T> T* alloc(size_t n = 1);        // aligned, uninitialized
  template <class T, class... Ts> T* make(Ts&&...); // construct in place
  void reset();                                      // frees everything at once
  size_t used() const;
};
template <class F> auto with_arena(size_t block_size, F f);  // scoped: reset + destroy on exit
```

**Why:** Realtime audio/video threads must never call `malloc` — an arena
that is `reset()` per block/frame is the standard RT-safe allocation path.
The same pattern serves every target domain: per-frame scratch (3D scenes,
UI widgets/events), per-block buffers (audio), per-batch intermediates (ML),
and per-worker arenas (`thread_local`) so `ThreadPool`/`par_map` workers
never contend on the global allocator. FP pipelines (`map` → `filter` →
`fold`) are exactly the short-lived-churn workload arenas eliminate. P1:
sibling of `RingBuffer` (#4) — together they are the realtime-safe
infrastructure story.

**Implementation** — minimal bump allocator over a growing buffer; base is
`max_align_t`-aligned, offset alignment handles the rest

```cpp
class Arena {
public:
  explicit Arena(size_t block = 64 * 1024) : block_(block) {
    buf_.reserve(block);
  }

  template <class T> T *alloc(size_t n = 1) {
    constexpr size_t align = alignof(T);
    size_t off = (offset_ + align - 1) & ~(align - 1);
    size_t bytes = n * sizeof(T);
    if (off + bytes > buf_.size())
      buf_.resize(buf_.size() + std::max(block_, bytes));
    T *p = reinterpret_cast<T *>(buf_.data() + off);
    offset_ = off + bytes;
    return p;
  }

  template <class T, class... Ts> T *make(Ts &&...ts) {
    T *p = alloc<T>();
    new (p) T(std::forward<Ts>(ts)...);
    return p;
  }

  void reset() { offset_ = 0; }   // memory stays owned; next alloc reuses it
  size_t used() const { return offset_; }

private:
  size_t block_;
  size_t offset_ = 0;
  std::vector<char> buf_;
};
```

Usage: `Arena frame; auto *verts = frame.alloc<Vertex>(n);` ... `frame.reset();`
per frame. The pmr bridge for `std::pmr::vector` consumers is
`std::pmr::monotonic_buffer_resource` over the same buffer.

**FP contract** — the arena is a *scoped resource*, never shared state. It
must live inside one scope and never escape it: allocate intermediates,
reset on scope exit, return *values* (copied out before `reset()`), never
arena pointers. Under that discipline the enclosing function stays pure
(same input → same output), which is what keeps the arena composable with
`map`/`pipe`/`par_map`. `with_arena` enforces the scope:

```cpp
template <class F>
auto with_arena(size_t block, F f) {
  Arena a(block);
  return f(a);   // reset + destroy happen on scope exit
}

// usage — still a pure expression:
auto scaled = with_arena(1 << 20, [](Arena &a) {
  Vertex *v = a.alloc<Vertex>(n);      // intermediates from the arena
  return fp::map(verts, shade);        // value out, arena dies with the scope
});
```

## 6. `lift` family — applicative "combine two successes" [P1]

**Lands in:** the ADT headers — `src/fp/maybe.hpp`, `src/fp/result.hpp`,
`src/fp/either.hpp`, `src/fp/validation.hpp` (one overload per module, per
design decision 5)

```
template <class A, class B, class F>
auto lift2(F, std::optional<A> const&, std::optional<B> const&)
    -> std::optional<std::invoke_result_t<F, A, B>>;
// same shape for Result, Either, Validation; lift3 likewise
```

**Why:** "Do this when *both* inputs succeeded" is glue in every domain —
combine features (ML), combine UI inputs, mix signals (audio), intersect
lookups (3D registries). `apply`/`combine2` exist piecemeal in
maybe/validation; a uniform `lift2`/`lift3` family gives all four ADTs the
same idiom. P1: the most-used applicative operation.

**Implementation** — one overload per module header; `std::optional` shown,
`Result` mirrors it with `is_ok()`

```cpp
template <class A, class B, class F>
auto lift2(F f, std::optional<A> const &a, std::optional<B> const &b)
    -> std::optional<std::invoke_result_t<F, A, B>> {
  if (a && b)
    return f(*a, *b);
  return std::nullopt;
}
```

## 7. `operator>>=` — bind sugar [P1]

**Lands in:** the ADT headers — `src/fp/maybe.hpp`, `src/fp/result.hpp`,
`src/fp/either.hpp`, `src/fp/validation.hpp` (delegates to each module's
existing `and_then`)

```
template <class T, class F>
auto operator>>=(std::optional<T> const&, F);   // = and_then
template <class T, class F>
auto operator>>=(Result<T> const&, F);
```

**Why:** The pipe (`into(x) | f`) covers value pipelines; monadic bind is
the error-aware chain: `parse(input) >>= validate >>= transform`. It is
do-notation-lite, it is *already implemented* as `and_then` in each module,
and the operator is a two-line addition per ADT. P1: ergonomics, zero new
logic.

**Implementation** — delegate to the existing `and_then`

```cpp
template <class T, class F>
auto operator>>=(std::optional<T> const &o, F f) {
  return fp::and_then(o, std::move(f));
}
```

## 8. Interop — unwrap / std::expected bridge [P1]

**Lands in:** `src/fp/result.hpp` — existing header (all functions are
`Result`-shaped)

```
template <class T> T unwrap(Result<T>);                       // throws on error
template <class T> T expect(Result<T>, std::string msg);      // custom message
template <class T> std::expected<T, std::string> to_expected(Result<T> const&);
template <class T> Result<T> from_expected(std::expected<T, std::string> const&);
```

**Why:** Library code wants `Result`; boundary code (main, callbacks, C
APIs) wants values or `std::expected` (C++23). `unwrap`/`expect` are the
sanctioned escape hatches; `to_expected`/`from_expected` bridge the std
world. Without these, every boundary is bespoke `.is_ok()` branching. P1:
interop is what makes the library usable inside a larger codebase.

**Implementation** — `unwrap`/`expect` throw `std::runtime_error` with the
stored message; the `std::expected` bridge is mechanical

```cpp
template <class T> T unwrap(Result<T> r) {
  if (!r.is_ok())
    throw std::runtime_error("unwrap on error: " + r.error());
  return r.value();
}

template <class T>
std::expected<T, std::string> to_expected(Result<T> const &r) {
  return r.is_ok() ? std::expected<T, std::string>(r.value())
                   : std::unexpected(r.error());
}
```

## 9. RNG — seeded `shuffle` / `sample` [P2]

**Lands in:** `src/fp/vec.hpp` — existing header (vector combinators)

```
template <class T> std::vector<T> shuffle(std::vector<T>, uint64_t seed);
template <class T> std::vector<T> sample(std::vector<T> const&, size_t n, uint64_t seed);
```

**Why:** Seeded randomness is needed by training loops (shuffle epochs),
audio (jitter), 3D (particle init), UI (A/B tests) — always the same shape:
deterministic-given-seed, pure, over a vector. `std::shuffle` mutates the
caller's vector and threads an RNG through; these wrap it. P2: used
everywhere but low complexity.

**Implementation** — `std::mt19937_64` from the seed; `shuffle` copies then
`std::shuffle`

```cpp
template <class T>
std::vector<T> shuffle(std::vector<T> v, uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::shuffle(v.begin(), v.end(), rng);
  return v;
}

template <class T>
std::vector<T> sample(std::vector<T> const &v, size_t n, uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<size_t> dist(0, v.size() - 1);
  std::vector<T> out;
  out.reserve(n);
  for (size_t i = 0; i < n && !v.empty(); ++i)
    out.push_back(v[dist(rng)]);
  return out;
}
```

## 10. `stats` — mean / median / stddev / percentile [P2]

**Lands in:** `src/fp/vec.hpp` — existing header (numeric vector functions;
sibling of `scan`/`fold_left`)

```
double mean(std::vector<double> const&);
double median(std::vector<double> const&);
double stddev(std::vector<double> const&);
double percentile(std::vector<double> const&, double p);
```

**Why:** Metrics in ML, meters in audio, mesh stats in 3D, perf in UI — all
reduce to the same four functions over a numeric vector; `mean` is a
`fold`/`size` one-liner. They exist to stop users re-deriving them. P2:
trivial individually, collectively the "obviously missing" numeric surface.

**Implementation** — `mean` via `fold_left`; `median`/`percentile` sort a copy

```cpp
inline double mean(std::vector<double> const &v) {
  return v.empty() ? 0.0
                   : fp::fold_left(v, 0.0, [](double a, double x) { return a + x; }) /
                         static_cast<double>(v.size());
}

inline double median(std::vector<double> v) {
  if (v.empty())
    return 0.0;
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}
```

## 11. `Lazy<T>` — call-by-need thunk [P2]

**Lands in:** `src/fp/memoize.hpp` — existing header (value-level sibling of
function-level `memoize`; the laziness story lives here)

```
template <class T> class Lazy {   // computes once on first get()
  explicit Lazy(std::function<T()> thunk);
  T const& get() const;
};
```

**Why:** DAG-shaped builds — 3D scenes, audio graphs, UI widget trees — want
nodes whose cost is paid once, only if demanded, possibly from multiple
readers. `memoize` covers function-level caching; `Lazy` is the value-level
wrapper (std has no portable one). P2: rounds out the laziness story started
by `memoize`.

**Implementation** — `std::once_flag` + stored thunk

```cpp
template <class T> class Lazy {
public:
  explicit Lazy(std::function<T()> thunk) : thunk_(std::move(thunk)) {}
  T const &get() const {
    std::call_once(flag_, [this] { value_ = thunk_(); });
    return *value_;
  }

private:
  mutable std::once_flag flag_;
  mutable std::optional<T> value_;
  std::function<T()> thunk_;
};
```

## 12. `interleave` / `deinterleave` — SoA ↔ AoS [P1]

**Lands in:** `src/fp/vec.hpp` — existing header

```
template <class T> std::vector<T> interleave(std::vector<T> const&, std::vector<T> const&);
template <class T> std::pair<std::vector<T>, std::vector<T>> deinterleave(std::vector<T> const&);
```

**Why:** The most common memory-layout transform in audio/3D — interleaved
stereo ↔ planar channels, interleaved `xyz` vertex arrays ↔ parallel arrays.
Doing it by hand in every hot loop is error-prone and cache-hostile; a
single generic transform (with SIMD-friendly contiguity) is a daily
operation in an audio workstation.

**Implementation**

```cpp
template <class T>
std::vector<T> interleave(std::vector<T> const &a, std::vector<T> const &b) {
  std::vector<T> out;
  out.reserve(a.size() + b.size());
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    out.push_back(a[i]);
    out.push_back(b[i]);
  }
  out.insert(out.end(), a.begin() + n, a.end());
  out.insert(out.end(), b.begin() + n, b.end());
  return out;
}

template <class T>
std::pair<std::vector<T>, std::vector<T>> deinterleave(std::vector<T> const &v) {
  std::vector<T> a, b;
  a.reserve(v.size() / 2 + 1);
  b.reserve(v.size() / 2);
  for (size_t i = 0; i + 1 < v.size(); i += 2) {
    a.push_back(v[i]);
    b.push_back(v[i + 1]);
  }
  if (v.size() % 2)
    a.push_back(v.back());
  return {std::move(a), std::move(b)};
}
```

## 13. Buffer-reuse combinators — `map_into` / `append` [P1]

**Lands in:** `src/fp/vec.hpp` — existing header

```
template <class T, class F> void map_into(std::vector<T>& out, std::vector<T> const&, F);
template <class T, class F> void append_into(std::vector<T>& out, std::vector<T> const&, F);
```

**Why:** Every `vec::map`/`filter` allocates a fresh vector. Hot loops and
realtime threads (audio render callbacks, frame loops) want to reuse a
preallocated buffer instead — the single biggest FP-pipeline performance gap.
The allocating versions stay the default (they're the pure, ergonomic
surface); these out-param variants are the documented escape hatch.

**Implementation** — `out` is cleared then filled; capacity is preserved

```cpp
template <class T, class F>
void map_into(std::vector<T> &out, std::vector<T> const &v, F f) {
  out.clear();
  if (out.capacity() < v.size())
    out.reserve(v.size());
  for (auto const &x : v)
    out.push_back(f(x));
}
```

## 14. `small_vector` — stack-buffer-optimized vector [P1]

**Lands in:** `src/fp/vec.hpp` — existing header

```
template <class T, size_t N> class small_vector {   // inline storage, spills to heap
  // std::vector-compatible: push_back, begin/end, size, operator[], data
};
```

**Why:** FP pipelines produce many *tiny* results (a filtered handful, a
`head`, a `take 3`), each currently a heap allocation. SBO keeps those on
the stack; only overflow spills. It is the workhorse of allocation-free
short pipelines.

**Implementation** — inline `alignas(T)` buffer + heap overflow; full
std::vector parity is the real cost, so this is the largest entry here

```cpp
template <class T, size_t N> class small_vector {
public:
  void push_back(T const &x) {
    if (size_ < N) {
      new (buf_ + size_) T(x);
    } else {
      if (!heap_)
        spill();
      heap_->push_back(x);
    }
    ++size_;
  }
  // begin/end/data/size/operator[] elided — heap_ holds the overflow tail

private:
  void spill() {
    heap_ = std::make_unique<std::vector<T>>();
    heap_->reserve(N * 2);
    heap_->assign(buf_, buf_ + size_);
  }
  alignas(alignof(T)) char buf_[N * sizeof(T)];
  size_t size_ = 0;
  std::unique_ptr<std::vector<T>> heap_;
};
```

## 15. `kahan_sum` — compensated summation [P1]

**Lands in:** `src/fp/vec.hpp` — existing header

```
template <class T> T kahan_sum(std::vector<T> const&);
```

**Why:** `fold_left(v, 0.0, +)` silently loses precision on long/wide-range
reductions — exactly what DSP meters and ML loss/gradient accumulation hit.
Kahan/Neumaier compensation costs ~4 ops per element and removes the drift.
One function, no API change.

**Implementation** — classic Kahan, non-associative and NOT parallelizable
(sequential-only; document that `par_reduce` cannot use it)

```cpp
template <class T>
T kahan_sum(std::vector<T> const &v) {
  T sum = 0, c = 0;
  for (auto x : v) {
    T y = x - c;
    T t = sum + y;
    c = (t - sum) - y;
    sum = t;
  }
  return sum;
}
```

## 16. `Pool<T>` — fixed-size object reuse [P1]

**Lands in:** `src/fp/arena.hpp` — existing header (memory primitives)

```
template <class T> class Pool {
  template <class... Ts> T* make(Ts&&...);   // pops a free slot or allocates
  void release(T* p);                         // returns p to the free list
};
```

**Why:** `Arena` resets *everything* at once; `Pool` reuses *individual*
objects — particles, events, audio-graph nodes, UI widgets that churn at
irregular rates. Complements Arena: bulk vs. granular reuse.

**Implementation** — free-list; `release` does not destroy the object (same
trivially-destructible contract as `Arena`)

```cpp
template <class T> class Pool {
public:
  template <class... Ts> T *make(Ts &&...ts) {
    T *p;
    if (free_.empty()) {
      p = arena_.template alloc<T>();
    } else {
      p = free_.back();
      free_.pop_back();
    }
    new (p) T(std::forward<Ts>(ts)...);
    return p;
  }
  void release(T *p) { free_.push_back(p); }

private:
  Arena arena_;
  std::vector<T *> free_;
};
```

## 17. `CacheAligned<T>` — false-sharing padding [P1]

**Lands in:** `src/fp/arena.hpp` — existing header (memory primitives)

```
template <class T> struct alignas(64) CacheAligned { T value; ... };
```

**Why:** Shared counters (audio meters, UI perf stats, `atomic` flags
updated from worker threads) sit in the same cache line as other data and
silently cost 10–100× via false sharing. A one-line `alignas(64)` wrapper
fixes it and makes the intent explicit.

**Implementation**

```cpp
template <class T> struct alignas(64) CacheAligned {
  T value{};
  CacheAligned() = default;
  CacheAligned(T v) : value(std::move(v)) {}
  T *operator->() { return &value; }
  T const *operator->() const { return &value; }
};
```

## 18. Branch hints + alignment/prefetch [P2]

**Lands in:** `src/fp/combinators.hpp` — existing header

```
#define FP_LIKELY(x)   __builtin_expect(!!(x), 1)
#define FP_UNLIKELY(x) __builtin_expect(!!(x), 0)
template <class T> T* assume_aligned(T*, size_t alignment = 64);
```

**Why:** Free wins for hot paths: branch hints for error/sentinel checks
(the rare path in `map`/`filter`), `__builtin_assume_aligned` for pointers
the compiler must otherwise prove aligned, and (later) `prefetch` wrappers
for streaming loops. P2 — micro, but zero cost to add.

**Implementation**

```cpp
#define FP_LIKELY(x)   __builtin_expect(!!(x), 1)
#define FP_UNLIKELY(x) __builtin_expect(!!(x), 0)

template <class T>
T *assume_aligned(T *p, size_t alignment = 64) {
  return static_cast<T *>(__builtin_assume_aligned(p, alignment));
}
```

---

## Design decisions (resolved)

1. **No domain modules, ever.** Deliberately NOT shipping `dsp.hpp`
   (windows/filters/envelopes), activation math (`relu`/`softmax`), MIDI,
   tensors, meshes, or TUI layout. Those are user-side compositions of
   `scan`/`map`/`zip_with`/`chunk`/grid/`parse`. Rationale: a domain-specific
   module pins the library to one niche and bloats every consumer's compile;
   generic primitives serve all domains, and domain aliases are a user-side
   header away.
2. **`scan` already ships** (`vec.hpp`) — `fold_left` returns the final
   state, `scan` every state. Together they cover stateful DSP (filters as
   scans), ML (cells as scans), and UI (reducers as folds). No backlog entry
   needed.
3. **Two transport layers, one naming.** `Channel` = blocking, bounded,
   multi-thread (control path). `RingBuffer` = non-blocking, lock-free, SPSC
   (realtime data path). `Stream` is the transform surface over either.
4. **Parser failures are `Result`, not exceptions** — matches the library's
   error model; `Validation` accumulates multi-error parses.
5. **`lift` overloads live in each ADT's own header** (`maybe.hpp`,
   `result.hpp`, `either.hpp`, `validation.hpp`), mirroring where `map`/
   `and_then` already live. No new `lift.hpp`.
6. **`operator>>=` delegates to existing `and_then`** — zero new logic, pure
   ergonomics; same rule as `operator|` for `pipe`.
7. **`unwrap`/`expect` throw `std::runtime_error`** with the stored message;
   they are explicitly the escape hatch. `std::expected` bridge covers the
   non-throwing interop.
8. **`stats` takes `std::vector<double>` by value where it must copy**
   (median/percentile sort), `const&` where it folds (mean/stddev).
9. **`Arena` is consumer-side; combinators keep default-allocator
   `std::vector`.** No allocator template parameter threaded through the
   API — it would infect every signature for little gain. Users route hot
   stages through the arena themselves (raw pointers, or
   `std::pmr::monotonic_buffer_resource` for `pmr::vector`).
10. **Arena destructor contract: `reset()` does not destroy.** `alloc`/
    `make` construct; nothing is destructed until the arena itself dies.
    Payloads must be trivially destructible (samples, vertices, events) —
    owning types (`std::string`) stay out of arenas unless the user tracks
    destructors. Documented once, here.
11. **Arena is FP-safe by scoping, not by design.** `with_arena` is the
    sanctioned entry point — it guarantees the two rules (arena never
    escapes its scope; values out, not pointers). Raw `Arena` usage outside
    a scope is allowed for RT hot paths but is the imperative escape hatch,
    documented as such.
12. **Perf primitives are layout/reuse, not algorithms.** The perf entries
    (#12–18) ship memory layout (`interleave`/`deinterleave`,
    `CacheAligned`), reuse (`map_into`, `Pool`, `small_vector`), and numeric
    accuracy (`kahan_sum`) — never work-stealing, FFT, or matmul. Those are
    a separate math/domain library. Allocating combinators stay the default;
    the reuse variants are the documented escape hatch, not the headline API.
13. **`kahan_sum` is sequential-only.** Compensated summation is
    order-dependent and cannot run under `par_reduce`; document this at the
    call site.
14. **GPU acceleration is deferred (v2), optional, and not Vulkan.** The
    target domains (ML, DSP, 3D, video) are data-parallel, but a GPU layer is
    a v2 concern: it only accelerates a small slice (elementwise `map`,
    `reduce`/`dot`, grid, `stats`) and would break the header-only,
    zero-dependency, synchronous contract. When built, it lands as an
    optional `gpu.hpp` behind `FP_HAS_SYCL` (mirroring `simd.hpp`'s
    `FP_HAS_SIMD` opt-in), skipped silently by `all.hpp`, exposing the same
    combinator names (`gpu::map`, `gpu::reduce`) over an explicitly
    async/device-resident API. SYCL, not Vulkan — Vulkan is a graphics API;
    compute-first FP wants lambda kernels over a cross-vendor single-source
    model. Free groundwork now: keep data contiguous/SoA (already true) so a
    future host↔device memcpy path is trivial.
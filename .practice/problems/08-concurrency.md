# 08 — Concurrency

**Headers:** `<fp/concurrent.hpp>`, `<fp/stream.hpp>`.

Compile with `-pthread`. These exercise the parallel combinators, the thread
pool, the lock-free SPSC ring, actors, and async helpers.

## What this module is about

Concurrency has two halves here: **parallelism** (split a pure computation
across threads — `par_map`/`par_reduce`) and **communication** (move data
between threads — `Channel`, `RingBuffer`, `Actor`, futures). The library keeps
them separate: parallel combinators are *pure* (same result, just faster),
while the communication primitives are the explicit, bounded places where
threads interact. `Stream` layers a declarative `map`/`filter` over either.

---

### 1. Parallel map keeps order · Easy

Double `{0..999}` with `par_map` and confirm the result stays ordered.

**Why `par_map` (and why order matters):** the pool splits the vector into
chunks, maps each on a worker, and reassembles **in order** — so a parallel map
is a drop-in replacement for a sequential one, with the same contract. Using a
`ThreadPool` (vs the one-shot `std::async` form) amortizes thread creation
across many calls.

```cpp
fp::ThreadPool pool(4);
auto v = fp::range(0, 1000);
auto out = fp::par_map(pool, v, fp::times(2));
assert(out.size() == 1000 && out[0] == 0 && out[999] == 1998);
```

### 2. Parallel reduce · Easy

Sum `{0..999}` (499500) with `par_reduce`.

**Why `par_reduce` needs associativity:** each worker reduces its own slice, then
the partials are combined. That's only correct if `op` is associative
(`a + (b + c) == (a + b) + c`). For `+` it is; for floating-point or
non-associative ops, use a sequential fold instead. `par_reduce` makes the
trade-off explicit in the API.

```cpp
fp::ThreadPool pool(4);
int sum = fp::par_reduce(pool, fp::range(0, 1000), 0, fp::plus);
assert(sum == 499500);
```

### 3. Ring buffer producer/consumer · Medium

One thread pushes `{0..999}` into a `RingBuffer<int>(64)`; the main thread pops
and checks the sum.

**Why `RingBuffer` over `Channel`:** `Channel` uses a mutex, which a realtime
thread (audio, render) must never block on. `RingBuffer` is lock-free SPSC — one
producer, one consumer, no locks — so it's the realtime-safe data path. The
cost is the SPSC contract: exactly two threads, and you busy-spin on full/empty.

```cpp
fp::RingBuffer<int> rb(64);
std::thread prod([&] {
    for (int i = 0; i < 1000; ++i) while (!rb.push(i)) {}
});
long long sum = 0;
for (int got = 0; got < 1000; ) if (auto x = rb.try_pop()) { sum += *x; ++got; }
prod.join();
assert(sum == 499500);
```

### 4. Actor counter · Medium

An `Actor<int,int>` sums its messages; send 100 ones, then read `snapshot() == 100`.

**Why `Actor`:** a stateful worker with a mailbox — you send messages and it
serializes them through a handler `State(State, Msg)`, so the state is never
touched by two threads at once. It's the pattern for a background service whose
state you mutate by message, not by shared memory. `snapshot()` reads the
current state; `Ask` returns the post-message state as a future.

```cpp
fp::Actor<int, int> counter(0, [](int s, int m){ return s + m; });
for (int i = 0; i < 100; ++i) counter.Send(1);
while (counter.snapshot() < 100) std::this_thread::yield();
assert(counter.snapshot() == 100);
```

### 5. Race: first result wins · Medium

Two futures — one slow (`ok(1)` after 50ms), one instant (`ok(2)`). `race`
returns the faster one.

**Why `race`:** when any of several async computations will do (fetch from a
mirror, first responder), you want the first completion, not a specific one.
`race` fans out the futures and resolves when the first sets the shared result;
the losers' attempts to set it are discarded.

```cpp
using namespace std::chrono_literals;
std::vector<fp::AsyncResult<int>> futs;
futs.push_back(std::async(std::launch::async, []{ std::this_thread::sleep_for(50ms); return fp::ok(1); }));
futs.push_back(std::async(std::launch::async, []{ return fp::ok(2); }));
auto r = fp::race(std::move(futs)).get();
assert(r.value() == 2);
```

### 6. Stream a counter · Medium

Build a `Stream<int>` counting 0..4, `map` (×2), `filter` (>2), `subscribe` and
sum (18).

**Why `Stream`:** it's the *transform surface* over any source of items — pull
(a `std::function<optional<T>()>`) or push (a `Channel`). `map`/`filter` return
a new lazy `Stream`; `subscribe` runs the pipeline. One abstraction for "a
sequence of values that arrives over time", whether it's synchronous or
threaded.

```cpp
fp::Stream<int> s([i = 0]() mutable -> std::optional<int> {
    return i < 5 ? std::optional<int>(i++) : std::nullopt;
});
long long total = 0;
s.map(fp::times(2)).filter(fp::gt(2)).subscribe([&](int x){ total += x; });
assert(total == 18);
```

---

## Solutions

<details>
<summary>1. Parallel map keeps order</summary>

```cpp
fp::ThreadPool pool(4);
auto out = fp::par_map(pool, v, fp::times(2));
```
</details>

<details>
<summary>2. Parallel reduce</summary>

```cpp
int sum = fp::par_reduce(pool, v, 0, fp::plus);
```
</details>

<details>
<summary>3. Ring buffer producer/consumer</summary>

```cpp
fp::RingBuffer<int> rb(64);
std::thread prod([&] { for (int i = 0; i < 1000; ++i) while (!rb.push(i)) {} });
long long sum = 0;
for (int got = 0; got < 1000; ) if (auto x = rb.try_pop()) { sum += *x; ++got; }
prod.join();
```
</details>

<details>
<summary>4. Actor counter</summary>

```cpp
fp::Actor<int, int> counter(0, [](int s, int m){ return s + m; });
for (int i = 0; i < 100; ++i) counter.Send(1);
while (counter.snapshot() < 100) std::this_thread::yield();
```
</details>

<details>
<summary>5. Race</summary>

```cpp
auto r = fp::race(std::move(futs)).get();
```
</details>

<details>
<summary>6. Stream a counter</summary>

```cpp
fp::Stream<int> s([i = 0]() mutable -> std::optional<int> {
    return i < 5 ? std::optional<int>(i++) : std::nullopt;
});
long long total = 0;
s.map(fp::times(2)).filter(fp::gt(2)).subscribe([&](int x){ total += x; });
```
</details>

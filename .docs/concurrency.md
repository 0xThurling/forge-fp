# Concurrency — `concurrent.hpp` and `stream.hpp`

`concurrent.hpp` provides parallel combinators, a thread pool, channels, a
lock-free SPSC ring buffer, actors, and async combinators. `stream.hpp` adds a
pull/push `Stream<T>` transform surface. Both are header-only and dependency-free.

```cpp
#include <fp/concurrent.hpp>
#include <fp/stream.hpp>   // Stream
```

## The one distinction that organizes this module

Concurrency here is split into two concerns, deliberately kept separate:

1. **Parallelism** — *split a pure computation* across threads
   (`par_map`, `par_reduce`, `par_for_each`). These are **pure**: same result,
   just faster. They need no locks because each worker owns its slice of the
   output.

2. **Communication** — *move data between threads* (`Channel`, `RingBuffer`,
   `Actor`, futures, `Stream`). These are the explicit, bounded places where
   threads interact, and they're the only places with synchronization.

The rule: parallelize pure work with the parallel combinators; share state only
through the communication primitives. Don't thread a shared `vector` through
`par_for_each` and write into it from all workers — that's a data race the
library won't stop you from writing, but the primitives are designed so you
don't need to.

## Parallel combinators

Two families: standalone (`std::async`-based, take an optional thread count) and
`ThreadPool`-based (reuse one pool). Both chunk the input, so they scale instead
of spawning a task per element.

```cpp
#include <fp/concurrent.hpp>
#include <fp/vec.hpp>

std::vector<int> v = fp::range(0, 1'000'000);
auto work = [](int x) { return x * 2; };

// standalone (spawns std::async tasks, chunks by `threads`)
auto a = fp::par_map(v, work, 8);              // vector<int>
fp::par_for_each(v, [](int x) { /* side effect */ }, 8);

// standalone: creates its workers on every call (~tens of microseconds),
// so use it for one-shot coarse work, not inside a loop
// pool-based (reuse one pool, cheaper for repeated calls)
fp::ThreadPool pool(8);
auto b = fp::par_map(pool, v, work);           // vector<int>
fp::par_for_each(pool, v, [](int x) { /* side effect */ });
int  c = fp::par_reduce(pool, v, 0, [](int x, int y) { return x + y; });
```

`par_reduce` requires an associative `op` (the reduction is reordered across
workers). `par_map`/`par_for_each` preserve element order in the result.

The pool-based helpers pull work in fine chunks from an atomic counter, and the
**calling thread is one of the workers**. A worker that is slow to wake costs
nothing when the caller or another worker already took that chunk, and skewed
work balances without tuning. They also avoid a future per chunk (a latch
instead), which cut `par_reduce` by ~20% and `par_map` by ~15% on 1M elements.
For the same reason, prefer the pool overloads in a loop: the standalone ones
still create their workers per call (documented, ~tens of microseconds).

Three more pool-based primitives cover the shapes those three don't:

```cpp
// f(i) for i in [begin, end) — the tiling primitive for index work
fp::par_for(pool, 0, rows, [&](std::size_t i) {
  for (std::size_t j = 0; j < cols; ++j)
    out[i][j] = a[i][j] * 2;
});

// f(i, v[i]) — index + element
fp::par_for_each_index(pool, v, [&](std::size_t i, int x) {
  doubled[i] = x * 2;
});

// f(src[i]) -> dst[i], writing into a caller-owned buffer (no result vector)
fp::par_map_to(pool, src, dst, activate);
```

`par_for` is how you parallelize a loop that isn't a `map` over one vector —
row-wise matrix work, tiled kernels, per-cell grid passes. `par_map_to` is
`par_map` without the allocation: reuse `dst` across iterations and the whole
pipeline stays allocation-free. As with the other parallel combinators, the
callback must be safe to run concurrently on disjoint indices; the library
guarantees each index is visited exactly once.

```cpp
fp::ThreadPool pool;                              // hardware_concurrency threads
auto fut = pool.enqueue([](int a, int b) { return a + b; }, 1, 2);  // future<int>
int sum = fut.get();                              // 3
```

## `Channel<T>` — blocking bounded/unbounded queue

```cpp
fp::Channel<int> ch(10);        // bounded to 10; 0 = unbounded
ch.send(42);                    // blocks if full; throws on a closed channel
int x = ch.recv();              // blocks if empty; throws on a closed+drained one
bool ok = ch.try_send(7);       // non-blocking: false when full or closed
auto y = ch.try_recv();         // optional<int>, non-blocking
ch.close();                     // wake waiters; recv throws after drain
```

`Channel` is the multi-thread control path (mutex + condvar). Use it for
general message passing between any number of threads. For a realtime
producer/consumer that must not block on a mutex, use `RingBuffer` instead.

`RingBuffer` costs ~14ns per message two-threaded (1.5ns single-threaded): the
two index words have to cross between the cores on every operation, which is
the price of the design. Padding the indices onto separate cache lines was
measured and made no difference, so they stay adjacent.

## `RingBuffer<T>` — lock-free SPSC

Single producer, single consumer, non-blocking, no locks:

```cpp
fp::RingBuffer<int> rb(1024);           // capacity rounded up to a power of two
bool ok = rb.push(1);                   // false when full
auto z  = rb.try_pop();                 // optional<int>, nullopt when empty
size_t n = rb.size();
```

The SPSC contract is the point: exactly **two** threads (one push, one pop),
and you busy-spin on full/empty. In exchange you get *no mutex*, so a render
or audio thread never blocks. `Channel` = control path; `RingBuffer` = data path.

## `Actor<Msg, State>` — mailbox + handler

```cpp
fp::Actor<int, long long> counter(
    0,                                     // initial state
    [](long long s, int m) { return s + m; });  // handler: State(State, Msg)

counter.send(1);                          // fire-and-forget
auto fut = counter.ask(10);               // request/response: future<State>
long long total = counter.snapshot();     // read current state
// destructor closes the mailbox and joins the worker thread
```

The handler is a *pure* function `State(State, Msg)`; the actor serializes
messages through it, so the state is only ever touched by one thread at a time.
You mutate state by sending messages, not by sharing memory.

## Async combinators (`std::future<Result<T>>`)

`AsyncResult<T>` is `std::future<Result<T>>` — an error-carrying future.

```cpp
using namespace std::chrono_literals;

fp::AsyncResult<int> fut = std::async(std::launch::async, [] { return fp::ok(21); });

fp::AsyncResult<int> doubled = fp::async_map(fut, [](int x) { return x * 2; }); // ok(42)

std::vector<fp::AsyncResult<int>> futs = /* ... */;
fp::AsyncResult<std::vector<int>> all = fp::async_sequence(std::move(futs));

// race: first result wins
auto first = fp::race<int>(std::move(futs)).get();

// timeout: value, or err("timeout") after the deadline
auto r = fp::timeout(fut, 100ms).get();

// retry: re-run `make` until it succeeds or attempts run out
auto r2 = fp::retry(
    [&] { return std::async(std::launch::async, [] { return fp::ok(42); }); },
    5, 10ms).get();
```

### `Async<T>` — a shareable future with `then`/`and_then`

```cpp
fp::Async<int> a(std::async(std::launch::async, [] { return 21; }));
fp::Async<int> b = a.then([](int x) { return x * 2; });   // 42
b.get();
```

Unlike `std::future`, `Async` is copyable (shared state), so one result can fan
out to many continuations.

## Cancellation — `Task<T>` and `std::stop_token`

A cancel is observed both when a continuation starts **and after the upstream
stage finishes**: cancelling a chain while an upstream task is still running
skips the continuation instead of running it on a stopped chain.

`Task<T>` is a cancellable `AsyncResult<T>`: a `shared_future<Result<T>>` plus
a shared `std::stop_source`. Cancellation is **cooperative** — `cancel()`
requests a stop, and producers/continuations check it at their boundaries; a
task already running uncooperative code is not preempted.

```cpp
#include <fp/task.hpp>

// The callable may take a leading std::stop_token to poll while running.
fp::Task<int> t = fp::async_task([](std::stop_token stop) {
    while (!stop.stop_requested()) { /* work */ }
    return fp::cancelled<int>();
});

t.cancel();          // request a stop (shared across the whole chain)
t.cancelled();       // true
t.get();             // err("cancelled") once the body observes it
```

Everything in a chain shares one stop source, and continuations are skipped
once it is stopped:

```cpp
auto sum = fp::async_task([] { return 10; })
    .then([](int x) { return x + 5; })          // skipped if cancelled
    .and_then([](int x) { return fp::async_task([x] { return x * 2; }); });
sum.get();   // ok(30)

auto recovered = fp::async_task([]() -> fp::Result<int> { return fp::err<int>("x"); })
    .recover([](std::string const&) { return 0; });
```

Producers:

| Producer | Returns |
|---|---|
| `fp::async_task(f)` / `fp::async_task(token, f)` | `Task<T>` on a fresh thread |
| `pool.enqueue(token, f, args…)` | `Task<T>`; stopped tasks are skipped |
| `fp::spawn(pool, [token,] f, args…)` | sugar for the above |
| `fp::cancel_after(ms)` | a `std::stop_source` that stops after `ms` |

Cancellable bulk operations check the token at every loop boundary and resolve
to `err("cancelled")` if it fires: `par_map`/`par_for_each`/`par_reduce` (token
overloads), `race` (cancels the losers), `timeout` (cancels the source),
`retry(token, make, attempts, delay)` (interruptible backoff; `make` receives
the token).

```cpp
using namespace std::chrono_literals;
auto src = fp::cancel_after(250ms);
auto r = fp::race({
    fp::async_task([] { return 1; }),
    fp::async_task([] { std::this_thread::sleep_for(1s); return 2; }),
}).get();                                  // ok(1), loser cancelled

auto tr = fp::timeout(fp::async_task([] { /* slow */ return 0; }), 100ms).get();
// err("timeout"), source cancelled
```

`Task` does **not** auto-cancel on destruction — call `cancel()` explicitly.
For plain (non-cancellable) futures, `Async<T>`/`AsyncResult<T>` remain as
before.

## `Stream<T>` — transform a sequence of items

Transport-agnostic: build from a pull source (`std::function<optional<T>()>`)
or a push `Channel`, then `map`/`filter`/`subscribe`.

```cpp
#include <fp/stream.hpp>

// pull source: a counter that runs dry after 5 items
fp::Stream<int> s([i = 0]() mutable -> std::optional<int> {
    return i < 5 ? std::optional<int>(i++) : std::nullopt;
});

long long total = 0;
s.map([](int x) { return x * 2; })     // Stream<int>
 .filter([](int x) { return x > 2; })
 .subscribe([&](int x) { total += x; });

// push source: drain a channel
fp::Channel<int> ch;
fp::Stream<int> from_ch(ch);           // reads via try_recv
from_ch.subscribe([&](int x) { /* ... */ });
```

`map`/`filter` return a new `Stream` (lazy — nothing runs until you
`subscribe`); `subscribe` runs the pipeline to completion. `Stream` is the
*transform surface*: one `map`/`filter` vocabulary over any item source,
synchronous or threaded.

Beyond `map`/`filter`/`subscribe`, a `Stream` also supports `collect` (a.k.a.
`to_vector`), `take(n)`, `take_while(pred)`, `scan(init, op)`,
`fold_left(init, op)`, and `concat(other)` — all lazy except the
materializing/folding ones:

```cpp
auto first_three = s.map([](int x) { return x * x; }).take(3).collect();
int total2 = s.take_while([](int x) { return x < 4; })
              .fold_left(0, std::plus<>{});
fp::Stream<int> both = from_ch.concat(s);   // this stream, then `other`
```

## Which tool for which job

| Need | Use |
|---|---|
| Parallelize a pure `map`/`for_each`/`reduce` | `ThreadPool` + `par_map`/`par_for_each`/`par_reduce` |
| Parallelize an index loop or write into an existing buffer | `par_for` / `par_for_each_index` / `par_map_to` |
| Message passing between threads | `Channel<T>` |
| Realtime, lock-free single-producer/single-consumer | `RingBuffer<T>` |
| Stateful worker with a mailbox | `Actor<Msg, State>` |
| Compose async `Result`s | `async_map` / `async_sequence` / `race` / `timeout` / `retry` |
| Cancel long-running or composed async work | `Task<T>` + `std::stop_token` (`spawn` / `async_task` / `cancel_after`) |
| Lazy transformation of a sequence (sync or async) | `Stream<T>` |

# Concurrency — `concurrent.hpp` and `stream.hpp`

`concurrent.hpp` provides parallel combinators, a thread pool, channels, a
lock-free SPSC ring buffer, actors, and async combinators. `stream.hpp` adds a
pull/push `Stream<T>` transform surface. Both are header-only and dependency-free.

```cpp
#include <fp/concurrent.hpp>
#include <fp/stream.hpp>   // Stream
```

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

// pool-based (reuse one pool, cheaper for repeated calls)
fp::ThreadPool pool(8);
auto b = fp::par_map(pool, v, work);           // vector<int>
fp::par_for_each(pool, v, [](int x) { /* side effect */ });
int  c = fp::par_reduce(pool, v, 0, [](int x, int y) { return x + y; });
```

`par_reduce` requires an associative `op` (the reduction is reordered across
workers). `par_map`/`par_for_each` preserve element order in the result.

```cpp
fp::ThreadPool pool;                              // hardware_concurrency threads
auto fut = pool.enqueue([](int a, int b) { return a + b; }, 1, 2);  // future<int>
int sum = fut.get();                              // 3
```

## `Channel<T>` — blocking bounded/unbounded queue

```cpp
fp::Channel<int> ch(10);        // bounded to 10; 0 = unbounded
ch.send(42);                    // blocks if full
int x = ch.recv();              // blocks if empty
auto y = ch.try_recv();         // optional<int>, non-blocking
ch.close();                     // wake waiters; recv throws after drain
```

`Channel` is the multi-thread control path (mutex + condvar). For a realtime
producer/consumer that must not block on a mutex, use `RingBuffer`.

## `RingBuffer<T>` — lock-free SPSC

Single producer, single consumer, non-blocking, no locks:

```cpp
fp::RingBuffer<int> rb(1024);           // capacity rounded up to a power of two
bool ok = rb.push(1);                   // false when full
auto z  = rb.try_pop();                 // optional<int>, nullopt when empty
size_t n = rb.size();
```

One thread pushes, another pops. Don't share it between more than two threads.

## `Actor<Msg, State>` — mailbox + handler

```cpp
fp::Actor<int, long long> counter(
    0,                                     // initial state
    [](long long s, int m) { return s + m; });  // handler: State(State, Msg)

counter.Send(1);                          // fire-and-forget
auto fut = counter.Ask(10);               // request/response: future<State>
long long total = counter.snapshot();     // read current state
// destructor closes the mailbox and joins the worker thread
```

`Ask` returns the *new* state after the message is applied.

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
auto r2 = fp::retry<int>(
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

`map`/`filter` return a new `Stream` (lazy); `subscribe` runs the pipeline to
completion.

## Which tool for which job

| Need | Use |
|---|---|
| Parallelize a pure `map`/`for_each`/`reduce` | `ThreadPool` + `par_map`/`par_for_each`/`par_reduce` |
| Message passing between threads | `Channel<T>` |
| Realtime, lock-free single-producer/single-consumer | `RingBuffer<T>` |
| Stateful worker with a mailbox | `Actor<Msg, State>` |
| Compose async `Result`s | `async_map` / `async_sequence` / `race` / `timeout` / `retry` |
| Lazy transformation of a sequence (sync or async) | `Stream<T>` |

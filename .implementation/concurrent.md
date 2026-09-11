# concurrent.hpp — implementation status

Current surface: `par_map` (per-call `std::async`), `actor` (fire-and-forget
only), `AsyncResult<T> = std::future<Result<T>>`, `async_map`.

## Implemented

None of the backlog has landed yet — the header still matches the original
four helpers. Everything below is still to do. (The bench harness references
these; uncomment `bench/concurrent_bench.cpp` cases as each lands.)

## Backlog

## 1. `ThreadPool` — reusable worker pool [P0]

```
class ThreadPool {
  explicit ThreadPool(size_t n = std::thread::hardware_concurrency());
  template <class F, class... Ts>
  auto enqueue(F&& f, Ts&&... ts) -> std::future<std::invoke_result_t<F, Ts...>>;
  size_t size() const;
}
```

**Why:** `par_map`'s implementation spawns *one `std::async` per chunk per
call* — thread creation cost dominates for short workloads, and callers
sending many maps in a loop recreate threads every time. A pool amortizes
thread lifetime; it's the standard architecture for every parallel map/reduce
in every language runtime. Everything below (pool-based par_map, par_reduce,
par_simd) becomes an enqueue-everything pattern on top.

**Implementation** — needs `#include <functional>`, `#include <stdexcept>`

```cpp
class ThreadPool {
public:
  explicit ThreadPool(size_t n = std::thread::hardware_concurrency())
      : stop_(false) {
    for (size_t i = 0; i < n; ++i)
      workers_.emplace_back([this] { loop(); });
  }

  template <class F, class... Ts>
  auto enqueue(F&& f, Ts&&... ts)
      -> std::future<std::invoke_result_t<F, Ts...>> {
    using R = std::invoke_result_t<F, Ts...>;
    auto task = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(f), std::forward<Ts>(ts)...));
    auto fut = task->get_future();
    {
      std::lock_guard lock(mu_);
      if (stop_)
        throw std::runtime_error("enqueue on stopped ThreadPool");
      tasks_.emplace([task] { (*task)(); });
    }
    cv_.notify_one();
    return fut;
  }

  size_t size() const { return workers_.size(); }

  ~ThreadPool() {
    {
      std::lock_guard lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_)
      w.join();
  }

private:
  void loop() {
    for (;;) {
      std::unique_lock lock(mu_);
      cv_.wait(lock, [&] { return stop_ || !tasks_.empty(); });
      if (tasks_.empty() && stop_)
        return;
      auto t = std::move(tasks_.front());
      tasks_.pop();
      lock.unlock();
      t();
    }
  }

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool stop_;
};
```

`ThreadPool` is move-immovable (threads); pass by reference or `shared_ptr`.
Keep the old `std::async`-based `par_map` as the no-lifetime-management
convenience. Uncomment the `ThreadPool` bench cases in
`bench/concurrent_bench.cpp` after this lands.

## 2. `par_map(ThreadPool&, ...)` — pool-based parallel map [P0]

```
template <class T, class F>
std::vector<std::invoke_result_t<F, T>> par_map(ThreadPool&, std::vector<T> const&, F);
```

**Why:** Same public shape as the existing `par_map` but on a reusable pool
(chunked, order-preserving, sequential fallback preserved). This is the
*fix* to the current headline function's flaw (per-call spawn + README's own
caveat "for tiny vectors keep threads small").

**Implementation** — order-preserving; one task per element (the pool
amortizes scheduling). Capture `[f, x]` BY VALUE — the loop variable is
reused each iteration

```cpp
template <class T, class F>
std::vector<std::invoke_result_t<F, T>> par_map(ThreadPool& pool,
                                                std::vector<T> const& v, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<R> out;
  out.reserve(v.size());
  std::vector<std::future<R>> futs;
  futs.reserve(v.size());
  for (auto const& x : v)
    futs.push_back(pool.enqueue([f, x] { return f(x); }));
  for (auto& fut : futs)
    out.push_back(fut.get());
  return out;
}
```

## 3. `par_for_each(ThreadPool&, v, f)` [P0]

```
void par_for_each(ThreadPool&, std::vector<T> const&, F);
// also keep a std::async-based overload for symmetry with par_map
```

**Why:** The most common parallel operation ("do X to every element, no
result needed": save files, publish events, write rows) has **no** library
support at all. Users currently wrap `par_map` with a dummy return. The pool
version joins cleanly; fire-and-forget `std::async` futures also block on
destruction.

**Implementation** — both variants; the `std::async` one mirrors the existing
`par_map` shape (empty / `threads <= 1` falls back to a sequential loop)

```cpp
template <class T, class F>
void par_for_each(ThreadPool& pool, std::vector<T> const& v, F f) {
  std::vector<std::future<void>> futs;
  futs.reserve(v.size());
  for (auto const& x : v)
    futs.push_back(pool.enqueue([f, x] { f(x); }));
  for (auto& fut : futs)
    fut.get();
}

template <class T, class F>
void par_for_each(std::vector<T> const& v, F f,
                  std::size_t threads = std::thread::hardware_concurrency()) {
  if (v.empty() || threads <= 1) {
    for (auto const& x : v)
      f(x);
    return;
  }
  std::size_t n = v.size();
  std::size_t chunk = (n + threads - 1) / threads;
  std::vector<std::future<void>> futures;
  futures.reserve((n + chunk - 1) / chunk);
  for (std::size_t start = 0; start < n; start += chunk) {
    std::size_t end = std::min(n, start + chunk);
    futures.emplace_back(std::async(std::launch::async, [&, start, end]() {
      for (std::size_t i = start; i < end; ++i)
        f(v[i]);
    }));
  }
  for (auto& fut : futures)
    fut.get();
}
```

## 4. `par_reduce` / `par_fold` [P1]

```
template <class T, class F>
T par_reduce(ThreadPool&, std::vector<T> const&, T init, F op);  // op associative
```

**Why:** Sum/product/min/max over large vectors is the canonical parallel
win — and the missing producer for `simd.md`'s `par_map_inplace` story.
Slice-parallel-reduce is ~15 lines once the pool exists. Document the
associativity requirement (it's the difference between correct and subtly
wrong results with floats).

**Implementation**

```cpp
template <class T, class F>
T par_reduce(ThreadPool& pool, std::vector<T> const& v, T init, F op) {
  size_t n = v.size();
  if (n == 0)
    return init;
  size_t threads = std::min(pool.size(), n);
  size_t slice = (n + threads - 1) / threads;
  std::vector<std::future<T>> futs;
  for (size_t s = 0; s < n; s += slice) {
    size_t e = std::min(n, s + slice);
    futs.push_back(pool.enqueue([&v, &op, s, e] {
      T acc = v[s];
      for (size_t i = s + 1; i < e; ++i)
        acc = op(acc, v[i]);
      return acc;
    }));
  }
  T acc = init;
  for (auto& fut : futs)
    acc = op(acc, fut.get());
  return acc;
}
```

Float math note: `op` must be *associative* (floats are not; results differ
from a sequential fold). Document in the README.

## 5. `Channel<T>` — bounded/unbounded message queue [P1]

```
template <class T> class Channel {
  explicit Channel(size_t capacity = 0);   // 0 = unbounded
  void send(T t);                          // blocks when bounded & full
  T recv();                                // blocks until available
  std::optional<T> try_recv();
  void close();
};
```

**Why:** Producer/consumer pipelines are the other half of actor-style
concurrency — and the actor's internal machinery (mutex + queue + cv) is
already exactly this. Without a channel, users re-implement it for every
pipeline (and get deadlocks). A public `Channel` gives the library a
coordination primitive usable from plain functions, and `actor` can be
*implemented on top of it* — one primitive, two surfaces.

**Implementation** — needs `#include <optional>`

```cpp
template <class T> class Channel {
public:
  explicit Channel(size_t capacity = 0) : capacity_(capacity), closed_(false) {}

  void send(T t) {
    std::unique_lock lock(mu_);
    if (capacity_ > 0)
      not_full_.wait(lock, [&] { return q_.size() < capacity_ || closed_; });
    if (closed_)
      throw std::runtime_error("send on closed Channel");
    q_.push(std::move(t));
    not_empty_.notify_one();
  }

  T recv() {
    std::unique_lock lock(mu_);
    not_empty_.wait(lock, [&] { return !q_.empty() || closed_; });
    if (q_.empty())
      throw std::runtime_error("recv on closed Channel");
    T t = std::move(q_.front());
    q_.pop();
    if (capacity_ > 0)
      not_full_.notify_one();
    return t;
  }

  std::optional<T> try_recv() {
    std::lock_guard lock(mu_);
    if (q_.empty())
      return std::nullopt;
    T t = std::move(q_.front());
    q_.pop();
    if (capacity_ > 0)
      not_full_.notify_one();
    return t;
  }

  void close() {
    {
      std::lock_guard lock(mu_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

private:
  size_t capacity_;
  bool closed_;
  std::queue<T> q_;
  std::mutex mu_;
  std::condition_variable not_empty_, not_full_;
};
```

`send`/`recv` throw on a closed channel — or return a sentinel; the throw
contract is the simplest to document.

## 6. `actor` ask/reply — two-way messaging [P1]

```
std::future<State> Ask(Msg m);   // returns the post-handler state
```

**Why:** The current `actor` is send-only: callers learn nothing about the
result of processing (state changes are unobservable except by polling
`snapshot()`). Classic "ask" pattern (Erlang's `!` + `receive`, Java
`CompletableFuture` replies) returns a future for the handler's result. With
it, actors become aggregators and request/response services, not just
counters. Keep `Send` as the fire-and-forget alias.

**Implementation** — the queue item carries an optional promise; `Send`
stays fire-and-forget, `Ask` gets the answer. This *replaces* the current
`actor` class body — keep `Send`'s name and behavior identical so existing
code compiles unchanged

```cpp
template <class Msg, class State> class actor {
  struct Item {
    Msg m;
    std::optional<std::promise<State>> reply;
  };

public:
  using Handler = std::function<State(State, Msg)>;

  actor(State initial, Handler h)
      : state_(std::move(initial)), handler_(std::move(h)), running_(true),
        thr_([this] { loop(); }) {}

  void Send(Msg m) {
    std::lock_guard lock(mu_);
    queue_.push(Item{std::move(m), std::nullopt});
    cv_.notify_one();
  }

  std::future<State> Ask(Msg m) {
    std::promise<State> p;
    auto fut = p.get_future();
    {
      std::lock_guard lock(mu_);
      queue_.push(Item{std::move(m), std::move(p)});
    }
    cv_.notify_one();
    return fut;
  }

  ~actor() {
    {
      std::lock_guard lock(mu_);
      running_ = false;
    }
    cv_.notify_all();
    if (thr_.joinable())
      thr_.join();
  }

  State snapshot() const {
    std::lock_guard lock(mu_);
    return state_;
  }

private:
  void loop() {
    std::unique_lock lock(mu_);
    while (running_) {
      cv_.wait(lock, [&] { return !queue_.empty() || !running_; });
      while (!queue_.empty()) {
        auto item = std::move(queue_.front());
        queue_.pop();
        State new_state = handler_(state_, item.m);
        if (item.reply)
          item.reply->set_value(new_state);
        state_ = std::move(new_state);
      }
    }
  }

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::queue<Item> queue_;
  State state_;
  Handler handler_;
  bool running_;
  std::thread thr_;
};
```

## 7. `async_sequence` [P1]

```
AsyncResult<std::vector<T>> async_sequence(std::vector<AsyncResult<T>>)
```

**Why:** Gather N asynchronous operations into one `Result<vector>` (parallel
API calls, batched lookups) — the async counterpart of `result::sequence`,
which already exists synchronously. Without a *join* combinator, "fan-out /
fan-in" workloads are impossible in one library call.

**Implementation**

```cpp
template <class T>
AsyncResult<std::vector<T>> async_sequence(std::vector<AsyncResult<T>> futs) {
  return std::async(std::launch::async, [futs = std::move(futs)]() {
    std::vector<T> out;
    out.reserve(futs.size());
    for (auto& fut : futs) {
      auto r = fut.get();
      if (!r.is_ok())
        return err<std::vector<T>>(r.error());
      out.push_back(r.value());
    }
    return ok(std::move(out));
  });
}
```

## 8. `Async<T>` wrapper with `then`/`and_then` — compositional futures [P1]

```
template <class T> class Async {   // wraps std::future<T> + continuation support
  template <class F> auto then(F) -> Async<std::invoke_result_t<F, T>>;
  template <class F> auto and_then(F) -> Async<...>;   // monadic: F returns Async
};
```

**Why:** Raw `std::future` has no `then`; chaining currently means
`async_map` one step at a time, each step coupling to `Result`. A small
wrapper with continuations makes async pipelines *declarative*
(`fetch(user) | then(parse) | and_then(load_profile)`) and — key point —
removes the per-step `std::async` spawn of `async_map`. This is the single
highest-value concurrency design upgrade; P1 because it reshapes the module's
idiom, so do it deliberately (see design questions).

**Implementation** — minimal version: a shared future + `then` (multiple
consumers); keep `AsyncResult<T>` working as a plain `std::future` alias
alongside

```cpp
template <class T> class Async {
public:
  Async(std::future<T> fut)
      : shared_(std::make_shared<std::future<T>>(std::move(fut))) {}

  T get() const { return shared_->get(); }

  template <class F>
  auto then(F f) const -> Async<std::invoke_result_t<F, T>> {
    using R = std::invoke_result_t<F, T>;
    return Async<R>(std::async(std::launch::async, [shared = shared_, f = std::move(f)] {
      return f(shared->get());
    }));
  }

  template <class F>
  auto and_then(F f) const
      -> Async<typename std::invoke_result_t<F, T>::value_type> {
    using R = typename std::invoke_result_t<F, T>::value_type;
    return Async<R>(std::async(std::launch::async, [shared = shared_, f = std::move(f)] {
      return f(shared->get()).get();
    }));
  }

private:
  std::shared_ptr<std::future<T>> shared_;
};
```

The `shared_ptr` makes a future consumable by multiple continuations. Decide
the exception policy (continuation on `get()` throw → propagate, vs.
`Result`-carrying) before finalizing.

## 9. `race` / `first_of` [P2]

```
AsyncResult<T> race(std::vector<AsyncResult<T>>)   // first success/result wins
```

**Why:** Timeout-or-fallback patterns ("try primary, fall back to cache")
need "first result wins". Implementable on shared promise + atomic.
P2 — `Async<T>` (8) is the natural carrier; land it after.

**Implementation** — one atomic + one promise; register `get_future` BEFORE launching

```cpp
template <class T>
AsyncResult<T> race(std::vector<AsyncResult<T>> futs) {
  auto shared = std::make_shared<std::promise<Result<T>>>();
  std::future<Result<T>> result = shared->get_future();
  auto first_done = std::make_shared<std::atomic<bool>>(false);
  for (auto& f : futs) {
    std::async(std::launch::async, [shared, first_done, f = std::move(f)]() mutable {
      Result<T> r;
      try {
        r = f.get();
      } catch (...) {
        return;  // a throwing future cannot win the race
      }
      if (!first_done->exchange(true)) {
        try {
          shared->set_value(std::move(r));
        } catch (std::future_error const&) {
        }
      }
    });
  }
  return result;
}
```

## 10. `timeout` / `at` [P2]

```
AsyncResult<T> timeout(AsyncResult<T>, std::chrono::milliseconds);
```

**Why:** Production callers cannot stall forever on a hung upstream; requiring
external `wait_for` loops re-implements the same logic everywhere. P2 —
depends on 8's semantics (cancellation, shared state, exception policy).

**Implementation** — the first `set_value` wins; the loser's `future_error` is swallowed

```cpp
template <class T>
AsyncResult<T> timeout(AsyncResult<T> fut, std::chrono::milliseconds ms) {
  auto shared = std::make_shared<std::promise<Result<T>>>();
  std::future<Result<T>> result = shared->get_future();
  std::async(std::launch::async, [shared, f = std::move(fut)]() mutable {
    Result<T> r;
    try {
      r = f.get();
    } catch (...) {
      return;
    }
    try {
      shared->set_value(std::move(r));
    } catch (std::future_error const&) {
    }
  });
  std::async(std::launch::async, [shared, ms]() {
    std::this_thread::sleep_for(ms);
    try {
      shared->set_value(err<T>("timeout"));
    } catch (std::future_error const&) {
    }
  });
  return result;
}
```

## 11. `retry` helper [P2]

```
template <class F> auto retry(F f, size_t attempts, std::chrono::milliseconds delay) -> AsyncResult<...>;
```

**Why:** Flaky network code (the *reason* most people reach for a futures
library) is 90% retry-with-backoff. A combinator beats bespoke loops.
P2 — sits cleanly on `then`/`and_then` once 8 exists.

**Implementation** — `f` is `() -> AsyncResult<T>`; retries only while the
result is an error (not on throws — document this)

```cpp
template <class T, class F>
AsyncResult<T> retry(F make, size_t attempts, std::chrono::milliseconds delay) {
  auto shared = std::make_shared<std::promise<Result<T>>>();
  std::future<Result<T>> result = shared->get_future();
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::async(std::launch::async, [shared, done, make, attempts, delay]() {
    for (size_t i = 0; i < attempts && !done->load(); ++i) {
      Result<T> r = make().get();
      if (r.is_ok()) {
        if (!done->exchange(true)) {
          try {
            shared->set_value(std::move(r));
          } catch (std::future_error const&) {
          }
        }
        return;
      }
      if (i + 1 < attempts)
        std::this_thread::sleep_for(delay);
    }
    if (!done->load()) {
      try {
        shared->set_value(err<T>("retry exhausted"));
      } catch (std::future_error const&) {
      }
    }
  });
  return result;
}
```

## 12. Bounded actor queue / mailbox [P2]

**Why:** Unbounded queues hide backpressure; bounded mailboxes (like
`Channel(capacity)` above) make overload observable and testable.

**Implementation** — reuse `Channel<Item>` for the mailbox and gain
backpressure for free. `close()` in the destructor makes the loop's `recv()`
throw, which is the shutdown signal; `state_` stays behind its own mutex so
`snapshot()` remains race-free (the handler runs while holding it, matching
the original actor's locking behavior)

```cpp
template <class Msg, class State> class actor {
  struct Item {
    Msg m;
    std::optional<std::promise<State>> reply;
  };

public:
  using Handler = std::function<State(State, Msg)>;

  actor(State initial, Handler h, size_t mailbox_capacity = 0)
      : mailbox_(mailbox_capacity), state_(std::move(initial)),
        handler_(std::move(h)), thr_([this] { loop(); }) {}

  void Send(Msg m) {
    mailbox_.send(Item{std::move(m), std::nullopt});
  }

  std::future<State> Ask(Msg m) {
    std::promise<State> p;
    auto fut = p.get_future();
    mailbox_.send(Item{std::move(m), std::move(p)});
    return fut;
  }

  ~actor() {
    mailbox_.close();
    if (thr_.joinable())
      thr_.join();
  }

  State snapshot() const {
    std::lock_guard lock(mu_);
    return state_;
  }

private:
  void loop() {
    for (;;) {
      Item item;
      try {
        item = mailbox_.recv();
      } catch (std::runtime_error const &) {
        return;
      }
      std::lock_guard lock(mu_);
      State new_state = handler_(state_, item.m);
      if (item.reply)
        item.reply->set_value(new_state);
      state_ = std::move(new_state);
    }
  }

  mutable std::mutex mu_;
  Channel<Item> mailbox_;
  State state_;
  Handler handler_;
  std::thread thr_;
};
```

Backpressure: with `mailbox_capacity > 0`, `Send`/`Ask` block when the
mailbox is full instead of silently growing memory. With `0` (the default)
behavior is identical to today's unbounded actor. The `running_` flag is
gone — shutdown is the channel's `close()`.

---

## Design decisions (resolved)

1. **`Async<T>` and `AsyncResult<T>` — keep both, layered.** `Async<T>`
   wraps a `std::future<T>`; `AsyncResult<T>` stays the `Result`-carrying
   surface (`Async<Result<T>>`, the existing alias, unchanged). Rationale:
   every combinator that exists or is planned (`async_map`, `async_sequence`,
   `race`, `timeout`, `retry`) inspects `Result` (`is_ok()`) — a
   separately-typed `Async<T>` would fork the API for zero gain. `then` maps
   over the value; `and_then` flattens, and since `T = Result<U>` here it
   short-circuits on error for free.
2. **Backpressure — unbounded by default, bounded opt-in.** `Channel(0)`
   = unbounded (today's semantics); `Channel(capacity)` = bounded. Ship
   both, document the trade-off in the README. Rationale: the unbounded
   default preserves the existing actor behavior (`mailbox_capacity = 0`),
   and bounded mode exists where overload must be observable.
3. **ThreadPool ownership — explicit instance only.** No global singleton.
   Callers construct the pool and pass it by reference (or `shared_ptr`);
   testable, no shutdown-order bugs, no hidden global state.
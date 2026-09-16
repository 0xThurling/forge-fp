# 08 — Concurrency

**Headers:** `<fp/concurrent.hpp>`, `<fp/stream.hpp>`.

Compile with `-pthread`. These exercise parallel combinators, the thread pool,
the SPSC ring, actors, and async helpers.

---

### 1. Parallel map keeps order · Easy

Use `fp::par_map` (thread-pool form) to double `{0..999}` and confirm the
result is `{0,2,4,…,1998}` **in order**.

```cpp
fp::ThreadPool pool(4);
auto v = fp::range(0, 1000);
auto out = fp::par_map(pool, v, fp::times(2));
assert(out.size() == 1000 && out[0] == 0 && out[999] == 1998);
```

### 2. Parallel reduce · Easy

Sum `{0..999}` (499500) with `fp::par_reduce`.

```cpp
fp::ThreadPool pool(4);
int sum = fp::par_reduce(pool, fp::range(0, 1000), 0, fp::plus);
assert(sum == 499500);
```

### 3. Ring buffer producer/consumer · Medium

One thread pushes `{0..999}` into a `RingBuffer<int>(64)`; the main thread pops
them all and checks the sum. Use `push`/`try_pop`.

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

Create an `Actor<int, int>` that sums its messages; `Send` 100 messages of 1,
then read `snapshot() == 100`.

```cpp
fp::Actor<int, int> counter(0, [](int s, int m){ return s + m; });
for (int i = 0; i < 100; ++i) counter.Send(1);
while (counter.snapshot() < 100) std::this_thread::yield();
assert(counter.snapshot() == 100);
```

### 5. Race: first result wins · Medium

Two futures, one resolves to `ok(1)` after a short delay, the other to `ok(2)`
immediately. `fp::race` should hand you the faster one (`ok(2)`).

```cpp
using namespace std::chrono_literals;
std::vector<fp::AsyncResult<int>> futs;
futs.push_back(std::async(std::launch::async, []{ std::this_thread::sleep_for(50ms); return fp::ok(1); }));
futs.push_back(std::async(std::launch::async, []{ return fp::ok(2); }));
auto r = fp::race(std::move(futs)).get();
assert(r.value() == 2);
```

### 6. Stream a counter · Medium

Build a `Stream<int>` from a pull source that counts 0..4, `map` (×2), `filter`
(>2), and `subscribe` to sum the result (`4+6+8 = 18`).

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

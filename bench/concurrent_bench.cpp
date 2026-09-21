// Benchmark the concurrency module against sequential baselines.
#include <fp/concurrent.hpp>
#include <fp/ranges.hpp>
#include <fp/stream.hpp>
#include <fp/vec.hpp>
#include <thread>

#include "bench.hpp"

int main() {
  std::printf("hardware concurrency: %u\n",
              std::thread::hardware_concurrency());

  std::vector<int> ids(1 << 20);
  for (int i = 0; i < (1 << 20); ++i)
    ids[i] = i;

  auto work = [](int x) { return static_cast<int>((x * 2654435761u) % 97); };

  // --- map family: does parallelism actually pay off? ---
  bench::measure("std::for_each (sequential)", [&] {
    long long acc = 0;
    for (auto x : ids)
      acc += work(x);
    bench::keep(&acc);  // consume the result — a discarded loop is deleted
  });
  bench::measure("fp::map (sequential)", [&] {
    auto r = fp::map(ids, work);
    bench::keep(&r[0]);
  });
  bench::measure("fp::par_map (std::async, 8)", [&] {
    auto r = fp::par_map(ids, work, 8);
    bench::keep(&r[0]);
  });

  fp::ThreadPool pool(8);
  bench::measure("fp::par_map (ThreadPool, 8)", [&] {
    auto r = fp::par_map(pool, ids, work);
    bench::keep(&r[0]);
  });

  // --- reduce family ---
  bench::measure("fp::fold_left (sequential)", [&] {
    auto r = fp::fold_left(ids, 0, [](int a, int b) { return a + b; });
    bench::keep(&r);
  });
  bench::measure("fp::par_reduce (ThreadPool, 8)", [&] {
    auto r = fp::par_reduce(pool, ids, 0, [](int a, int b) { return a + b; });
    bench::keep(&r);
  });

  // --- for_each (side-effect) ---
  bench::measure("fp::par_for_each (ThreadPool, 8)", [&] {
    std::vector<int> scratch(ids.size());
    fp::par_for_each(pool, ids,
                     [&](int x) { scratch[x] = work(x); });  // ids[i]==i: no shared slot
    bench::keep(&scratch[0]);
  });

  // --- RingBuffer: raw push/pop cost, and a real two-thread SPSC ---
  constexpr int kRing = 1 << 20;
  bench::measure("RingBuffer push+pop (1M, 1 thread)", [&] {
    fp::RingBuffer<int> rb(1024);
    long long sink = 0;
    for (int i = 0; i < kRing; ++i) {
      rb.push(i);
      if (auto v = rb.try_pop())
        sink += *v;
    }
    bench::keep(&sink);
  });
  bench::measure("RingBuffer SPSC (2 threads, 1M)", [&] {
    fp::RingBuffer<int> rb(1024);
    std::thread prod([&] {
      for (int i = 0; i < kRing; ++i)
        while (!rb.push(i)) {
        }
    });
    long long sum = 0;
    int got = 0;
    std::thread cons([&] {
      while (got < kRing) {
        if (auto v = rb.try_pop()) {
          sum += *v;
          ++got;
        }
      }
    });
    prod.join();
    cons.join();
    bench::keep(&sum);
  });

  // --- Channel: mutex+queue+condvar cost (unbounded) ---
  bench::measure("Channel send+recv (1M, 1 thread)", [&] {
    fp::Channel<int> ch;
    for (int i = 0; i < kRing; ++i)
      ch.send(i);
    long long sum = 0;
    for (int i = 0; i < kRing; ++i)
      sum += ch.recv();
    bench::keep(&sum);
  });

  // --- Stream: pull-based map/subscribe overhead ---
  bench::measure("Stream map+subscribe (1M, pull)", [&] {
    fp::Stream<int> s([i = 0]() mutable -> std::optional<int> {
      return i < kRing ? std::optional<int>(i++) : std::nullopt;
    });
    long long sum = 0;
    s.map([](int x) { return x * 2; }).subscribe([&](int x) { sum += x; });
    bench::keep(&sum);
  });

  // --- Actor: mailbox drain throughput ---
  constexpr int kMsgs = 100'000;
  std::printf("actor drain (%d messages)\n", kMsgs);
  bench::measure("Actor.send -> drained", [&] {
    fp::Actor<int, long long> counter(0,
                                      [](long long s, int m) { return s + m; });
    for (int i = 0; i < kMsgs; ++i)
      counter.send(1);
    while (counter.snapshot() < kMsgs)
      std::this_thread::yield();
    bench::keep(&counter);
  });
}

#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST(Concurrent, ChannelFifoAndClose) {
  fp::Channel<int> ch;
  ch.send(1);
  ch.send(2);
  EXPECT_EQ(ch.recv(), 1);
  EXPECT_EQ(ch.try_recv().value(), 2);
  EXPECT_FALSE(ch.try_recv().has_value());
  ch.close();
  EXPECT_FALSE(ch.try_recv().has_value());
}

TEST(Concurrent, ChannelThrowsWhenClosed) {
  fp::Channel<int> ch;
  ch.close();
  EXPECT_THROW(ch.send(1), std::runtime_error);
  EXPECT_THROW(ch.recv(), std::runtime_error);
}

TEST(Concurrent, RingBuffer) {
  fp::RingBuffer<int> rb(2);
  EXPECT_TRUE(rb.push(1));
  EXPECT_TRUE(rb.push(2));
  EXPECT_FALSE(rb.push(3));
  EXPECT_EQ(rb.size(), 2u);
  EXPECT_EQ(rb.try_pop().value(), 1);
  EXPECT_EQ(rb.try_pop().value(), 2);
  EXPECT_FALSE(rb.try_pop().has_value());
}

TEST(Concurrent, ParMapAndForEach) {
  std::vector<int> v(100);
  std::iota(v.begin(), v.end(), 1);
  auto squares = fp::par_map(v, [](int x) { return x * x; }, 4);
  ASSERT_EQ(squares.size(), v.size());
  EXPECT_EQ(squares[99], 10000);

  std::atomic<long long> total{0};
  fp::par_for_each(v, [&](int x) { total += x; }, 4);
  EXPECT_EQ(total.load(), 5050);
}

TEST(Concurrent, ParMapEmptyAndSingleThreaded) {
  std::vector<int> empty;
  EXPECT_TRUE(fp::par_map(empty, [](int x) { return x; }, 4).empty());
  EXPECT_EQ(fp::par_map(std::vector<int>{1, 2}, [](int x) { return x + 1; }, 1),
            (std::vector<int>{2, 3}));
}

TEST(Concurrent, ThreadPool) {
  fp::ThreadPool pool(4);
  EXPECT_EQ(pool.size(), 4u);
  auto f = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);
  EXPECT_EQ(f.get(), 5);

  std::vector<int> v = {1, 2, 3, 4};
  EXPECT_EQ(fp::par_map(pool, v, [](int x) { return x * 10; }),
            (std::vector<int>{10, 20, 30, 40}));
  EXPECT_EQ(fp::par_reduce(pool, v, 0, std::plus<>{}), 10);

  std::atomic<int> count{0};
  fp::par_for_each(pool, v, [&](int) { ++count; });
  EXPECT_EQ(count.load(), 4);
}

TEST(Concurrent, ActorSendAsk) {
  fp::Actor<int, int> counter(0, [](int s, int m) { return s + m; });
  for (int i = 0; i < 10; ++i)
    counter.send(1);
  EXPECT_EQ(counter.ask(5).get(), 15);
  EXPECT_EQ(counter.snapshot(), 15);
}

TEST(Concurrent, AsyncThenAndThen) {
  fp::Async<int> a(std::async(std::launch::async, [] { return 21; }));
  EXPECT_EQ(a.then([](int x) { return x * 2; }).get(), 42);

  auto chained = a.and_then([](int x) {
    return fp::Async<int>(std::async(std::launch::async, [x] { return x + 1; }));
  });
  EXPECT_EQ(chained.get(), 22);
}

TEST(Concurrent, AsyncMapAndSequence) {
  auto mapped = fp::async_map(std::async(std::launch::async, [] { return fp::ok(2); }),
                              [](int x) { return x * 3; });
  EXPECT_EQ(mapped.get().value(), 6);

  std::vector<fp::AsyncResult<int>> futs;
  futs.push_back(std::async(std::launch::async, [] { return fp::ok(1); }));
  futs.push_back(std::async(std::launch::async, [] { return fp::ok(2); }));
  auto seq = fp::async_sequence(std::move(futs)).get();
  ASSERT_TRUE(seq.is_ok());
  EXPECT_EQ(seq.value(), (std::vector<int>{1, 2}));
}

TEST(Concurrent, Race) {
  std::vector<fp::AsyncResult<int>> futs;
  futs.push_back(std::async(std::launch::async, [] { return fp::ok(1); }));
  futs.push_back(std::async(std::launch::async, [] { return fp::ok(2); }));
  auto r = fp::race(std::move(futs)).get();
  ASSERT_TRUE(r.is_ok());
  EXPECT_TRUE(r.value() == 1 || r.value() == 2);
}

TEST(Concurrent, TimeoutFastAndSlow) {
  auto fast = std::async(std::launch::async, [] { return fp::ok(7); });
  EXPECT_EQ(fp::timeout(std::move(fast), 500ms).get().value(), 7);

  auto slow = std::async(std::launch::async, [] {
    std::this_thread::sleep_for(300ms);
    return fp::ok(1);
  });
  auto tr = fp::timeout(std::move(slow), 20ms).get();
  ASSERT_FALSE(tr.is_ok());
  EXPECT_EQ(tr.error(), "timeout");
}

TEST(Concurrent, RetryEventuallySucceeds) {
  auto attempts = std::make_shared<std::atomic<int>>(0);
  auto make = [attempts]() -> fp::AsyncResult<int> {
    int n = attempts->fetch_add(1);
    return std::async(std::launch::async, [n]() -> fp::Result<int> {
      if (n < 2)
        return fp::err<int>("transient");
      return fp::ok(42);
    });
  };
  EXPECT_EQ(fp::retry(make, 5, 1ms).get().value(), 42);
  EXPECT_GE(attempts->load(), 3);
}

TEST(Concurrent, RetryExhausted) {
  auto make = []() -> fp::AsyncResult<int> {
    return std::async(std::launch::async, [] { return fp::err<int>("always"); });
  };
  auto r = fp::retry(make, 3, 1ms).get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "retry exhausted");
}

#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {
bool wait_for_flag(std::atomic<bool> const &flag,
                   std::chrono::milliseconds limit) {
  auto deadline = std::chrono::steady_clock::now() + limit;
  while (!flag.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return flag.load();
}
} // namespace

TEST(Cancellation, Helpers) {
  auto src = fp::cancel_after(1ms);
  std::this_thread::sleep_for(20ms);
  EXPECT_TRUE(src.stop_requested());

  auto c = fp::cancelled<int>();
  ASSERT_FALSE(c.is_ok());
  EXPECT_EQ(c.error(), "cancelled");

  auto co = fp::cancelled_outcome<int>();
  ASSERT_FALSE(co.is_ok());
  EXPECT_EQ(co.error().code, fp::errc::cancelled);
}

TEST(Cancellation, AsyncTaskThen) {
  auto t = fp::async_task([] { return 21; }).then([](int x) { return x * 2; });
  EXPECT_EQ(t.get().value(), 42);

  auto bad =
      fp::async_task([]() -> fp::Result<int> { return fp::err<int>("x"); })
          .then([](int v) { return v + 1; });
  auto r = bad.get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "x");
}

TEST(Cancellation, ThenSkippedWhenCancelled) {
  auto ran = std::make_shared<std::atomic<bool>>(false);
  auto t = fp::async_task([] {
             std::this_thread::sleep_for(50ms);
             return 1;
           }).then([ran](int x) {
             ran->store(true);
             return x;
           });
  t.cancel();
  auto r = t.get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "cancelled");
  EXPECT_FALSE(ran->load());
}

TEST(Cancellation, TokenAwareBodyObservesCancel) {
  auto started = std::make_shared<std::atomic<bool>>(false);
  auto observed = std::make_shared<std::atomic<bool>>(false);
  auto t = fp::async_task(
      [started, observed](std::stop_token tok) -> fp::Result<int> {
        started->store(true);
        while (!tok.stop_requested())
          std::this_thread::sleep_for(1ms);
        observed->store(true);
        return fp::cancelled<int>();
      });
  ASSERT_TRUE(wait_for_flag(*started, 1000ms));
  t.cancel();
  auto r = t.get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "cancelled");
  EXPECT_TRUE(observed->load());
}

TEST(Cancellation, AndThenChains) {
  auto t =
      fp::async_task([] { return 10; })
          .and_then([](int x) { return fp::async_task([x] { return x + 5; }); });
  EXPECT_EQ(t.get().value(), 15);
}

TEST(Cancellation, Recover) {
  auto t = fp::async_task([]() -> fp::Result<int> { return fp::err<int>("boom"); })
               .recover([](std::string const &e) { return (int)e.size(); });
  EXPECT_EQ(t.get().value(), 4);

  auto ok = fp::async_task([] { return 7; })
                .recover([](std::string const &) { return 0; });
  EXPECT_EQ(ok.get().value(), 7);
}

TEST(Cancellation, JoinAll) {
  std::vector<fp::Task<int>> tasks;
  tasks.push_back(fp::async_task([] { return 1; }));
  tasks.push_back(fp::async_task([] { return 2; }));
  tasks.push_back(
      fp::async_task([]() -> fp::Result<int> { return fp::err<int>("x"); }));
  auto rs = fp::Task<int>::join(std::move(tasks));
  ASSERT_EQ(rs.size(), 3u);
  EXPECT_EQ(rs[0].value(), 1);
  EXPECT_EQ(rs[1].value(), 2);
  EXPECT_FALSE(rs[2].is_ok());
}

TEST(Cancellation, PoolRunsTokenAwareCallables) {
  fp::ThreadPool pool(4);

  auto t =
      pool.enqueue(std::stop_token{}, [](int a, int b) { return a + b; }, 2, 3);
  EXPECT_EQ(t.get().value(), 5);

  auto sum = fp::spawn(pool, [](int x) { return x * 2; }, 21);
  EXPECT_EQ(sum.get().value(), 42);

  auto with_tok = pool.enqueue(std::stop_token{}, [](std::stop_token tok) {
    return tok.stop_possible() ? 1 : 0;
  });
  EXPECT_EQ(with_tok.get().value(), 1);
}

TEST(Cancellation, PoolSkipsStoppedTasks) {
  fp::ThreadPool pool(2);
  std::stop_source src;
  src.request_stop();
  auto t = pool.enqueue(src.get_token(), [] { return 1; });
  auto r = t.get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "cancelled");

  auto s = fp::spawn(pool, src.get_token(), [] { return 1; });
  EXPECT_FALSE(s.get().is_ok());
}

TEST(Cancellation, ParMapCancels) {
  fp::ThreadPool pool(4);
  std::stop_source src;
  src.request_stop();
  std::vector<int> v = {1, 2, 3, 4};
  auto r = fp::par_map(pool, src.get_token(), v, [](int x) { return x * 2; });
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "cancelled");
}

TEST(Cancellation, ParForEachCancels) {
  fp::ThreadPool pool(4);
  std::stop_source src;
  src.request_stop();
  std::vector<int> v = {1, 2, 3};
  auto r = fp::par_for_each(pool, src.get_token(), v, [](int) {});
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "cancelled");
}

TEST(Cancellation, ParReduce) {
  fp::ThreadPool pool(4);
  std::vector<int> v = {1, 2, 3, 4};
  EXPECT_EQ(
      fp::par_reduce(pool, std::stop_token{}, v, 0, std::plus<>{}).value(), 10);

  std::stop_source src;
  src.request_stop();
  EXPECT_FALSE(fp::par_reduce(pool, src.get_token(), v, 0, std::plus<>{}).is_ok());
}

TEST(Cancellation, RuntimeCancellationOfParMap) {
  fp::ThreadPool pool(4);
  std::stop_source src;
  std::vector<int> v(64, 1);
  auto slow = [&src](int x) {
    std::this_thread::sleep_for(2ms);
    (void)src;
    return x;
  };
  auto fut = std::async(std::launch::async, [&] {
    return fp::par_map(pool, src.get_token(), v, slow);
  });
  std::this_thread::sleep_for(5ms);
  src.request_stop();
  auto r = fut.get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "cancelled");
}

TEST(Cancellation, RaceWinnerWinsAndLoserObservesCancel) {
  auto started = std::make_shared<std::atomic<bool>>(false);
  auto observed = std::make_shared<std::atomic<bool>>(false);

  auto loser = fp::async_task([started, observed](std::stop_token tok) {
    started->store(true);
    while (!tok.stop_requested())
      std::this_thread::sleep_for(1ms);
    observed->store(true);
    return 2;
  });
  ASSERT_TRUE(wait_for_flag(*started, 1000ms));

  std::vector<fp::Task<int>> tasks;
  tasks.push_back(fp::async_task([] { return 1; }));
  tasks.push_back(std::move(loser));

  auto r = fp::race(std::move(tasks)).get();
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(r.value(), 1);
  EXPECT_TRUE(wait_for_flag(*observed, 1000ms));
}

TEST(Cancellation, RaceEmpty) {
  std::vector<fp::Task<int>> tasks;
  auto r = fp::race(std::move(tasks)).get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "race: no tasks");
}

TEST(Cancellation, TimeoutCancelsSource) {
  auto started = std::make_shared<std::atomic<bool>>(false);
  auto observed = std::make_shared<std::atomic<bool>>(false);

  auto t = fp::async_task([started, observed](std::stop_token tok) {
    started->store(true);
    while (!tok.stop_requested())
      std::this_thread::sleep_for(1ms);
    observed->store(true);
    return 1;
  });
  ASSERT_TRUE(wait_for_flag(*started, 1000ms));

  auto r = fp::timeout(t, 20ms).get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "timeout");
  EXPECT_TRUE(wait_for_flag(*observed, 1000ms));
  EXPECT_TRUE(t.cancelled());
}

TEST(Cancellation, TimeoutFastPath) {
  auto t = fp::async_task([] { return 7; });
  auto r = fp::timeout(t, 1000ms).get();
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(r.value(), 7);
}

TEST(Cancellation, RetryWithToken) {
  auto attempts = std::make_shared<std::atomic<int>>(0);
  auto make = [attempts](std::stop_token) -> fp::AsyncResult<int> {
    int n = attempts->fetch_add(1);
    return std::async(std::launch::async, [n]() -> fp::Result<int> {
      if (n < 2)
        return fp::err<int>("transient");
      return fp::ok(42);
    });
  };
  auto r = fp::retry(std::stop_token{}, make, 5, 1ms).get();
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(r.value(), 42);
  EXPECT_GE(attempts->load(), 3);
}

TEST(Cancellation, RetryStopsWhenCancelled) {
  auto t = fp::retry(std::stop_token{},
                     [](std::stop_token) -> fp::AsyncResult<int> {
                       return std::async(std::launch::async,
                                         [] { return fp::err<int>("nope"); });
                     },
                     1000, 5ms);
  t.cancel();
  auto r = t.get();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "cancelled");
}

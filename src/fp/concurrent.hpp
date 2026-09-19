#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "result.hpp"
#include "task.hpp"

namespace fp {
template <class T, class F>
auto par_map(std::vector<T> const &v, F f,
             std::size_t threads = std::thread::hardware_concurrency()) {
  using R = std::invoke_result_t<F, T>;
  if (v.empty() || threads <= 1) {
    std::vector<R> out;
    out.reserve(v.size());
    for (auto const &x : v)
      out.push_back(f(x));
    return out;
  }

  std::size_t n = v.size();
  std::size_t chunk = (n + threads - 1) / threads;
  std::vector<std::future<std::vector<R>>> futures;

  futures.reserve((n + chunk - 1) / chunk);

  for (std::size_t start = 0; start < n; start += chunk) {
    std::size_t end = std::min(n, start + chunk);
    futures.emplace_back(std::async(std::launch::async, [&, start, end]() {
      std::vector<R> out;
      out.reserve(end - start);
      for (std::size_t i = start; i < end; ++i)
        out.push_back(f(v[i]));
      return out;
    }));
  }

  std::vector<R> result;
  result.reserve(n);
  for (auto &fut : futures) {
    auto part = fut.get();
    result.insert(result.end(), part.begin(), part.end());
  }
  return result;
}

template <class T, class F>
void par_for_each(std::vector<T> const &v, F f,
                  std::size_t threads = std::thread::hardware_concurrency()) {
  if (v.empty() || threads <= 1) {
    for (auto const &x : v)
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
  for (auto &fut : futures)
    fut.get();
}

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

  size_t size() const {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    return tail - head;
  }

private:
  size_t cap_;
  std::vector<T> buf_;
  std::atomic<size_t> head_{0}, tail_{0};
};

template <class Msg, class State> class Actor {
  struct Item {
    Msg m;
    std::optional<std::promise<State>> reply;
  };

public:
  using Handler = std::function<State(State, Msg)>;

  Actor(State initial, Handler h, size_t mailbox_capacity = 0)
      : mailbox_(mailbox_capacity), state_(std::move(initial)),
        handler_(std::move(h)), thr_([this] { loop(); }) {}

  void send(Msg m) {
    mailbox_.send(Item{std::move(m), std::nullopt});
  }

  std::future<State> ask(Msg m) {
    std::promise<State> p;
    auto fut = p.get_future();
    mailbox_.send(Item{std::move(m), std::move(p)});
    return fut;
  }

  ~Actor() {
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


template <class T> using AsyncResult = std::future<Result<T>>;

template <class T, class F>
AsyncResult<std::invoke_result_t<F, T>> async_map(AsyncResult<T> fut, F f) {
  using R = std::invoke_result_t<F, T>;
  return std::async(std::launch::async,
                    [f = std::move(f), fut = std::move(fut)]() mutable {
                      auto r = fut.get();

                      if (!r.is_ok())
                        return err<R>(r.error());

                      return ok<R>(f(r.value()));
                    });
}

template <class T>
AsyncResult<std::vector<T>> async_sequence(std::vector<AsyncResult<T>> futs) {
  return std::async(std::launch::async, [futs = std::move(futs)]() mutable {
    std::vector<T> out;
    out.reserve(futs.size());
    for (auto &fut : futs) {
      auto r = fut.get();
      if (!r.is_ok())
        return err<std::vector<T>>(r.error());
      out.push_back(r.value());
    }
    return ok(std::move(out));
  });
}

class ThreadPool {
public:
  explicit ThreadPool(size_t n = std::thread::hardware_concurrency())
      : stop_(false) {
    for (size_t i = 0; i < n; ++i)
      workers_.emplace_back([this] { loop(); });
  }

  template <class F, class... Ts>
    requires(!std::is_same_v<std::decay_t<F>, std::stop_token>)
  auto enqueue(F &&f, Ts &&...ts) -> std::future<std::invoke_result_t<F, Ts...>> {
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

  // Cancellable enqueue: a task whose token is stopped before it runs never
  // executes and resolves to `err("cancelled")`. The returned Task shares its
  // cancellation with `tok`. Callables may take a leading `std::stop_token` to
  // observe cancellation while running.
  template <class F, class... Ts>
  auto enqueue(std::stop_token tok, F &&f, Ts &&...ts)
      -> Task<detail::task_value_t<detail::producer_result_t<F, Ts...>>> {
    using R = detail::producer_result_t<F, Ts...>;
    using V = detail::task_value_t<R>;

    auto src = std::make_shared<std::stop_source>();
    auto stopper = [src] { src->request_stop(); };
    std::shared_ptr<std::stop_callback<decltype(stopper)>> bridge;
    if (tok.stop_possible())
      bridge =
          std::make_shared<std::stop_callback<decltype(stopper)>>(tok, stopper);

    auto task = std::make_shared<std::packaged_task<Result<V>()>>(
        [src, bridge, f = std::forward<F>(f),
         ... ts = std::forward<Ts>(ts)]() mutable -> Result<V> {
          if (src->stop_requested())
            return cancelled<V>();
          if constexpr (std::is_void_v<R>) {
            detail::call_producer(f, src->get_token(), std::move(ts)...);
            return ok<void>();
          } else {
            return detail::as_result(
                detail::call_producer(f, src->get_token(), std::move(ts)...));
          }
        });

    auto fut = task->get_future().share();
    {
      std::lock_guard lock(mu_);
      if (stop_)
        throw std::runtime_error("enqueue on stopped ThreadPool");
      tasks_.emplace([task] { (*task)(); });
    }
    cv_.notify_one();
    return Task<V>(std::move(fut), std::move(src));
  }

  size_t size() const { return workers_.size(); }

  ~ThreadPool() {
    {
      std::lock_guard lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto &w : workers_)
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

template <class T, class F>
std::vector<std::invoke_result_t<F, T>> par_map(ThreadPool &pool,
                                                std::vector<T> const &v, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<R> out(v.size());
  size_t n = v.size();
  size_t threads = std::max<size_t>(1, pool.size());
  size_t chunk = (n + threads - 1) / threads;
  std::vector<std::future<void>> futs;
  futs.reserve((n + chunk - 1) / chunk);
  for (size_t s = 0; s < n; s += chunk) {
    size_t e = std::min(n, s + chunk);
    futs.push_back(pool.enqueue([&v, &out, f, s, e] {
      for (size_t i = s; i < e; ++i)
        out[i] = f(v[i]);
    }));
  }
  for (auto &fut : futs)
    fut.get();
  return out;
}

template <class T, class F>
void par_for_each(ThreadPool &pool, std::vector<T> const &v, F f) {
  size_t n = v.size();
  size_t threads = std::max<size_t>(1, pool.size());
  size_t chunk = (n + threads - 1) / threads;
  std::vector<std::future<void>> futs;
  futs.reserve((n + chunk - 1) / chunk);
  for (size_t s = 0; s < n; s += chunk) {
    size_t e = std::min(n, s + chunk);
    futs.push_back(pool.enqueue([&v, f, s, e] {
      for (size_t i = s; i < e; ++i)
        f(v[i]);
    }));
  }
  for (auto &fut : futs)
    fut.get();
}

template <class T, class F>
T par_reduce(ThreadPool &pool, std::vector<T> const &v, T init, F op) {
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
  for (auto &fut : futs)
    acc = op(acc, fut.get());
  return acc;
}

// --- cancellable variants -------------------------------------------------
// The token is observed at every loop boundary; if it stops, the partial work
// is discarded and the call resolves to `err("cancelled")`. Running callables
// are never preempted, only skipped.

template <class T, class F>
auto par_map(ThreadPool &pool, std::stop_token tok, std::vector<T> const &v,
             F f) -> Result<std::vector<std::invoke_result_t<F, T>>> {
  using R = std::invoke_result_t<F, T>;
  std::vector<R> out(v.size());
  size_t n = v.size();
  if (n == 0)
    return ok(std::move(out));
  size_t threads = std::max<size_t>(1, pool.size());
  size_t chunk = (n + threads - 1) / threads;
  std::vector<std::future<void>> futs;
  futs.reserve((n + chunk - 1) / chunk);
  for (size_t s = 0; s < n; s += chunk) {
    size_t e = std::min(n, s + chunk);
    futs.push_back(pool.enqueue([&v, &out, f, s, e, tok] {
      for (size_t i = s; i < e && !tok.stop_requested(); ++i)
        out[i] = f(v[i]);
    }));
  }
  for (auto &fut : futs)
    fut.get();
  if (tok.stop_requested())
    return cancelled<std::vector<R>>();
  return ok(std::move(out));
}

template <class T, class F>
Result<void> par_for_each(ThreadPool &pool, std::stop_token tok,
                          std::vector<T> const &v, F f) {
  size_t n = v.size();
  size_t threads = std::max<size_t>(1, pool.size());
  size_t chunk = (n + threads - 1) / threads;
  std::vector<std::future<void>> futs;
  futs.reserve((n + chunk - 1) / chunk);
  for (size_t s = 0; s < n; s += chunk) {
    size_t e = std::min(n, s + chunk);
    futs.push_back(pool.enqueue([&v, f, s, e, tok] {
      for (size_t i = s; i < e && !tok.stop_requested(); ++i)
        f(v[i]);
    }));
  }
  for (auto &fut : futs)
    fut.get();
  if (tok.stop_requested())
    return cancelled<void>();
  return ok<void>();
}

template <class T, class F>
Result<T> par_reduce(ThreadPool &pool, std::stop_token tok,
                     std::vector<T> const &v, T init, F op) {
  size_t n = v.size();
  if (n == 0)
    return ok(std::move(init));
  size_t threads = std::min(pool.size(), n);
  size_t slice = (n + threads - 1) / threads;
  std::vector<std::future<T>> futs;
  for (size_t s = 0; s < n; s += slice) {
    size_t e = std::min(n, s + slice);
    futs.push_back(pool.enqueue([&v, &op, s, e, tok] {
      T acc = v[s];
      for (size_t i = s + 1; i < e && !tok.stop_requested(); ++i)
        acc = op(acc, v[i]);
      return acc;
    }));
  }
  T acc = std::move(init);
  for (auto &fut : futs)
    acc = op(acc, fut.get());
  if (tok.stop_requested())
    return cancelled<T>();
  return ok(std::move(acc));
}

// Sugar over the cancellable enqueue.
template <class F, class... Ts>
auto spawn(ThreadPool &pool, std::stop_token tok, F &&f, Ts &&...ts)
    -> Task<detail::task_value_t<detail::producer_result_t<F, Ts...>>> {
  return pool.enqueue(tok, std::forward<F>(f), std::forward<Ts>(ts)...);
}

template <class F, class... Ts>
  requires(!std::is_same_v<std::decay_t<F>, std::stop_token>)
auto spawn(ThreadPool &pool, F &&f, Ts &&...ts)
    -> Task<detail::task_value_t<detail::producer_result_t<F, Ts...>>> {
  return pool.enqueue(std::stop_token{}, std::forward<F>(f),
                      std::forward<Ts>(ts)...);
}

// Cancellable std::async. The callable may take a leading `std::stop_token`.
template <class F>
auto async_task(std::stop_token tok, F f)
    -> Task<detail::task_value_t<detail::producer_result_t<F>>> {
  using R = detail::producer_result_t<F>;
  using V = detail::task_value_t<R>;

  auto src = std::make_shared<std::stop_source>();
  auto stopper = [src] { src->request_stop(); };
  std::shared_ptr<std::stop_callback<decltype(stopper)>> bridge;
  if (tok.stop_possible())
    bridge =
        std::make_shared<std::stop_callback<decltype(stopper)>>(tok, stopper);

  auto fut =
      std::async(std::launch::async,
                 [f = std::move(f), src, bridge]() mutable -> Result<V> {
                   if (src->stop_requested())
                     return cancelled<V>();
                   if constexpr (std::is_void_v<R>) {
                     detail::call_producer(f, src->get_token());
                     return ok<void>();
                   } else {
                     return detail::as_result(
                         detail::call_producer(f, src->get_token()));
                   }
                 })
          .share();
  return Task<V>(std::move(fut), std::move(src));
}

template <class F> auto async_task(F f) {
  return async_task(std::stop_token{}, std::move(f));
}

template <class T> class Async {
public:
  using value_type = T;

  // shared_future so an Async can be awaited from multiple continuations.
  Async(std::future<T> fut)
      : shared_(std::make_shared<std::shared_future<T>>(std::move(fut))) {}

  T get() const { return shared_->get(); }

  template <class F> auto then(F f) const -> Async<std::invoke_result_t<F, T>> {
    using R = std::invoke_result_t<F, T>;
    return Async<R>(
        std::async(std::launch::async, [shared = shared_, f = std::move(f)] {
          return f(shared->get());
        }));
  }

  template <class F>
  auto and_then(F f) const
      -> Async<typename std::invoke_result_t<F, T>::value_type> {
    using R = typename std::invoke_result_t<F, T>::value_type;
    return Async<R>(
        std::async(std::launch::async, [shared = shared_, f = std::move(f)] {
          return f(shared->get()).get();
        }));
  }

private:
  std::shared_ptr<std::shared_future<T>> shared_;
};

template <class T> AsyncResult<T> race(std::vector<AsyncResult<T>> futs) {
  auto shared = std::make_shared<std::promise<Result<T>>>();
  std::future<Result<T>> result = shared->get_future();
  auto first_done = std::make_shared<std::atomic<bool>>(false);
  for (auto &f : futs) {
    std::thread([shared, first_done, f = std::move(f)]() mutable {
      Result<T> r;
      try {
        r = f.get();
      } catch (...) {
        return;
      }
      if (!first_done->exchange(true)) {
        try {
          shared->set_value(std::move(r));
        } catch (std::future_error const &) {
        }
      }
    }).detach();
  }
  return result;
}

template <class T>
AsyncResult<T> timeout(AsyncResult<T> fut, std::chrono::milliseconds ms) {
  auto shared = std::make_shared<std::promise<Result<T>>>();
  std::future<Result<T>> result = shared->get_future();
  std::thread([shared, f = std::move(fut)]() mutable {
    Result<T> r;
    try {
      r = f.get();
    } catch (...) {
      return;
    }

    try {
      shared->set_value(std::move(r));
    } catch (std::future_error const &) {
    }
  }).detach();

  std::thread([shared, ms]() {
    std::this_thread::sleep_for(ms);
    try {
      shared->set_value(err<T>("timeout"));
    } catch (std::future_error const &) {
    }
  }).detach();
  return result;
}

// --- cancellable variants (Task-based) ------------------------------------

// `make()` (no arguments) returns an AsyncResult<T>; T is deduced from it.
template <class F>
auto retry(F make, size_t attempts, std::chrono::milliseconds delay) {
  using T = typename decltype(std::declval<std::invoke_result_t<F>>().get())
      ::value_type;
  auto shared = std::make_shared<std::promise<Result<T>>>();
  std::future<Result<T>> result = shared->get_future();
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread([shared, done, make, attempts, delay]() {
    for (size_t i = 0; i < attempts && !done->load(); ++i) {
      Result<T> r = make().get();
      if (r.is_ok()) {
        if (!done->exchange(true)) {
          try {
            shared->set_value(std::move(r));
          } catch (std::future_error const &) {
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
      } catch (std::future_error const &) {
      }
    }
  }).detach();
  return result;
}

// First task to finish wins; the rest are cancelled.
template <class T> Task<T> race(std::vector<Task<T>> tasks) {
  auto src = std::make_shared<std::stop_source>();
  auto shared = std::make_shared<std::vector<Task<T>>>(std::move(tasks));

  if (shared->empty()) {
    return Task<T>(
        std::async(std::launch::async, [] { return err<T>("race: no tasks"); })
            .share(),
        src);
  }

  auto promise = std::make_shared<std::promise<Result<T>>>();
  auto result = promise->get_future();
  auto done = std::make_shared<std::atomic<bool>>(false);

  auto stopper = [shared] {
    for (auto &t : *shared)
      t.cancel();
  };
  auto bridge = std::make_shared<std::stop_callback<decltype(stopper)>>(
      src->get_token(), stopper);

  for (auto &t : *shared) {
    std::thread([t, promise, done, shared, bridge]() mutable {
      Result<T> r = t.get();
      if (done->exchange(true))
        return;
      for (auto &x : *shared)
        x.cancel();
      try {
        promise->set_value(std::move(r));
      } catch (std::future_error const &) {
      }
    }).detach();
  }
  return Task<T>(result.share(), std::move(src));
}

// Value, or `err("timeout")` after the deadline — cancelling the source.
template <class T>
Task<T> timeout(Task<T> t, std::chrono::milliseconds ms) {
  auto src = t.source();
  return Task<T>(
      std::async(std::launch::async,
                 [t, ms]() mutable -> Result<T> {
                   if (t.wait_for(ms) == std::future_status::ready)
                     return t.get();
                   t.cancel();
                   return err<T>("timeout");
                 })
          .share(),
      std::move(src));
}

// `make(std::stop_token)` returns an AsyncResult<T>; T is deduced from it.
template <class F>
auto retry(std::stop_token tok, F make, size_t attempts,
           std::chrono::milliseconds delay) {
  using T = typename decltype(
      std::declval<std::invoke_result_t<F, std::stop_token>>().get())
      ::value_type;
  auto src = std::make_shared<std::stop_source>();
  auto stopper = [src] { src->request_stop(); };
  std::shared_ptr<std::stop_callback<decltype(stopper)>> bridge;
  if (tok.stop_possible())
    bridge =
        std::make_shared<std::stop_callback<decltype(stopper)>>(tok, stopper);

  auto fut = std::async(std::launch::async,
                        [make, attempts, delay, src, bridge]() mutable
                            -> Result<T> {
                          for (size_t i = 0; i < attempts; ++i) {
                            if (src->stop_requested())
                              return cancelled<T>();
                            Result<T> r = make(src->get_token()).get();
                            if (r.is_ok())
                              return r;
                            if (i + 1 == attempts)
                              break;
                            std::mutex m;
                            std::unique_lock lk(m);
                            std::condition_variable_any cv;
                            cv.wait_for(lk, src->get_token(), delay,
                                        [] { return false; });
                          }
                          if (src->stop_requested())
                            return cancelled<T>();
                          return err<T>("retry exhausted");
                        })
                 .share();
  return Task<T>(std::move(fut), std::move(src));
}
} // namespace fp

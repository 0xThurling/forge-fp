#pragma once
#include <algorithm>
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
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "result.hpp"

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

  void Send(Msg m) {
    mailbox_.send(Item{std::move(m), std::nullopt});
  }

  std::future<State> Ask(Msg m) {
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
  return std::async(std::launch::async, [futs = std::move(futs)]() {
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
  auto enqueue(F &&f, Ts &&...ts) -> std::future<std::invoke_result<F, Ts...>> {
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
  std::vector<R> out;
  out.reserve(v.size());
  std::vector<std::future<R>> futs;
  futs.reserve(v.size());
  for (auto const &x : v)
    futs.push_back(pool.enqueue([f, x] { return f(x); }));
  for (auto &fut : futs)
    out.push_back(fut.get());
  return out;
}

template <class T, class F>
void par_for_each(ThreadPool &pool, std::vector<T> const &v, F f) {
  std::vector<std::future<void>> futs;
  futs.reserve(v.size());
  for (auto const &x : v)
    futs.push_back(pool.enqueue([f, x] { f(x); }));
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

template <class T> class Async {
public:
  Async(std::future<T> fut)
      : shared_(std::make_shared<std::future<T>>(std::move(fut))) {}

  T get() const { return shared_.get(); }

  template <class F> auto then(F f) const -> Async<std::invoke_result_t<F, T>> {
    using R = std::invoke_result_t<F, T>;
    return Async<R>(
        std::async(std::launch::async, [shared = shared_, f = std::move(f)] {
          return f(shared.get());
        }));
  }

  template <class F>
  auto and_then(F f) const
      -> Async<typename std::invoke_result_t<F, T>::value_type> {
    using R = typename std::invoke_result_t<F, T>::value_type;
    return Async<R>(
        std::async(std::launch::async, [shared = shared_, f = std::move(f)] {
          return f(shared.get()).get();
        }));
  }

private:
  std::shared_ptr<std::future<T>> shared_;
};

template <class T> AsyncResult<T> race(std::vector<AsyncResult<T>> futs) {
  auto shared = std::make_shared<std::promise<Result<T>>>();
  std::future<Result<T>> result = shared->get_future();
  auto first_done = std::make_shared<std::atomic<bool>>(false);
  for (auto &f : futs) {
    std::async(std::launch::async,
               [shared, first_done, f = std::move(f)]() mutable {
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
               });
  }
  return result;
}

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
    } catch (std::future_error const &) {
    }
  });

  std::async(std::launch::async, [shared, ms]() {
    std::this_thread::sleep_for(ms);
    try {
      shared->set_value(err<T>("timeout"));
    } catch (std::future_error const &) {
    }
  });
  return result;
}

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
    return result;
  });
}
} // namespace fp

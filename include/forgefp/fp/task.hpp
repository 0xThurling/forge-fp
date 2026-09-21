#pragma once
// Cooperative cancellation for async work, built on C++20 std::stop_token.
//
// `Task<T>` is a cancellable `AsyncResult<T>` (`shared_future<Result<T>>` plus
// a shared `stop_source`). Cancellation is cooperative: `cancel()` only
// requests a stop, and producers/continuations observe it at their boundaries.
// A task that is superseded by another, or that runs long without checking its
// token, cannot be preempted.

#include "error.hpp"
#include "result.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp {

// Standard failure for a cancelled operation.
template <class T> Result<T> cancelled() { return err<T>("cancelled"); }
template <class T> Outcome<T> cancelled_outcome() {
  return Outcome<T>::err(error("cancelled", errc::cancelled));
}

// A stop source that requests stop after `ms`.
inline std::stop_source cancel_after(std::chrono::milliseconds ms) {
  std::stop_source src;
  std::thread([s = src, ms]() mutable {
    std::this_thread::sleep_for(ms);
    s.request_stop();
  }).detach();
  return src;
}

namespace detail {

template <class R> struct is_either : std::false_type {};
template <class E, class U> struct is_either<Either<E, U>> : std::true_type {};

inline std::string to_error_string(std::string s) { return s; }
inline std::string to_error_string(Error const &e) { return to_string(e); }

// Normalize any callable result into a Result: plain values become ok(...),
// Eithers keep their success/failure (structured errors flatten to strings).
template <class E, class U> Result<U> as_result(Either<E, U> r) {
  if (r.is_ok())
    return ok<U>(std::move(r.value()));
  return err<U>(to_error_string(r.error()));
}

template <class R> Result<std::decay_t<R>> as_result(R &&r) {
  return ok<std::decay_t<R>>(std::forward<R>(r));
}

// Producers accept callables that take a leading `std::stop_token` (so the
// body can poll for cancellation) or plain callables. The trait is lazy: only
// the applicable `invoke_result` is instantiated.
template <class F, class... Ts> struct producer_result {
private:
  template <class G>
  static auto pick(int) -> std::enable_if_t<
      std::is_invocable_v<G &, std::stop_token, Ts...>,
      std::invoke_result<G &, std::stop_token, Ts...>>;
  template <class G> static std::invoke_result<G &, Ts...> pick(...);

public:
  using type = typename decltype(pick<F>(0))::type;
};

template <class F, class... Ts>
using producer_result_t = typename producer_result<F, Ts...>::type;

template <class F, class... Ts>
decltype(auto) call_producer(F &f, std::stop_token tok, Ts &&...ts) {
  if constexpr (std::is_invocable_v<F &, std::stop_token, Ts...>)
    return f(tok, std::forward<Ts>(ts)...);
  else
    return f(std::forward<Ts>(ts)...);
}

template <class R, bool = is_either<std::remove_cvref_t<R>>::value>
struct task_value {
  using type = std::remove_cvref_t<R>;
};

template <class R> struct task_value<R, true> {
  using type = typename std::remove_cvref_t<R>::value_type;
};

template <class R> using task_value_t = typename task_value<R>::type;

} // namespace detail

template <class T> class Task {
public:
  using value_type = T;
  using result_type = Result<T>;

  Task(std::shared_future<Result<T>> fut, std::shared_ptr<std::stop_source> src)
      : fut_(std::move(fut)), src_(std::move(src)) {}

  // Request cancellation. All tasks sharing this chain observe it.
  void cancel() const { src_->request_stop(); }

  std::stop_token token() const { return src_->get_token(); }
  bool cancelled() const { return token().stop_requested(); }

  Result<T> get() const { return fut_.get(); }

  template <class Rep, class Period>
  std::future_status
  wait_for(std::chrono::duration<Rep, Period> const &d) const {
    return fut_.wait_for(d);
  }

  bool ready() const {
    return fut_.wait_for(std::chrono::milliseconds(0)) ==
           std::future_status::ready;
  }

  std::shared_ptr<std::stop_source> const &source() const { return src_; }

  // f : T -> U. Skipped (produces `cancelled`) if the chain was stopped or the
  // upstream task failed.
  template <class F> auto then(F f) const {
    if constexpr (std::is_void_v<T>) {
      using U = std::invoke_result_t<F>;
      auto src = src_;
      auto fut = fut_;
      return Task<U>(
          std::async(std::launch::async,
                     [f = std::move(f), src, fut]() mutable -> Result<U> {
                       if (src->stop_requested())
                         return fp::cancelled<U>();
                       auto r = fut.get();
                       if (!r.is_ok())
                         return err<U>(r.error());
                       return detail::as_result(f());
                     })
              .share(),
          src);
    } else {
      using U = std::invoke_result_t<F, T>;
      auto src = src_;
      auto fut = fut_;
      return Task<U>(
          std::async(std::launch::async,
                     [f = std::move(f), src, fut]() mutable -> Result<U> {
                       if (src->stop_requested())
                         return fp::cancelled<U>();
                       auto r = fut.get();
                       if (!r.is_ok())
                         return err<U>(r.error());
                       return detail::as_result(f(std::move(r.value())));
                     })
              .share(),
          src);
    }
  }

  // f : T -> Task<U>. Cancelling the returned task cancels the inner one.
  template <class F> auto and_then(F f) const {
    if constexpr (std::is_void_v<T>) {
      using U = typename std::invoke_result_t<F>::value_type;
      auto src = src_;
      auto fut = fut_;
      return Task<U>(
          std::async(std::launch::async,
                     [f = std::move(f), src, fut]() mutable -> Result<U> {
                       if (src->stop_requested())
                         return fp::cancelled<U>();
                       auto r = fut.get();
                       if (!r.is_ok())
                         return err<U>(r.error());
                       Task<U> next = f();
                       std::stop_callback cb(src->get_token(),
                                             [&next] { next.cancel(); });
                       return next.get();
                     })
              .share(),
          src);
    } else {
      using U = typename std::invoke_result_t<F, T>::value_type;
      auto src = src_;
      auto fut = fut_;
      return Task<U>(
          std::async(std::launch::async,
                     [f = std::move(f), src, fut]() mutable -> Result<U> {
                       if (src->stop_requested())
                         return fp::cancelled<U>();
                       auto r = fut.get();
                       if (!r.is_ok())
                         return err<U>(r.error());
                       Task<U> next = f(std::move(r.value()));
                       std::stop_callback cb(src->get_token(),
                                             [&next] { next.cancel(); });
                       return next.get();
                     })
              .share(),
          src);
    }
  }

  // f : Error -> T (or Result<T> / Outcome<T>). Turns a failure into a value.
  template <class F> Task<T> recover(F f) const {
    auto src = src_;
    auto fut = fut_;
    return Task<T>(std::async(std::launch::async,
                              [f = std::move(f), src, fut]() mutable
                                  -> Result<T> {
                                auto r = fut.get();
                                if (r.is_ok())
                                  return r;
                                return detail::as_result(f(r.error()));
                              })
                       .share(),
                   src);
  }

  // Run `f` over all tasks and return every result (errors included).
  static std::vector<Result<T>> join(std::vector<Task<T>> tasks) {
    std::vector<Result<T>> out;
    out.reserve(tasks.size());
    for (auto &t : tasks)
      out.push_back(t.get());
    return out;
  }

private:
  std::shared_future<Result<T>> fut_;
  std::shared_ptr<std::stop_source> src_;
};

} // namespace fp

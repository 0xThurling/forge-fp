#pragma once
#include "forgefp/fp/compose.hpp"
#include "forgefp/fp/concurrent.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp {

template <class T> class Stream {
public:
  using Source = std::function<std::optional<T>()>;

  explicit Stream(Source src) : src_(std::move(src)) {}
  Stream(Channel<T> &ch) : src_([&ch] { return ch.try_recv(); }) {}

  template <class F> auto map(F f) const {
    using R = std::invoke_result_t<F, T>;
    return Stream<R>([src = src_, f = std::move(f)]() -> std::optional<R> {
      auto x = src();
      return x ? std::optional<R>(f(*x)) : std::nullopt;
    });
  }

  template <class F> auto filter(F pred) const {
    return Stream<T>(
        [src = src_, pred = std::move(pred)]() -> std::optional<T> {
          for (;;) {
            auto x = src();
            if (!x || pred(*x))
              return x;
          }
        });
  }

  template <class F> void subscribe(F on_item) const {
    for (;;) {
      auto x = src_();
      if (!x)
        break;
      on_item(*x);
    }
  }

  // Materialize the stream by draining it.
  std::vector<T> collect() const {
    std::vector<T> out;
    for (;;) {
      auto x = src_();
      if (!x)
        break;
      out.push_back(std::move(*x));
    }
    return out;
  }
  std::vector<T> to_vector() const { return collect(); }

  // At most `n` items.
  template <class N> auto take(N n) const {
    return Stream<T>([src = src_, n, i = std::size_t{0}]() mutable
                         -> std::optional<T> {
      if (i >= static_cast<std::size_t>(n))
        return std::nullopt;
      auto x = src();
      if (!x)
        return std::nullopt;
      ++i;
      return x;
    });
  }

  // Items while `pred` holds, then end.
  template <class F> auto take_while(F pred) const {
    return Stream<T>([src = src_, pred = std::move(pred), done = false]() mutable
                         -> std::optional<T> {
      if (done)
        return std::nullopt;
      auto x = src();
      if (!x || !pred(*x)) {
        done = true;
        return std::nullopt;
      }
      return x;
    });
  }

  // Every intermediate accumulator, like fp::scan.
  template <class S, class F> auto scan(S init, F op) const {
    using R = decltype(op(std::declval<S>(), std::declval<T>()));
    return Stream<R>([src = src_, acc = std::move(init),
                      op = std::move(op)]() mutable -> std::optional<R> {
      auto x = src();
      if (!x)
        return std::nullopt;
      acc = op(acc, *x);
      return acc;
    });
  }

  // Drain to a single value, like fp::fold_left.
  template <class S, class F> S fold_left(S init, F op) const {
    for (;;) {
      auto x = src_();
      if (!x)
        break;
      init = op(std::move(init), *x);
    }
    return init;
  }

  // All items of this stream, then all items of `other`.
  Stream concat(Stream other) const {
    return Stream([src = src_, other = std::move(other),
                   first = true]() mutable -> std::optional<T> {
      if (first) {
        auto x = src();
        if (x)
          return x;
        first = false;
      }
      return other.src_();
    });
  }

private:
  Source src_;
};

// pipe: `into(stream) | f` maps `f` over the items.
template <class T, class F>
auto operator|(Piped<Stream<T>> p, F f) {
  return Piped{p.value.map(std::move(f))};
}

} // namespace fp

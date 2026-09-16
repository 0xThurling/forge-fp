#pragma once
#include "concurrent.hpp"
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

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

private:
  Source src_;
};

} // namespace fp

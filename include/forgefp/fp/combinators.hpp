#pragma once

#include "compose.hpp"
#include <functional>
#include <type_traits>
#include <utility>

namespace fp {
template <class T> T identity(T x) { return x; }

template <class T> auto const_(T x) {
  return [x](auto &&...) { return x; };
}

template <class F> auto flip(F f) {
  return [f](auto a, auto b) { return f(b, a); };
}

template <class F, class G> auto on(F f, G g) {
  return [f = std::move(f), g = std::move(g)](auto const &x,
                                              auto const &y) -> decltype(auto) {
    return f(g(x), g(y));
  };
}

template <class F> auto compose(F f) { return f; }

template <class F, class G, class... Fs> auto compose(F f, G g, Fs... fs) {
  return [f = std::move(f),
          rest = compose(std::move(g), std::move(fs)...)](auto &&...args) {
    return f(rest(std::forward<decltype(args)>(args)...));
  };
}

template <class F> auto fix(F f) {
  return [f = std::move(f)](auto &&...args) -> decltype(auto) {
    auto impl = [&f](auto &&impl, auto &&...inner) -> decltype(auto) {
      auto recur = [&impl](auto &&...next) -> decltype(auto) {
        return impl(impl, std::forward<decltype(next)>(next)...);
      };

      return std::invoke(f, recur, std::forward<decltype(inner)>(inner)...);
    };

    return impl(impl, std::forward<decltype(args)>(args)...);
  };
}

template <class F, class... Ts>
auto apply(F f, Ts... ts) -> std::invoke_result_t<F, Ts...> {
  return std::invoke(std::move(f), std::move(ts)...);
}

template <class F> auto when(bool cond, F f) {
  return [cond, f = std::move(f)](auto &&x) {
    return cond ? f(std::forward<decltype(x)>(x))
                : std::forward<decltype(x)>(x);
  };
}

template <class F> auto unless(bool cond, F f) {
  return when(!cond, std::move(f));
}

template <class F> auto first(F f) {
  return [f = std::move(f)](auto p) {
    p.first = f(std::move(p.first));
    return p;
  };
}

template <class F> auto second(F f) {
  return [f = std::move(f)](auto p) {
    p.second = f(std::move(p.second));
    return p;
  };
}

template <class T, class F, class... Ts>
auto pipe_with(Piped<T> p, F f, Ts const &...ts) {
  return Piped<decltype(f(std::move(p.value), ts...))>{
      f(std::move(p.value, ts...))};
}
} // namespace fp

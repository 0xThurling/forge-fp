#pragma once
#include <type_traits>
#include <utility>

namespace fp {
template <class F, class... Bound> auto curry(F f, Bound... bound) {
  return [f, bound...](auto &&...rest) {
    if constexpr (std::is_invocable_v<F, Bound..., decltype(rest)...>) {
      return f(bound..., std::forward<decltype(rest)>(rest)...);
    } else {
      return curry(f, bound..., std::forward<decltype(rest)>(rest)...);
    }
  };
}

template <class F> decltype(auto) uncurry_step(F f) { return std::move(f); }

template <class F, class T, class... Ts>
decltype(auto) uncurry_step(F f, T t, Ts... ts) {
  return uncurry_step(std::move(f)(std::move(t)), std::move(ts)...);
}

template <class F> auto uncurry(F f) {
  return [f = std::move(f)](auto &&...args) -> decltype(auto) {
    return uncurry_step(f, std::forward<decltype(args)>(args)...);
  };
}
} // namespace fp

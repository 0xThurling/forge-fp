#pragma once
#include "compose.hpp"
#include <optional>
#include <type_traits>
#include <vector>

namespace fp {
template <class T, class F>
auto map(std::optional<T> const &o, F f)
    -> std::optional<std::invoke_result_t<F, T>> {
  if (!o)
    return std::nullopt;
  return f(*o);
}

template <class T, class F>
auto and_then(std::optional<T> const &o, F f) -> std::invoke_result_t<F, T> {
  if (!o)
    return std::nullopt;
  return f(*o);
}

template <class T> T or_else(std::optional<T> const &o, T fallback) {
  return o.value_or(fallback);
}

template <class T, class F>
[[nodiscard]] std::optional<T> filter(std::optional<T> const &o, F pred) {
  if (o && pred(*o))
    return o;
  return std::nullopt;
}

template <class T, class F> T or_else(std::optional<T> const &o, F fallback) {
  return o ? *o : fallback();
}

template <class T>
[[nodiscard]] std::optional<T> flatten(std::optional<std::optional<T>> const &o) {
  if (!o) {
    return std::nullopt;
  }
  return *o;
}

template <class F, class T>
auto apply(std::optional<F> const &of, std::optional<T> const &o)
    -> std::optional<std::invoke_result_t<F, T>> {
  if (!of || !o) {
    return std::nullopt;
  }
  return (*of)(*o);
}

template <class T>
[[nodiscard]] std::optional<std::vector<T>> collect(std::vector<std::optional<T>> const &os) {
  std::vector<T> out;
  out.reserve(os.size());
  for (auto const &o : os) {
    if (!o)
      return std::nullopt;
    out.push_back(*o);
  }
  return out;
}

template <class T, class F>
T value_or_lazy(std::optional<T> const &o, F fallback) {
  return or_else(o, std::move(fallback));
}

template <class T, class F>
auto operator>>=(std::optional<T> const &o, F f) -> std::invoke_result_t<F, T> {
  return fp::and_then(o, std::move(f));
}

// pipe: `into(optional) | f` maps `f` over the value (propagating nullopt).
template <class T, class F>
auto operator|(Piped<std::optional<T>> p, F f) {
  return Piped{fp::map(p.value, std::move(f))};
}
} // namespace fp

#pragma once
#include "forgefp/fp/result.hpp"
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace fp {
template <class... Fs> struct overload : Fs... {
  using Fs::operator()...;
};
template <class... Fs> overload(Fs...) -> overload<Fs...>;

template <class Variant, class... Fs> auto match(Variant &&v, Fs... fs) {
  return std::visit(overload{fs...}, std::forward<Variant>(v));
}

template <class T, class F> auto case_(F f) {
  return [f = std::move(f)](T const &v) -> decltype(auto) { return f(v); };
}

template <class P, class F> struct Arm {
  P pred;
  F fn;
};

template <class P, class F> Arm<P, F> when(P p, F f) {
  return {std::move(p), std::move(f)};
}

struct always_t {
  template <class T> bool operator()(T const &) const { return true; }
};

inline constexpr always_t always{};

template <class F> auto otherwise(F f) { return when(always, std::move(f)); }

template <class T, class P, class F, class... Rest>
auto cond(T const &v, Arm<P, F> a, Rest &&...rest) {
  if (a.pred(v))
    return a.fn(v);
  if constexpr (sizeof...(Rest) > 0) {
    return cond(v, std::forward<Rest>(rest)...);
  }
  throw ::std::runtime_error("cond: no arm matched");
}

template <class T, class F, class G>
auto match(std::optional<T> const &o, F some, G none) {
  return o ? some(*o) : none();
}

template <class T, class F, class G>
auto match(Result<T> const &r, F ok_f, G err_f) {
  return r.is_ok() ? ok_f(r.value()) : err_f(r.error());
}

template <class T> T value_or(std::optional<T> const &o, T fallback) {
  return o.value_or(std::move(fallback));
}

template <class F> auto unpack(F f) {
  return [f = std::move(f)](auto p) {
    return f(std::move(p.first), std::move(p.second));
  };
}
} // namespace fp

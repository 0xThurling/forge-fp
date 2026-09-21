#pragma once
#include "result.hpp"
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace fp {
template <class... Fs> struct overload : Fs... {
  using Fs::operator()...;
};
template <class... Fs> overload(Fs...) -> overload<Fs...>;

namespace detail {
template <class> struct is_variant : std::false_type {};
template <class... Ts> struct is_variant<std::variant<Ts...>> : std::true_type {};
template <class T>
inline constexpr bool is_variant_v = is_variant<std::remove_cvref_t<T>>::value;
} // namespace detail

// Dispatch a std::variant to a set of arms (exhaustive, compiler-checked).
template <class Variant, class... Fs>
  requires detail::is_variant_v<Variant>
auto match(Variant &&v, Fs... fs) {
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

// Either / Result / Validation
template <class E, class T, class F, class G>
auto match(Either<E, T> const &e, F ok_f, G err_f) {
  return e.is_ok() ? ok_f(e.value()) : err_f(e.error());
}

// Either<E, void> / Result<void>
template <class E, class F, class G>
auto match(Either<E, void> const &e, F ok_f, G err_f) {
  return e.is_ok() ? ok_f() : err_f(e.error());
}

template <class T> T value_or(std::optional<T> const &o, T fallback) {
  return o.value_or(std::move(fallback));
}

template <class E, class T>
T value_or(Either<E, T> const &e, T fallback) {
  return e.is_ok() ? e.value() : std::move(fallback);
}

template <class F> auto unpack(F f) {
  return [f = std::move(f)](auto p) {
    return f(std::move(p.first), std::move(p.second));
  };
}
} // namespace fp

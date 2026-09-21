#pragma once
#include <cmath>
#include <functional>
#include <type_traits>
#include <utility>

namespace fp {
namespace detail {

template <class BinOp> struct left_curried {
  template <class A> constexpr auto operator()(A a) const {
    return [a = std::move(a)](auto b) { return BinOp{}(a, b); };
  }
  template <class A, class B> constexpr auto operator()(A a, B b) const {
    return BinOp{}(a, b);
  }
};

template <class BinOp> struct flipped_curried {
  template <class A> constexpr auto operator()(A a) const {
    return [a = std::move(a)](auto b) { return BinOp{}(b, a); };
  }
  template <class A, class B> constexpr auto operator()(A a, B b) const {
    return BinOp{}(a, b);
  }
};

struct min_op {
  template <class A, class B>
  constexpr auto operator()(A const &a, B const &b) const {
    return b < a ? b : a;
  }
};

struct max_op {
  template <class A, class B>
  constexpr auto operator()(A const &a, B const &b) const {
    return a < b ? b : a;
  }
};

} // namespace detail

// arithmetic — left-curried: plus(1)(x) == 1 + x
inline constexpr detail::left_curried<std::plus<>> plus{};
inline constexpr detail::left_curried<std::minus<>> minus{};
inline constexpr detail::left_curried<std::multiplies<>> times{};
inline constexpr detail::left_curried<std::divides<>> divide{};

// comparison — predicate-curried: gt(0)(x) == x > 0
inline constexpr detail::flipped_curried<std::equal_to<>> eq{};
inline constexpr detail::flipped_curried<std::not_equal_to<>> ne{};
inline constexpr detail::flipped_curried<std::less<>> lt{};
inline constexpr detail::flipped_curried<std::less_equal<>> le{};
inline constexpr detail::flipped_curried<std::greater<>> gt{};
inline constexpr detail::flipped_curried<std::greater_equal<>> ge{};

// logic — left-curried like arithmetic (trailing _ avoids the keyword)
inline constexpr detail::left_curried<std::logical_and<>> and_{};
inline constexpr detail::left_curried<std::logical_or<>> or_{};

// unary — plain lambdas, no curried form
inline constexpr auto negate    = [](auto a) { return -a; };
inline constexpr auto not_      = [](auto a) { return !a; };
inline constexpr auto increment = [](auto a) { return a + 1; };
inline constexpr auto decrement = [](auto a) { return a - 1; };

// elementwise math — same shape as the other unary operators
inline constexpr auto abs = [](auto x) {
  using T = std::decay_t<decltype(x)>;
  if constexpr (std::is_unsigned_v<T>)
    return x;
  else
    return x < 0 ? -x : x;
};
inline constexpr auto sqrt  = [](auto x) { return std::sqrt(x); };
inline constexpr auto exp   = [](auto x) { return std::exp(x); };
inline constexpr auto log   = [](auto x) { return std::log(x); };
inline constexpr auto log1p = [](auto x) { return std::log1p(x); };
inline constexpr auto sign  = [](auto x) { return (x > 0) - (x < 0); };

// min_/max_ follow the comparison convention: the captured value is the right
// operand, so max_(0)(x) == max(x, 0).
inline constexpr detail::flipped_curried<detail::min_op> min_{};
inline constexpr detail::flipped_curried<detail::max_op> max_{};

// pow(k)(x) == x^k
template <class T> auto pow(T k) {
  return [k](auto x) { return std::pow(x, k); };
}

// clamp(lo, hi)(x) == min(max(x, lo), hi)
template <class T> auto clamp(T lo, T hi) {
  return [lo, hi](auto x) { return x < lo ? lo : (hi < x ? hi : x); };
}
} // namespace fp

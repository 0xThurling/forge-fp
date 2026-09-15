#pragma once
#include <functional>
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
} // namespace fp

#pragma once
// Lazy, non-owning views over ranges: the std::views vocabulary under the fp
// umbrella, plus the adaptors C++20 lacks (filter_map, flat_map, enumerate,
// zip). Views are lazy and borrow their source; the only owning case is a view
// built from an rvalue (`std::views::all` wraps it in an owning_view).
//
// Terminate a pipeline with `fp::to_vector` (or a fold); never let a view
// outlive the range it borrows.
//
// Inherently non-lazy operations (sort, partition, span, chunk, windows,
// group_by) stay eager and live in the `fp::` namespace.

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>

namespace fp::views {

inline constexpr auto map = std::views::transform;
inline constexpr auto filter = std::views::filter;
inline constexpr auto take = std::views::take;
inline constexpr auto drop = std::views::drop;
inline constexpr auto take_while = std::views::take_while;
inline constexpr auto drop_while = std::views::drop_while;
inline constexpr auto reverse = std::views::reverse;
inline constexpr auto join = std::views::join;

// map + drop-the-empties in one lazy pass: f returns an optional; present
// values are kept, nullopts skipped.
inline constexpr auto filter_map = [](auto f) {
  return std::views::transform(std::move(f)) |
         std::views::filter([](auto &&o) { return static_cast<bool>(o); }) |
         std::views::transform(
             [](auto &&o) { return *std::forward<decltype(o)>(o); });
};

// map each element to a range, then flatten (lazy).
inline constexpr auto flat_map = [](auto f) {
  return std::views::transform(std::move(f)) | std::views::join;
};

namespace detail {

// A copyable handle to `r`: a borrowing ref_view for lvalues, an owning copy
// for rvalues. (`std::views::all` would give an `owning_view`, which is
// move-only, and view adaptors require copyable callables.)
template <class R> auto hold(R &&r) {
  if constexpr (std::is_lvalue_reference_v<R>)
    return std::views::all(r);
  else
    return std::remove_cvref_t<R>(std::forward<R>(r));
}

} // namespace detail

// Pair each element with its index. Requires a random-access range.
template <std::ranges::random_access_range R> auto enumerate(R &&r) {
  auto base = detail::hold(std::forward<R>(r));
  using T = std::ranges::range_value_t<decltype(base)>;
  auto n = std::ranges::size(base);
  return std::views::iota(std::size_t{0}, n) |
         std::views::transform(
             [base, n](std::size_t i) mutable
             -> std::pair<std::size_t, T> { return {i, base[i]}; });
}

// Closure form for `fp::into(r) | fp::views::enumerate()`.
inline auto enumerate() {
  return [](auto &&r) {
    return fp::views::enumerate(std::forward<decltype(r)>(r));
  };
}

// Zip two random-access ranges, truncating to the shorter one.
template <std::ranges::random_access_range R1,
          std::ranges::random_access_range R2>
auto zip(R1 &&a, R2 &&b) {
  auto va = detail::hold(std::forward<R1>(a));
  auto vb = detail::hold(std::forward<R2>(b));
  using A = std::ranges::range_value_t<decltype(va)>;
  using B = std::ranges::range_value_t<decltype(vb)>;
  auto n = std::min(std::ranges::size(va), std::ranges::size(vb));
  return std::views::iota(std::size_t{0}, n) |
         std::views::transform([va, vb](std::size_t i) mutable
                               -> std::pair<A, B> { return {va[i], vb[i]}; });
}

// Closure form for `fp::into(a) | fp::views::zip(b)`. `other` is held by
// value (moved), so the closure keeps it alive.
template <class R> auto zip(R &&other) {
  auto kept = detail::hold(std::forward<R>(other));
  return [kept](auto &&a) mutable {
    return fp::views::zip(std::forward<decltype(a)>(a), kept);
  };
}

} // namespace fp::views

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
#include <tuple>
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
inline constexpr auto iota = std::views::iota;

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

// A closure that supports `range | closure` as well as fp's
// `into(x) | closure`. Plain lambdas only work with the latter, so the new
// adaptors wrap theirs in this type.
template <class F> struct pipe_closure {
  F f;
  explicit pipe_closure(F fn) : f(std::move(fn)) {}
  template <class R> auto operator()(R &&r) {
    return f(std::forward<R>(r));
  }
};

template <class T> struct is_pipe_closure : std::false_type {};
template <class F> struct is_pipe_closure<pipe_closure<F>> : std::true_type {};

// Enables `range | fp::views::chunk(n)` (ADL finds this through the closure
// type's namespace).
template <std::ranges::range R, class F>
  requires is_pipe_closure<std::remove_cvref_t<F>>::value
auto operator|(R &&r, F &&f) {
  return std::forward<F>(f)(std::forward<R>(r));
}

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

// --- chunk / slide / stride -------------------------------------------------

// Non-overlapping groups of `n`; each element is a subrange view into the
// source. The source is held by value, so an rvalue pipeline owns its data.
template <std::ranges::random_access_range R> auto chunk(R &&r, std::size_t n) {
  auto base = detail::hold(std::forward<R>(r));
  auto total = std::ranges::size(base);
  auto count = n == 0 ? std::size_t{0} : (total + n - 1) / n;
  return std::views::iota(std::size_t{0}, count) |
         std::views::transform([base, n, total](std::size_t i) mutable {
           const auto start = i * n;
           const auto len = std::min(n, total - start);
           return std::views::counted(std::ranges::begin(base) + start, len);
         });
}

// Closure form for `into(r) | fp::views::chunk(n)`.
inline auto chunk(std::size_t n) {
  return pipe_closure{[n](auto &&r) {
    return fp::views::chunk(std::forward<decltype(r)>(r), n);
  }};
}

// Overlapping windows of `n` (sliding window).
template <std::ranges::random_access_range R> auto slide(R &&r, std::size_t n) {
  auto base = detail::hold(std::forward<R>(r));
  auto total = std::ranges::size(base);
  auto count = (n > 0 && total >= n) ? total - n + 1 : std::size_t{0};
  return std::views::iota(std::size_t{0}, count) |
         std::views::transform([base, n](std::size_t i) mutable {
           return std::views::counted(std::ranges::begin(base) + i, n);
         });
}

inline auto slide(std::size_t n) {
  return pipe_closure{[n](auto &&r) {
    return fp::views::slide(std::forward<decltype(r)>(r), n);
  }};
}

// Every `n`-th element (0 -> empty).
template <std::ranges::random_access_range R>
auto stride(R &&r, std::size_t n) {
  auto base = detail::hold(std::forward<R>(r));
  auto total = std::ranges::size(base);
  auto count = n == 0 ? std::size_t{0} : (total + n - 1) / n;
  return std::views::iota(std::size_t{0}, count) |
         std::views::transform(
             [base, n](std::size_t i) mutable -> decltype(auto) {
               return base[i * n];
             });
}

inline auto stride(std::size_t n) {
  return pipe_closure{[n](auto &&r) {
    return fp::views::stride(std::forward<decltype(r)>(r), n);
  }};
}

// Last `n` elements (random access: needs the length).
template <std::ranges::random_access_range R>
auto take_last(R &&r, std::size_t n) {
  auto base = detail::hold(std::forward<R>(r));
  auto total = std::ranges::size(base);
  auto skip = total > n ? total - n : std::size_t{0};
  return base | std::views::drop(skip);
}

inline auto take_last(std::size_t n) {
  return pipe_closure{[n](auto &&r) {
    return fp::views::take_last(std::forward<decltype(r)>(r), n);
  }};
}

// --- fused zip --------------------------------------------------------------

// Fused zip + transform: `into(a) | fp::views::zip_with(b, f)`. Both ranges are
// owned by the returned view, so no reference outlives the closure.
template <std::ranges::random_access_range R, class F>
auto zip_with(R &&other, F f) {
  auto kept = detail::hold(std::forward<R>(other));
  return pipe_closure{[kept = std::move(kept), f = std::move(f)](
                                  auto &&a) mutable {
    auto base = detail::hold(std::forward<decltype(a)>(a));
    using A = std::ranges::range_value_t<decltype(base)>;
    using B = std::ranges::range_value_t<decltype(kept)>;
    auto n = std::min(std::ranges::size(base), std::ranges::size(kept));
    return std::views::iota(std::size_t{0}, n) |
           std::views::transform([base, kept, f](std::size_t i) mutable
                                     -> std::invoke_result_t<F, A, B> {
               return f(base[i], kept[i]);
             });
  }};
}

// Three-way zip into tuples, truncating to the shortest range.
template <std::ranges::random_access_range R1,
          std::ranges::random_access_range R2,
          std::ranges::random_access_range R3>
auto zip3(R1 &&a, R2 &&b, R3 &&c) {
  auto va = detail::hold(std::forward<R1>(a));
  auto vb = detail::hold(std::forward<R2>(b));
  auto vc = detail::hold(std::forward<R3>(c));
  using A = std::ranges::range_value_t<decltype(va)>;
  using B = std::ranges::range_value_t<decltype(vb)>;
  using C = std::ranges::range_value_t<decltype(vc)>;
  auto n = std::min({std::ranges::size(va), std::ranges::size(vb),
                     std::ranges::size(vc)});
  return std::views::iota(std::size_t{0}, n) |
         std::views::transform(
             [va, vb, vc](std::size_t i) mutable -> std::tuple<A, B, C> {
               return {va[i], vb[i], vc[i]};
             });
}

} // namespace fp::views

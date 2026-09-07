#pragma once
#include "fp/vec.hpp"
#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fp {

template <std::ranges::range R, class F> auto filter_map(R &&r, F f) {
  using T = std::invoke_result_t<F, std::ranges::range_value_t<R>>;
  std::vector<typename T::value_type> out;
  for (auto &&x : r) {
    if (auto v = f(x))
      out.push_back(*v);
  }

  return out;
}

template <std::ranges::range R, class T, class F>
T fold_left(R &&r, T init, F f) {
  for (auto &&x : r)
    init = f(std::move(init), x);
  return init;
}

template <std::ranges::range R, class F> auto map(R &&r, F f) {
  using T = std::ranges::range_value_t<R>;
  std::vector<std::invoke_result_t<F, T>> out;
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r));
  for (auto &&x : r)
    out.push_back(f(std::forward<decltype(x)>(x)));
  return out;
}

template <std::ranges::range R, class F> auto filter(R &&r, F pred) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  if constexpr (std::ranges::sized_range<R>) {
    out.reserve(std::ranges::size(r));
  }
  for (auto &&x : r)
    if (pred(x))
      out.push_back(std::forward<decltype(x)>(x));
  return out;
}

template <std::ranges::range R> auto take(R &&r, size_t n) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  size_t i = 0;
  for (auto it = std::ranges::begin(r); it != std::ranges::end(r) && i < n;
       ++it, ++i)
    out.push_back(*it);
  return out;
}

template <std::ranges::range R> auto drop(R &&r, size_t n) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  auto it = std::ranges::begin(r);
  auto end = std::ranges::end(r);
  for (size_t i = 0; i < n && it != end; ++i, ++it) {
  }
  for (; it != end; ++it)
    out.push_back(*it);
  return out;
}

template <std::ranges::range R> auto concat(R &&rs) {
  using Inner = std::ranges::range_value_t<R>;
  static_assert(std::ranges::range<Inner>);
  using T = std::ranges::range_value_t<Inner>;
  std::vector<T> out;
  for (auto &&xs : rs)
    for (auto &&x : xs)
      out.push_back(std::forward<decltype(x)>(x));
  return out;
}

template <std::ranges::range R, class F> bool all(R &&r, F pred) {
  return std::ranges::all_of(r, pred);
}

template <std::ranges::range R, class F> bool any(R &&r, F pred) {
  return std::ranges::any_of(r, pred);
}

template <std::ranges::range R, class F> bool none(R &&r, F pred) {
  return std::ranges::none_of(r, pred);
}

template <std::ranges::range R, class F> size_t count(R &&r, F pred) {
  return std::ranges::count_if(r, pred);
}

template <std::ranges::range R, class T> size_t count(R &&r, T const &v) {
  return std::ranges::count(r, v);
}

template <std::ranges::range R> auto to_vector(R &&r) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  if constexpr (std::ranges::sized_range<R>) {
    out.reserve(std::ranges::size(r));
    for (auto &&x : r)
      out.push_back(std::forward<decltype(x)>(x));
    return out;
  }
}

template <std::ranges::range R, class F> auto flat_map(R &&r, F f) {
  using Sub = std::invoke_result_t<F, std::ranges::range_value_t<R>>;
  static_assert(std::ranges::range<Sub>);
  using T = std::ranges::range_value_t<Sub>;
  std::vector<T> out;
  for (auto &&x : r)
    for (auto &&y : f(x))
      out.push_back(std::forward<decltype(y)>(y));

  return out;
}

template <std::ranges::range R, class F> auto group_by(R &&r, F key_fn) {
  using T = std::ranges::range_value_t<R>;
  using K = std::invoke_result_t<F, T>;
  std::unordered_map<K, std::vector<T>> out;
  for (auto &&x : r)
    out[key_fn(x)].push_back(std::forward<decltype(x)>(x));
  return out;
}

template <std::ranges::range R1, std::ranges::range R2>
auto zip(R1 &&a, R2 &&b) {
  using A = std::ranges::range_value_t<R1>;
  using B = std::ranges::range_value_t<R2>;
  std::vector<std::pair<A, B>> out;
  auto ia = std::ranges::begin(a), ib = std::ranges::begin(a);
  auto ea = std::ranges::end(a), eb = std::ranges::end(b);
  for (; ia != ea && ib != eb; ++ia, ++ib)
    out.emplace_back(*ia, *ib);
  return out;
}

template <std::ranges::range R> auto enumerate(R &&r) {
  using T = std::ranges::range_value_t<R>;
  std::vector<std::pair<size_t, T>> out;
  size_t i = 0;
  for (auto &&x : r)
    out.emplace_back(i++, std::forward<decltype(x)>(x));
  return out;
}

template <std::ranges::range R, class T, class F>
T fold_right(R &&r, T init, F op) {
  std::vector<std::ranges::range_value_t<R>> tmp(r.begin(), r.end());
  for (size_t i = tmp.size(); i-- > 0;)
    init = op(tmp[i], std::move(init));
  return init;
}

template <std::ranges::range R, class T, class F>
std::vector<T> scan(R &&r, T init, F op) {
  std::vector<T> out;
  for (auto &&x : r) {
    init = op(init, x);
    out.push_back(init);
  }
  return out;
}

template <std::ranges::range R> auto chunk(R &&r, size_t n) {
  using T = std::ranges::range_value_t<R>;
  std::vector<std::vector<T>> out;
  if (n == 0)
    return out;
  std::vector<T> cur;
  for (auto &&x : r) {
    cur.push_back(std::forward<decltype(x)>(x));
    if (cur.size() == n) {
      out.push_back(std::move(cur));
      cur.clear();
    }
  }
  if (!cur.empty())
    out.push_back(std::move(cur));
  return out;
}

template <std::ranges::range R> auto windows(R &&r, size_t n) {
  using T = std::ranges::range_value_t<R>;
  std::vector<std::vector<T>> out;
  std::vector<T> buf;
  for (auto &&x : r) {
    buf.push_back(std::forward<decltype(x)>(x));
    if (buf.size() > n)
      buf.erase(buf.begin());
    if (buf.size() == n)
      out.push_back(buf);
  }
  return out;
}

template <std::ranges::range R>
std::optional<std::ranges::range_value_t<R>> minimum(R &&r) {
  if (std::ranges::empty(r))
    return std::nullopt;
  return *std::ranges::min_element(r);
}

template <std::ranges::range R>
std::optional<std::ranges::range_value_t<R>> maximum(R &&r) {
  if (std::ranges::empty(r))
    return std::nullopt;
  return *std::ranges::max_element(r);
}
} // namespace fp

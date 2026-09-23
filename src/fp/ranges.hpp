#pragma once
#include "vec.hpp"
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
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::min<std::size_t>(n, std::ranges::size(r)));
  size_t i = 0;
  for (auto it = std::ranges::begin(r); it != std::ranges::end(r) && i < n;
       ++it, ++i)
    out.push_back(*it);
  return out;
}

template <std::ranges::range R> auto drop(R &&r, size_t n) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r) - std::min<std::size_t>(n, std::ranges::size(r)));
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

template <std::ranges::range R, class F>
  requires std::predicate<F &, std::ranges::range_value_t<R>>
size_t count(R &&r, F pred) {
  return std::ranges::count_if(r, pred);
}

template <std::ranges::range R, class T>
  requires(!std::predicate<T &, std::ranges::range_value_t<R>>)
size_t count(R &&r, T const &v) {
  return std::ranges::count(r, v);
}

template <std::ranges::range R> auto to_vector(R &&r) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r));
  for (auto &&x : r)
    out.push_back(std::forward<decltype(x)>(x));
  return out;
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
  if constexpr (std::ranges::sized_range<R1> && std::ranges::sized_range<R2>)
    out.reserve(std::min<std::size_t>(std::ranges::size(a), std::ranges::size(b)));
  // Separate declarations: one `auto` statement cannot deduce two different
  // iterator types (e.g. zip(vector<int>, vector<string>)).
  auto ia = std::ranges::begin(a);
  auto ib = std::ranges::begin(b);
  auto ea = std::ranges::end(a);
  auto eb = std::ranges::end(b);
  for (; ia != ea && ib != eb; ++ia, ++ib)
    out.emplace_back(*ia, *ib);
  return out;
}

template <std::ranges::range R> auto enumerate(R &&r) {
  using T = std::ranges::range_value_t<R>;
  std::vector<std::pair<size_t, T>> out;
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r));
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
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r));
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

// Every window of `n` consecutive elements. `n == 0` yields nothing (like
// chunk); a ring buffer keeps the sliding window O(1) per element instead of
// erasing from the front.
template <std::ranges::range R> auto windows(R &&r, size_t n) {
  using T = std::ranges::range_value_t<R>;
  std::vector<std::vector<T>> out;
  if (n == 0)
    return out;
  std::vector<T> ring(n);
  size_t seen = 0;
  for (auto &&x : r) {
    ring[seen % n] = std::forward<decltype(x)>(x);
    ++seen;
    if (seen >= n) {
      std::vector<T> window;
      window.reserve(n);
      for (size_t k = 0; k < n; ++k)
        window.push_back(ring[(seen - n + k) % n]);
      out.push_back(std::move(window));
    }
  }
  return out;
}

template <std::ranges::range R>
[[nodiscard]] std::optional<std::ranges::range_value_t<R>> minimum(R &&r) {
  if (std::ranges::empty(r))
    return std::nullopt;
  return *std::ranges::min_element(r);
}

template <std::ranges::range R>
[[nodiscard]] std::optional<std::ranges::range_value_t<R>> maximum(R &&r) {
  if (std::ranges::empty(r))
    return std::nullopt;
  return *std::ranges::max_element(r);
}

// --- parity with vec.hpp over any range -----------------------------------

template <std::ranges::range R, class F>
auto take_while(R &&r, F pred) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  for (auto &&x : r) {
    if (!pred(x))
      break;
    out.push_back(std::forward<decltype(x)>(x));
  }
  return out;
}

template <std::ranges::range R, class F>
auto drop_while(R &&r, F pred) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out;
  auto it = std::ranges::begin(r);
  auto end = std::ranges::end(r);
  for (; it != end && pred(*it); ++it) {
  }
  for (; it != end; ++it)
    out.push_back(*it);
  return out;
}

template <std::ranges::range R> auto unique(R &&r) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out(r.begin(), r.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

template <std::ranges::range R> auto sort(R &&r) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out(r.begin(), r.end());
  std::ranges::sort(out);
  return out;
}

// See vec.hpp's sort_by: the key is evaluated per comparison (allocation-free,
// best for cheap keys).
template <std::ranges::range R, class F> auto sort_by(R &&r, F key_fn) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> out(r.begin(), r.end());
  std::ranges::sort(out, [&](T const &a, T const &b) {
    return key_fn(a) < key_fn(b);
  });
  return out;
}

// Keys computed once: better when the key is expensive to produce.
template <std::ranges::range R, class F> auto sort_by_cached(R &&r, F key_fn) {
  using T = std::ranges::range_value_t<R>;
  using K = std::invoke_result_t<F, T>;
  std::vector<std::pair<K, T>> keyed;
  for (auto &&x : r)
    keyed.emplace_back(key_fn(x), std::forward<decltype(x)>(x));
  std::ranges::stable_sort(keyed, {}, &std::pair<K, T>::first);
  std::vector<T> out;
  out.reserve(keyed.size());
  for (auto &kv : keyed)
    out.push_back(std::move(kv.second));
  return out;
}

template <std::ranges::range R, class F> auto partition(R &&r, F pred) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> yes, no;
  for (auto &&x : r)
    (pred(x) ? yes : no).push_back(std::forward<decltype(x)>(x));
  return std::pair{std::move(yes), std::move(no)};
}

// One pass: the head is collected until the predicate fails, the rest follows.
// (A find_if_not + second loop would re-iterate the range, which input ranges
// cannot survive.)
template <std::ranges::range R, class F> auto span(R &&r, F pred) {
  using T = std::ranges::range_value_t<R>;
  std::vector<T> head, rest;
  bool in_head = true;
  for (auto &&x : r) {
    if (in_head && !pred(x))
      in_head = false;
    (in_head ? head : rest).push_back(std::forward<decltype(x)>(x));
  }
  return std::pair{std::move(head), std::move(rest)};
}

// --- curried (pipe-friendly) forms ----------------------------------------
// Enable point-free collection pipelines:
//   fp::out(fp::into(v) | fp::filter(fp::gt(0)) | fp::map(fp::plus(1)));
// Each takes fewer args than the eager form and returns `range -> range`.

template <class F> auto map(F f) {
  return [f = std::move(f)](auto &&r) {
    return fp::map(std::forward<decltype(r)>(r), f);
  };
}
template <class F> auto filter(F pred) {
  return [pred = std::move(pred)](auto &&r) {
    return fp::filter(std::forward<decltype(r)>(r), pred);
  };
}
template <class F> auto take_while(F pred) {
  return [pred = std::move(pred)](auto &&r) {
    return fp::take_while(std::forward<decltype(r)>(r), pred);
  };
}
template <class F> auto drop_while(F pred) {
  return [pred = std::move(pred)](auto &&r) {
    return fp::drop_while(std::forward<decltype(r)>(r), pred);
  };
}
template <class F> auto flat_map(F f) {
  return [f = std::move(f)](auto &&r) {
    return fp::flat_map(std::forward<decltype(r)>(r), f);
  };
}
template <class F> auto filter_map(F f) {
  return [f = std::move(f)](auto &&r) {
    return fp::filter_map(std::forward<decltype(r)>(r), f);
  };
}
template <class F> auto group_by(F key_fn) {
  return [key_fn = std::move(key_fn)](auto &&r) {
    return fp::group_by(std::forward<decltype(r)>(r), key_fn);
  };
}
template <class N> auto take(N n) {
  return [n](auto &&r) { return fp::take(std::forward<decltype(r)>(r), n); };
}
template <class N> auto drop(N n) {
  return [n](auto &&r) { return fp::drop(std::forward<decltype(r)>(r), n); };
}
template <class N> auto chunk(N n) {
  return [n](auto &&r) { return fp::chunk(std::forward<decltype(r)>(r), n); };
}
template <class B, class F> auto zip_with(B b, F f) {
  return [b = std::move(b), f = std::move(f)](auto &&r) {
    return fp::zip_with(std::forward<decltype(r)>(r), b, f);
  };
}
template <class T, class F> auto fold_left(T init, F op) {
  return [init = std::move(init), op = std::move(op)](auto &&r) mutable {
    return fp::fold_left(std::forward<decltype(r)>(r), std::move(init), op);
  };
}
template <class T, class F> auto fold_right(T init, F op) {
  return [init = std::move(init), op = std::move(op)](auto &&r) mutable {
    return fp::fold_right(std::forward<decltype(r)>(r), std::move(init), op);
  };
}
template <class T, class F> auto scan(T init, F op) {
  return [init = std::move(init), op = std::move(op)](auto &&r) mutable {
    return fp::scan(std::forward<decltype(r)>(r), std::move(init), op);
  };
}
template <class F> auto sort_by(F key_fn) {
  return [key_fn = std::move(key_fn)](auto &&r) {
    return fp::sort_by(std::forward<decltype(r)>(r), key_fn);
  };
}
template <class F> auto sort_by_cached(F key_fn) {
  return [key_fn = std::move(key_fn)](auto &&r) {
    return fp::sort_by_cached(std::forward<decltype(r)>(r), key_fn);
  };
}
template <class F> auto partition(F pred) {
  return [pred = std::move(pred)](auto &&r) {
    return fp::partition(std::forward<decltype(r)>(r), pred);
  };
}
template <class F> auto span(F pred) {
  return [pred = std::move(pred)](auto &&r) {
    return fp::span(std::forward<decltype(r)>(r), pred);
  };
}
inline auto unique() {
  return [](auto &&r) { return fp::unique(std::forward<decltype(r)>(r)); };
}
inline auto sort() {
  return [](auto &&r) { return fp::sort(std::forward<decltype(r)>(r)); };
}
inline auto reverse() {
  return [](auto &&r) {
    using T = std::ranges::range_value_t<std::decay_t<decltype(r)>>;
    std::vector<T> out(r.begin(), r.end());
    std::reverse(out.begin(), out.end());
    return out;
  };
}
inline auto enumerate() {
  return [](auto &&r) { return fp::enumerate(std::forward<decltype(r)>(r)); };
}
} // namespace fp

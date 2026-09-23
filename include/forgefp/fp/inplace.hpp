#pragma once
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp {

// --- iteration --------------------------------------------------------------
// Plain loops over any range. No allocation, no type erasure, no bounds
// checks. `f` receives the element by reference, so mutation is allowed.

template <std::ranges::range R, class F> void for_each(R &&r, F f) {
  for (auto &&x : r)
    f(x);
}

template <std::ranges::range R, class F> void for_each_index(R &&r, F f) {
  std::size_t i = 0;
  for (auto &&x : r)
    f(i++, x);
}

template <std::ranges::range R, class F> void transform_inplace(R &r, F f) {
  for (auto &&x : r)
    x = f(x);
}

// Truncates to the shorter input, like fp::zip.
template <std::ranges::range A, std::ranges::range B, class F>
void zip_for_each(A &&a, B &&b, F f) {
  auto ia = std::ranges::begin(a);
  auto ea = std::ranges::end(a);
  auto ib = std::ranges::begin(b);
  auto eb = std::ranges::end(b);
  for (; ia != ea && ib != eb; ++ia, ++ib)
    f(*ia, *ib);
}

template <std::ranges::range A, std::ranges::range B, std::ranges::range C,
          class F>
void zip3_for_each(A &&a, B &&b, C &&c, F f) {
  auto ia = std::ranges::begin(a);
  auto ea = std::ranges::end(a);
  auto ib = std::ranges::begin(b);
  auto eb = std::ranges::end(b);
  auto ic = std::ranges::begin(c);
  auto ec = std::ranges::end(c);
  for (; ia != ea && ib != eb && ic != ec; ++ia, ++ib, ++ic)
    f(*ia, *ib, *ic);
}

template <std::ranges::range R, class T> void fill(R &r, T const &value) {
  std::ranges::fill(r, value);
}

// --- transform into a destination (no result allocation) --------------------

// Truncates to the shorter input, like fp::zip.
template <std::ranges::range Src, std::ranges::range Dst, class F>
void map_to(Src &&src, Dst &dst, F f) {
  auto is = std::ranges::begin(src);
  auto es = std::ranges::end(src);
  auto id = std::ranges::begin(dst);
  auto ed = std::ranges::end(dst);
  for (; is != es && id != ed; ++is, ++id)
    *id = f(*is);
}

template <std::ranges::range A, std::ranges::range B, class F>
void zip_transform_inplace(A &a, B const &b, F f) {
  auto ia = std::ranges::begin(a);
  auto ea = std::ranges::end(a);
  auto ib = std::ranges::begin(b);
  auto eb = std::ranges::end(b);
  for (; ia != ea && ib != eb; ++ia, ++ib)
    *ia = f(*ia, *ib);
}

// --- in-place algorithms (no copy) ------------------------------------------

template <std::ranges::bidirectional_range R> void reverse_inplace(R &r) {
  std::ranges::reverse(r);
}

template <std::ranges::random_access_range R> void sort_inplace(R &r) {
  std::ranges::sort(r);
}

template <std::ranges::random_access_range R, class Key>
void sort_by_inplace(R &r, Key key) {
  std::ranges::sort(r, [&key](auto const &a, auto const &b) {
    return key(a) < key(b);
  });
}

template <std::ranges::random_access_range R, class Key>
void stable_sort_by_inplace(R &r, Key key) {
  std::stable_sort(std::ranges::begin(r), std::ranges::end(r),
                   [&key](auto const &a, auto const &b) {
                     return key(a) < key(b);
                   });
}

// Sort with the keys computed once; costs one scratch buffer and wins when the
// key is expensive (see vec.hpp's sort_by_cached for the measurements).
template <std::ranges::random_access_range R, class Key>
void sort_by_cached_inplace(R &r, Key key) {
  using T = std::ranges::range_value_t<R>;
  using K = std::invoke_result_t<Key, T>;
  std::vector<std::pair<K, T>> keyed;
  keyed.reserve(static_cast<std::size_t>(std::ranges::size(r)));
  for (auto &x : r)
    keyed.emplace_back(key(x), std::move(x));
  std::ranges::stable_sort(keyed, {}, &std::pair<K, T>::first);
  auto it = std::ranges::begin(r);
  for (auto &kv : keyed)
    *it++ = std::move(kv.second);
}

// Containers that can shrink (vector, string, ...).
template <class R>
concept erasable_range =
    std::ranges::range<R> &&
    requires(R &r, std::ranges::iterator_t<R> it) { r.erase(it, it); };

template <erasable_range R> void unique_inplace(R &r) {
  r.erase(std::unique(std::ranges::begin(r), std::ranges::end(r)),
          std::ranges::end(r));
}

template <erasable_range R, class Pred> void remove_if_inplace(R &r, Pred pred) {
  r.erase(std::remove_if(std::ranges::begin(r), std::ranges::end(r), pred),
          std::ranges::end(r));
}

} // namespace fp

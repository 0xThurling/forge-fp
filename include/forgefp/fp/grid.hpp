#pragma once

#include "forgefp/fp/either.hpp"
#include <cstddef>
#include <type_traits>
#include <vector>

namespace fp {
template <class T, class F>
auto map2d(std::vector<std::vector<T>> const &g, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<std::vector<R>> out;
  out.reserve(g.size());
  for (auto const &row : g)
    out.push_back(fp::map(row, f));
  return out;
}

template <class T>
std::vector<std::vector<T>> transpose(std::vector<std::vector<T>> const &g) {
  if (g.empty())
    return {};
  size_t rows = g.size(), cols = g[0].size();
  std::vector<std::vector<T>> out(cols, std::vector<T>(rows));
  for (size_t i = 0; i < rows; ++i)
    for (size_t j = 0; j < cols; ++j)
      out[j][i] = g[i][j];
  return out;
}

template <class T>
std::vector<T> flatten(std::vector<std::vector<T>> const &g) {
  std::vector<T> out;
  size_t n = 0;
  for (auto const &row : g)
    n += row.size();
  out.reserve(n);
  for (auto const &row : g)
    out.insert(out.end(), row.begin(), row.end());
  return out;
}

template <class F> void for_each_index(size_t rows, size_t cols, F f) {
  for (size_t i = 0; i < rows; ++i)
    for (size_t j = 0; j < cols; ++j)
      f(i, j);
}

template <class T, class F> std::vector<T> tabulate(size_t n, F f) {
  std::vector<T> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.push_back(f(i));
  return out;
}

template <class A, class B>
std::vector<std::pair<A, B>> cartesian_product(std::vector<A> const &as,
                                               std::vector<B> const &bs) {
  std::vector<std::pair<A, B>> out;
  out.reserve(as.size() * bs.size());
  for (auto const &a : as)
    for (auto const &b : bs)
      out.emplace_back(a, b);
  return out;
}

template <class T, class F>
auto map3d(std::vector<std::vector<std::vector<T>>> const &g, F f) {
  std::vector<std::vector<std::vector<std::invoke_result_t<F, T>>>> out;
  out.reserve(g.size());
  for (auto const &plane : g) {
    std::vector<std::vector<std::invoke_result_t<F, T>>> p;
    p.reserve(plane.size());
    for (auto const &row : plane)
      p.push_back(fp::map(row, f));
    out.push_back(std::move(p));
  }
  return out;
}
} // namespace fp

#pragma once

#include "either.hpp"
#include <cstddef>
#include <type_traits>
#include <vector>

namespace fp {
template <class T, class F>
auto map2d(std::vector<std::vector<T>> const &g, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<std::vector<R>> out;
  out.reserve(g.size());
  // Self-contained: `grid.hpp` is included before the range helpers, so it
  // cannot lean on `fp::map` here.
  for (auto const &row : g) {
    std::vector<R> mapped;
    mapped.reserve(row.size());
    for (auto const &x : row)
      mapped.push_back(f(x));
    out.push_back(std::move(mapped));
  }
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

template <class F>
auto tabulate(size_t n, F f) -> std::vector<std::invoke_result_t<F, size_t>> {
  using T = std::invoke_result_t<F, size_t>;
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
  using R = std::invoke_result_t<F, T>;
  std::vector<std::vector<std::vector<R>>> out;
  out.reserve(g.size());
  for (auto const &plane : g) {
    std::vector<std::vector<R>> p;
    p.reserve(plane.size());
    for (auto const &row : plane) {
      std::vector<R> mapped;
      mapped.reserve(row.size());
      for (auto const &x : row)
        mapped.push_back(f(x));
      p.push_back(std::move(mapped));
    }
    out.push_back(std::move(p));
  }
  return out;
}
// --- 2-D helpers ------------------------------------------------------------

// f(i, j, x) -> y: index-aware mapping for bias add, masks, positional terms.
template <class T, class F>
auto map2d_indexed(std::vector<std::vector<T>> const &g, F f) {
  using R = std::invoke_result_t<F, std::size_t, std::size_t, T>;
  std::vector<std::vector<R>> out;
  out.reserve(g.size());
  for (std::size_t i = 0; i < g.size(); ++i) {
    std::vector<R> row;
    row.reserve(g[i].size());
    for (std::size_t j = 0; j < g[i].size(); ++j)
      row.push_back(f(i, j, g[i][j]));
    out.push_back(std::move(row));
  }
  return out;
}

// In-place map2d.
template <class T, class F>
void map2d_inplace(std::vector<std::vector<T>> &g, F f) {
  for (auto &row : g)
    for (auto &x : row)
      x = f(x);
}

// Column j as a vector (copies; columns are strided).
template <class T>
std::vector<T> column(std::vector<std::vector<T>> const &g, std::size_t j) {
  std::vector<T> out;
  out.reserve(g.size());
  for (auto const &row : g)
    out.push_back(row[j]);
  return out;
}

// Non-overlapping kh x kw patches in row-major order. Each patch is a kh x kw
// grid; incomplete edges are dropped.
template <class T>
auto windows2d(std::vector<std::vector<T>> const &g, std::size_t kh,
               std::size_t kw) {
  std::vector<std::vector<std::vector<T>>> out;
  if (g.empty() || kh == 0 || kw == 0)
    return out;
  const std::size_t height = g.size();
  const std::size_t width = g[0].size();
  for (std::size_t i = 0; i + kh <= height; i += kh) {
    for (std::size_t j = 0; j + kw <= width; j += kw) {
      std::vector<std::vector<T>> patch(kh, std::vector<T>(kw));
      for (std::size_t r = 0; r < kh; ++r)
        for (std::size_t c = 0; c < kw; ++c)
          patch[r][c] = g[i + r][j + c];
      out.push_back(std::move(patch));
    }
  }
  return out;
}

} // namespace fp

#pragma once
#include "grid.hpp"
#include "result.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace fp {

// Dense linear algebra over nested ranges (`std::vector<std::vector<T>>`) and
// spans. Hot kernels live here; shape preconditions are documented and checked
// with `assert` in debug builds. Domain code validates shapes at its boundary
// and then calls these functions.

// --- products ---------------------------------------------------------------

// (m x k) * (k x n) -> (m x n). Loop order i-k-j keeps both `b`'s row and the
// output row contiguous.
template <class T>
std::vector<std::vector<T>> matmul(std::vector<std::vector<T>> const &a,
                                   std::vector<std::vector<T>> const &b) {
  assert(!a.empty() && !b.empty());
  const std::size_t m = a.size();
  const std::size_t k = a[0].size();
  const std::size_t n = b[0].size();
  assert(k == b.size());

  std::vector<std::vector<T>> out(m, std::vector<T>(n, T{}));
  for (std::size_t i = 0; i < m; ++i) {
    for (std::size_t p = 0; p < k; ++p) {
      const T a_ip = a[i][p];
      for (std::size_t j = 0; j < n; ++j)
        out[i][j] += a_ip * b[p][j];
    }
  }
  return out;
}

// Flat-buffer product: `a` is m x k row-major, `b` is k x n row-major, both
// contiguous (std::span accepts arrays, vector, Buffer<T>, slices). The nested
// form above is convenient; this one removes a pointer chase per element for
// data that already lives in one buffer.
template <class T>
std::vector<T> matmul(std::span<T const> a, std::size_t m, std::size_t k,
                      std::span<T const> b, std::size_t n) {
  assert(a.size() >= m * k);
  assert(b.size() >= k * n);
  std::vector<T> out(m * n, T{});
  for (std::size_t i = 0; i < m; ++i)
    for (std::size_t p = 0; p < k; ++p) {
      const T a_ip = a[i * k + p];
      for (std::size_t j = 0; j < n; ++j)
        out[i * n + j] += a_ip * b[p * n + j];
    }
  return out;
}

// Batched (B x m x k) * (B x k x n) -> (B x m x n).
template <class T>
std::vector<std::vector<std::vector<T>>>
batched_matmul(std::vector<std::vector<std::vector<T>>> const &a,
               std::vector<std::vector<std::vector<T>>> const &b) {
  assert(a.size() == b.size());
  std::vector<std::vector<std::vector<T>>> out;
  out.reserve(a.size());
  for (std::size_t i = 0; i < a.size(); ++i)
    out.push_back(matmul(a[i], b[i]));
  return out;
}

// (m x k) * (k) -> (m)
template <class T>
std::vector<T> matvec(std::vector<std::vector<T>> const &a,
                      std::vector<T> const &x) {
  assert(!a.empty());
  assert(a[0].size() == x.size());
  std::vector<T> out(a.size(), T{});
  for (std::size_t i = 0; i < a.size(); ++i) {
    T acc = T{};
    for (std::size_t j = 0; j < x.size(); ++j)
      acc += a[i][j] * x[j];
    out[i] = acc;
  }
  return out;
}

// (m) x (n) -> (m x n)
template <class T>
std::vector<std::vector<T>> outer(std::vector<T> const &a,
                                  std::vector<T> const &b) {
  std::vector<std::vector<T>> out(a.size(), std::vector<T>(b.size(), T{}));
  for (std::size_t i = 0; i < a.size(); ++i)
    for (std::size_t j = 0; j < b.size(); ++j)
      out[i][j] = a[i] * b[j];
  return out;
}

// --- solve ------------------------------------------------------------------

// Gaussian elimination with partial pivoting. Fails on singular systems.
template <class T>
[[nodiscard]] Result<std::vector<T>> solve(std::vector<std::vector<T>> const &a,
                             std::vector<T> const &b) {
  const std::size_t n = a.size();
  if (n == 0 || b.size() != n || a[0].size() != n)
    return err<std::vector<T>>("solve: system is not n x n");

  std::vector<std::vector<T>> m = a;
  std::vector<T> rhs = b;

  for (std::size_t col = 0; col < n; ++col) {
    std::size_t pivot = col;
    T best = std::abs(m[col][col]);
    for (std::size_t r = col + 1; r < n; ++r) {
      const T candidate = std::abs(m[r][col]);
      if (candidate > best) {
        best = candidate;
        pivot = r;
      }
    }
    if (best < static_cast<T>(1e-12))
      return err<std::vector<T>>("solve: singular matrix");

    if (pivot != col) {
      std::swap(m[col], m[pivot]);
      std::swap(rhs[col], rhs[pivot]);
    }

    const T diag = m[col][col];
    for (std::size_t r = col + 1; r < n; ++r) {
      const T factor = m[r][col] / diag;
      if (factor == T{})
        continue;
      for (std::size_t c = col; c < n; ++c)
        m[r][c] -= factor * m[col][c];
      rhs[r] -= factor * rhs[col];
    }
  }

  std::vector<T> x(n);
  for (std::size_t i = n; i-- > 0;) {
    T acc = rhs[i];
    for (std::size_t j = i + 1; j < n; ++j)
      acc -= m[i][j] * x[j];
    x[i] = acc / m[i][i];
  }
  return ok(std::move(x));
}

// --- elementwise ------------------------------------------------------------

template <class T>
std::vector<T> hadamard(std::vector<T> const &a, std::vector<T> const &b) {
  assert(a.size() == b.size());
  std::vector<T> out(a.size());
  for (std::size_t i = 0; i < a.size(); ++i)
    out[i] = a[i] * b[i];
  return out;
}

template <class T> std::vector<T> scale(std::vector<T> const &v, T k) {
  std::vector<T> out(v.size());
  for (std::size_t i = 0; i < v.size(); ++i)
    out[i] = v[i] * k;
  return out;
}

// Adds `row` to every row of `g` (bias add).
template <class T>
std::vector<std::vector<T>>
add_row_broadcast(std::vector<std::vector<T>> const &g,
                  std::vector<T> const &row) {
  std::vector<std::vector<T>> out = g;
  for (auto &r : out)
    for (std::size_t j = 0; j < r.size(); ++j)
      r[j] += row[j];
  return out;
}

// --- reductions -------------------------------------------------------------

template <class T> T dot(std::span<T const> a, std::span<T const> b) {
  assert(a.size() == b.size());
  T acc = T{};
  for (std::size_t i = 0; i < a.size(); ++i)
    acc += a[i] * b[i];
  return acc;
}

template <class T> T norm_l1(std::span<T const> v) {
  T acc = T{};
  for (T x : v)
    acc += x < T{} ? -x : x;
  return acc;
}

template <class T> T norm_l2(std::span<T const> v) {
  T acc = T{};
  for (T x : v)
    acc += x * x;
  return std::sqrt(acc);
}

// First index of the maximum; empty -> nullopt. Ties keep the first index.
template <std::ranges::range R>
[[nodiscard]] std::optional<std::size_t> argmax(R const &r) {
  auto it = std::ranges::begin(r);
  auto end = std::ranges::end(r);
  if (it == end)
    return std::nullopt;

  auto best = *it;
  std::size_t best_index = 0;
  std::size_t i = 0;
  for (auto cur = it; cur != end; ++cur, ++i) {
    if (*cur > best) {
      best = *cur;
      best_index = i;
    }
  }
  return best_index;
}

template <std::ranges::range R>
[[nodiscard]] std::optional<std::size_t> argmin(R const &r) {
  auto it = std::ranges::begin(r);
  auto end = std::ranges::end(r);
  if (it == end)
    return std::nullopt;

  auto best = *it;
  std::size_t best_index = 0;
  std::size_t i = 0;
  for (auto cur = it; cur != end; ++cur, ++i) {
    if (*cur < best) {
      best = *cur;
      best_index = i;
    }
  }
  return best_index;
}

template <std::ranges::range R> double mean(R const &r) {
  assert(!std::ranges::empty(r));
  double sum = 0.0;
  std::size_t n = 0;
  for (auto const &x : r) {
    sum += static_cast<double>(x);
    ++n;
  }
  return sum / static_cast<double>(n);
}

// ddof = 0: population variance; ddof = 1: sample variance.
template <std::ranges::range R>
double variance(R const &r, std::size_t ddof = 0) {
  const std::size_t n = std::ranges::size(r);
  assert(n > ddof);
  const double m = mean(r);
  double acc = 0.0;
  for (auto const &x : r) {
    const double d = static_cast<double>(x) - m;
    acc += d * d;
  }
  return acc / static_cast<double>(n - ddof);
}

// --- grid reductions --------------------------------------------------------

template <class T>
std::vector<T> row_sums(std::vector<std::vector<T>> const &g) {
  std::vector<T> out;
  out.reserve(g.size());
  for (auto const &row : g) {
    T acc = T{};
    for (T x : row)
      acc += x;
    out.push_back(acc);
  }
  return out;
}

template <class T>
std::vector<T> col_sums(std::vector<std::vector<T>> const &g) {
  if (g.empty())
    return {};
  std::vector<T> out(g[0].size(), T{});
  for (auto const &row : g)
    for (std::size_t j = 0; j < row.size(); ++j)
      out[j] += row[j];
  return out;
}

template <class T>
std::vector<T> row_means(std::vector<std::vector<T>> const &g) {
  std::vector<T> out;
  out.reserve(g.size());
  for (auto const &row : g) {
    T acc = T{};
    for (T x : row)
      acc += x;
    out.push_back(row.empty() ? T{} : acc / static_cast<T>(row.size()));
  }
  return out;
}

template <class T>
std::vector<T> col_means(std::vector<std::vector<T>> const &g) {
  if (g.empty())
    return {};
  std::vector<T> out = col_sums(g);
  const T n = static_cast<T>(g.size());
  for (auto &x : out)
    x /= n;
  return out;
}

template <class T>
std::vector<std::size_t> argmax_rows(std::vector<std::vector<T>> const &g) {
  std::vector<std::size_t> out;
  out.reserve(g.size());
  for (auto const &row : g)
    out.push_back(argmax(row).value_or(0));
  return out;
}

template <class T>
std::vector<std::size_t> argmin_rows(std::vector<std::vector<T>> const &g) {
  std::vector<std::size_t> out;
  out.reserve(g.size());
  for (auto const &row : g)
    out.push_back(argmin(row).value_or(0));
  return out;
}

} // namespace fp

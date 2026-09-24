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

// The kernels below hand the compiler two things it cannot infer: that the
// output row and the `b` row do not overlap, and that reductions may use
// several independent accumulators. Without them every hot loop stays scalar.
#ifndef FP_RESTRICT
#if defined(_MSC_VER)
#define FP_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define FP_RESTRICT __restrict__
#else
#define FP_RESTRICT
#endif
#endif

namespace detail {

// j-tile width for the matmul micro-kernel: one accumulator row, sized so the
// accumulators stay in vector registers (8 doubles = 2 AVX2 registers).
inline constexpr std::size_t matmul_tile = 8;

// One output row of `A * B`: for each j-tile, accumulate over all k in
// registers, then store once. `b_row(p)` yields a pointer to row p of B.
template <class T, class BRow>
void matmul_row(T const *FP_RESTRICT a_row, std::size_t k, std::size_t n,
                T *FP_RESTRICT out_row, BRow &&b_row) {
  std::size_t j = 0;
  for (; j + matmul_tile <= n; j += matmul_tile) {
    T acc[matmul_tile] = {};
    for (std::size_t p = 0; p < k; ++p) {
      const T a_ip = a_row[p];
      const T *FP_RESTRICT b = b_row(p) + j;
      for (std::size_t t = 0; t < matmul_tile; ++t)
        acc[t] += a_ip * b[t];
    }
    for (std::size_t t = 0; t < matmul_tile; ++t)
      out_row[j + t] = acc[t];
  }
  for (; j < n; ++j) {
    T acc = T{};
    for (std::size_t p = 0; p < k; ++p)
      acc += a_row[p] * b_row(p)[j];
    out_row[j] = acc;
  }
}

// Four output rows per pass over B: B's rows are read once per four rows of A
// instead of once per row, which is the difference between an L3- and an
// L2-bound kernel once B no longer fits in L2 (from roughly 256x256 doubles).
template <class T, class BRow>
void matmul_4row(T const *FP_RESTRICT const *a_rows, std::size_t k,
                 std::size_t n, T *FP_RESTRICT const *out_rows, BRow &&b_row) {
  // 4x4 tile: 16 accumulators fit in vector registers (a 4x8 tile spills), and
  // B's rows are read once per four output rows.
  constexpr std::size_t tile = 4;
  std::size_t j = 0;
  for (; j + tile <= n; j += tile) {
    T r0[tile] = {}, r1[tile] = {}, r2[tile] = {}, r3[tile] = {};
    for (std::size_t p = 0; p < k; ++p) {
      const T *FP_RESTRICT b = b_row(p) + j;
      const T a0 = a_rows[0][p];
      const T a1 = a_rows[1][p];
      const T a2 = a_rows[2][p];
      const T a3 = a_rows[3][p];
      for (std::size_t t = 0; t < tile; ++t) {
        r0[t] += a0 * b[t];
        r1[t] += a1 * b[t];
        r2[t] += a2 * b[t];
        r3[t] += a3 * b[t];
      }
    }
    for (std::size_t t = 0; t < tile; ++t) {
      out_rows[0][j + t] = r0[t];
      out_rows[1][j + t] = r1[t];
      out_rows[2][j + t] = r2[t];
      out_rows[3][j + t] = r3[t];
    }
  }
  for (; j < n; ++j) { // ragged tail columns
    T c0{}, c1{}, c2{}, c3{};
    for (std::size_t p = 0; p < k; ++p) {
      const T bp = b_row(p)[j];
      c0 += a_rows[0][p] * bp;
      c1 += a_rows[1][p] * bp;
      c2 += a_rows[2][p] * bp;
      c3 += a_rows[3][p] * bp;
    }
    out_rows[0][j] = c0;
    out_rows[1][j] = c1;
    out_rows[2][j] = c2;
    out_rows[3][j] = c3;
  }
}

} // namespace detail

// `transpose` lives in grid.hpp (the canonical home for 2-D shape helpers) and
// is included here, so `#include <fp/linalg.hpp>` is enough to use it.

// --- products ---------------------------------------------------------------

// (m x k) * (k x n) -> (m x n). Loop order i-k-j keeps both `b`'s row and the
// output row contiguous; the j-tile keeps the accumulator in registers, so the
// output row is written once per k instead of read-modify-written k times.
template <class T>
std::vector<std::vector<T>> matmul(std::vector<std::vector<T>> const &a,
                                   std::vector<std::vector<T>> const &b) {
  assert(!a.empty() && !b.empty());
  const std::size_t m = a.size();
  const std::size_t k = a[0].size();
  const std::size_t n = b[0].size();
  assert(k == b.size());

  std::vector<std::vector<T>> out(m, std::vector<T>(n));
  auto b_row = [&b](std::size_t p) { return b[p].data(); };
  std::size_t i = 0;
  for (; i + 4 <= m; i += 4) {
    T const *a_rows[4] = {a[i].data(), a[i + 1].data(), a[i + 2].data(),
                          a[i + 3].data()};
    T *out_rows[4] = {out[i].data(), out[i + 1].data(), out[i + 2].data(),
                      out[i + 3].data()};
    detail::matmul_4row<T>(a_rows, k, n, out_rows, b_row);
  }
  for (; i < m; ++i)
    detail::matmul_row<T>(a[i].data(), k, n, out[i].data(), b_row);
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
  std::vector<T> out(m * n);
  auto b_row = [b, n](std::size_t p) { return b.data() + p * n; };
  std::size_t i = 0;
  for (; i + 4 <= m; i += 4) {
    T const *a_rows[4] = {a.data() + i * k, a.data() + (i + 1) * k,
                          a.data() + (i + 2) * k, a.data() + (i + 3) * k};
    T *out_rows[4] = {out.data() + i * n, out.data() + (i + 1) * n,
                      out.data() + (i + 2) * n, out.data() + (i + 3) * n};
    detail::matmul_4row<T>(a_rows, k, n, out_rows, b_row);
  }
  for (; i < m; ++i)
    detail::matmul_row<T>(a.data() + i * k, k, n, out.data() + i * n, b_row);
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
  const std::size_t n = x.size();
  std::vector<T> out(a.size(), T{});
  for (std::size_t i = 0; i < a.size(); ++i) {
    T const *FP_RESTRICT row = a[i].data();
    T const *FP_RESTRICT px = x.data();
    T a0{}, a1{}, a2{}, a3{};
    std::size_t j = 0;
    for (; j + 4 <= n; j += 4) {
      a0 += row[j] * px[j];
      a1 += row[j + 1] * px[j + 1];
      a2 += row[j + 2] * px[j + 2];
      a3 += row[j + 3] * px[j + 3];
    }
    T acc = (a0 + a1) + (a2 + a3);
    for (; j < n; ++j)
      acc += row[j] * px[j];
    out[i] = acc;
  }
  return out;
}

// (m) x (n) -> (m x n)
template <class T>
std::vector<std::vector<T>> outer(std::vector<T> const &a,
                                  std::vector<T> const &b) {
  std::vector<std::vector<T>> out(a.size(), std::vector<T>(b.size()));
  T const *FP_RESTRICT pb = b.data();
  const std::size_t n = b.size();
  for (std::size_t i = 0; i < a.size(); ++i) {
    T *FP_RESTRICT row = out[i].data();
    const T ai = a[i];
    for (std::size_t j = 0; j < n; ++j)
      row[j] = ai * pb[j];
  }
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
  const std::size_t n = a.size();
  std::vector<T> out(n);
  T const *FP_RESTRICT pa = a.data();
  T const *FP_RESTRICT pb = b.data();
  T *FP_RESTRICT po = out.data();
  for (std::size_t i = 0; i < n; ++i)
    po[i] = pa[i] * pb[i];
  return out;
}

template <class T> std::vector<T> scale(std::vector<T> const &v, T k) {
  const std::size_t n = v.size();
  std::vector<T> out(n);
  T const *FP_RESTRICT pv = v.data();
  T *FP_RESTRICT po = out.data();
  for (std::size_t i = 0; i < n; ++i)
    po[i] = pv[i] * k;
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

// Reductions run with four independent accumulators: a single accumulator is
// limited by FP-add latency (one element per ~4 cycles), four of them reach
// one per cycle even before the vectorizer packs them.
template <class T> T dot(std::span<T const> a, std::span<T const> b) {
  assert(a.size() == b.size());
  T const *FP_RESTRICT pa = a.data();
  T const *FP_RESTRICT pb = b.data();
  const std::size_t n = a.size();
  T a0{}, a1{}, a2{}, a3{};
  std::size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    a0 += pa[i] * pb[i];
    a1 += pa[i + 1] * pb[i + 1];
    a2 += pa[i + 2] * pb[i + 2];
    a3 += pa[i + 3] * pb[i + 3];
  }
  T acc = (a0 + a1) + (a2 + a3);
  for (; i < n; ++i)
    acc += pa[i] * pb[i];
  return acc;
}

template <class T> T norm_l1(std::span<T const> v) {
  const std::size_t n = v.size();
  T a0{}, a1{}, a2{}, a3{};
  std::size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    const T x0 = v[i], x1 = v[i + 1], x2 = v[i + 2], x3 = v[i + 3];
    a0 += x0 < T{} ? -x0 : x0;
    a1 += x1 < T{} ? -x1 : x1;
    a2 += x2 < T{} ? -x2 : x2;
    a3 += x3 < T{} ? -x3 : x3;
  }
  T acc = (a0 + a1) + (a2 + a3);
  for (; i < n; ++i)
    acc += v[i] < T{} ? -v[i] : v[i];
  return acc;
}

template <class T> T norm_l2(std::span<T const> v) {
  const std::size_t n = v.size();
  T a0{}, a1{}, a2{}, a3{};
  std::size_t i = 0;
  for (; i + 4 <= n; i += 4) {
    a0 += v[i] * v[i];
    a1 += v[i + 1] * v[i + 1];
    a2 += v[i + 2] * v[i + 2];
    a3 += v[i + 3] * v[i + 3];
  }
  T acc = (a0 + a1) + (a2 + a3);
  for (; i < n; ++i)
    acc += v[i] * v[i];
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
  double a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
  std::size_t n = 0;
  auto it = std::ranges::begin(r);
  const auto end = std::ranges::end(r);
  while (it != end) { // four independent chains, ragged end handled by breaks
    a0 += static_cast<double>(*it);
    ++it;
    ++n;
    if (it == end)
      break;
    a1 += static_cast<double>(*it);
    ++it;
    ++n;
    if (it == end)
      break;
    a2 += static_cast<double>(*it);
    ++it;
    ++n;
    if (it == end)
      break;
    a3 += static_cast<double>(*it);
    ++it;
    ++n;
  }
  return ((a0 + a1) + (a2 + a3)) / static_cast<double>(n);
}

// ddof = 0: population variance; ddof = 1: sample variance.
template <std::ranges::range R>
double variance(R const &r, std::size_t ddof = 0) {
  const std::size_t n = std::ranges::size(r);
  assert(n > ddof);
  const double m = mean(r);
  double a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
  auto it = std::ranges::begin(r);
  const auto end = std::ranges::end(r);
  while (it != end) { // four independent chains (a switch per element is slower)
    double d = static_cast<double>(*it) - m;
    a0 += d * d;
    ++it;
    if (it == end)
      break;
    d = static_cast<double>(*it) - m;
    a1 += d * d;
    ++it;
    if (it == end)
      break;
    d = static_cast<double>(*it) - m;
    a2 += d * d;
    ++it;
    if (it == end)
      break;
    d = static_cast<double>(*it) - m;
    a3 += d * d;
    ++it;
  }
  return ((a0 + a1) + (a2 + a3)) / static_cast<double>(n - ddof);
}

// --- grid reductions --------------------------------------------------------

template <class T>
std::vector<T> row_sums(std::vector<std::vector<T>> const &g) {
  std::vector<T> out;
  out.reserve(g.size());
  for (auto const &row : g) {
    T const *FP_RESTRICT p = row.data();
    const std::size_t n = row.size();
    T a0{}, a1{}, a2{}, a3{};
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
      a0 += p[i];
      a1 += p[i + 1];
      a2 += p[i + 2];
      a3 += p[i + 3];
    }
    T acc = (a0 + a1) + (a2 + a3);
    for (; i < n; ++i)
      acc += p[i];
    out.push_back(acc);
  }
  return out;
}

template <class T>
std::vector<T> col_sums(std::vector<std::vector<T>> const &g) {
  if (g.empty())
    return {};
  const std::size_t n = g[0].size();
  std::vector<T> out(n, T{});
  T *FP_RESTRICT po = out.data();
  for (auto const &row : g) {
    T const *FP_RESTRICT p = row.data();
    for (std::size_t j = 0; j < n; ++j)
      po[j] += p[j];
  }
  return out;
}

template <class T>
std::vector<T> row_means(std::vector<std::vector<T>> const &g) {
  std::vector<T> out;
  out.reserve(g.size());
  for (auto const &row : g) {
    T const *FP_RESTRICT p = row.data();
    const std::size_t n = row.size();
    T a0{}, a1{}, a2{}, a3{};
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
      a0 += p[i];
      a1 += p[i + 1];
      a2 += p[i + 2];
      a3 += p[i + 3];
    }
    T acc = (a0 + a1) + (a2 + a3);
    for (; i < n; ++i)
      acc += p[i];
    out.push_back(n == 0 ? T{} : acc / static_cast<T>(n));
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

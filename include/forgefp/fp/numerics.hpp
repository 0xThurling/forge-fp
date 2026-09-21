#pragma once
#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <ranges>
#include <vector>

namespace fp {

// --- stable nonlinearities --------------------------------------------------

inline double sigmoid(double z) {
  if (z >= 0.0) {
    const double e = std::exp(-z);
    return 1.0 / (1.0 + e);
  }
  const double e = std::exp(z);
  return e / (1.0 + e);
}

inline double relu(double z) { return z > 0.0 ? z : 0.0; }

template <class T> constexpr T clamp(T value, T lo, T hi) {
  return value < lo ? lo : (hi < value ? hi : value);
}

// --- stable reductions ------------------------------------------------------

template <std::ranges::range R> double logsumexp(R const &r) {
  auto it = std::ranges::begin(r);
  auto end = std::ranges::end(r);
  if (it == end)
    return -std::numeric_limits<double>::infinity();

  double m = -std::numeric_limits<double>::infinity();
  for (auto i = it; i != end; ++i)
    m = std::max(m, static_cast<double>(*i));

  double sum = 0.0;
  for (auto i = it; i != end; ++i)
    sum += std::exp(static_cast<double>(*i) - m);

  return m + std::log(sum);
}

// Subtracts the max before exp, so large logits cannot overflow.
template <std::ranges::range R> std::vector<double> softmax(R const &r) {
  std::vector<double> out;
  auto it = std::ranges::begin(r);
  auto end = std::ranges::end(r);
  if (it == end)
    return out;
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r));

  double m = -std::numeric_limits<double>::infinity();
  for (auto i = it; i != end; ++i)
    m = std::max(m, static_cast<double>(*i));

  double sum = 0.0;
  for (auto i = it; i != end; ++i) {
    const double e = std::exp(static_cast<double>(*i) - m);
    out.push_back(e);
    sum += e;
  }

  if (sum > 0.0)
    for (auto &e : out)
      e /= sum;
  return out;
}

// Computed as x - logsumexp(x), never as log(softmax(x)).
template <std::ranges::range R> std::vector<double> log_softmax(R const &r) {
  const double lse = logsumexp(r);
  std::vector<double> out;
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r));
  for (auto const &x : r)
    out.push_back(static_cast<double>(x) - lse);
  return out;
}

// In-place row-wise softmax (no allocation). Floating point only.
template <std::floating_point T>
void softmax_rows(std::vector<std::vector<T>> &g) {
  for (auto &row : g) {
    if (row.empty())
      continue;

    T m = row[0];
    for (T x : row)
      m = std::max(m, x);

    T sum = T{};
    for (T &x : row) {
      x = static_cast<T>(std::exp(static_cast<double>(x) -
                                  static_cast<double>(m)));
      sum += x;
    }

    if (sum > T{})
      for (T &x : row)
        x /= sum;
  }
}

// --- generation -------------------------------------------------------------

// n points from `from` to `to`, both endpoints included.
inline std::vector<double> linspace(double from, double to, std::size_t n) {
  std::vector<double> out;
  if (n == 0)
    return out;
  out.reserve(n);
  if (n == 1) {
    out.push_back(from);
    return out;
  }
  const double step = (to - from) / static_cast<double>(n - 1);
  for (std::size_t i = 0; i < n; ++i)
    out.push_back(from + step * static_cast<double>(i));
  out.back() = to;
  return out;
}

// Half-open: [from, to) with the given step.
inline std::vector<double> arange(double from, double to, double step = 1.0) {
  std::vector<double> out;
  if (step == 0.0)
    return out;
  if (step > 0.0) {
    for (double x = from; x < to; x += step)
      out.push_back(x);
  } else {
    for (double x = from; x > to; x += step)
      out.push_back(x);
  }
  return out;
}

// --- numeric predicates and derivatives -------------------------------------

inline bool approx_equal(double a, double b, double eps = 1e-9) {
  return std::abs(a - b) <= eps * (1.0 + std::max(std::abs(a), std::abs(b)));
}

inline bool is_finite(double x) { return std::isfinite(x); }

inline double nan_to_num(double x, double replacement = 0.0) {
  return std::isfinite(x) ? x : replacement;
}

// Central finite difference: the general derivative primitive.
template <class F> double central_difference(F f, double x, double h = 1e-6) {
  return (f(x + h) - f(x - h)) / (2.0 * h);
}

} // namespace fp

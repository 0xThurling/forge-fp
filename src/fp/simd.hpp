#pragma once
// std::experimental::simd is a GCC/Clang library extension; MSVC does not ship
// it. Gate the whole header so portable code can feature-test with
// `__has_include(<experimental/simd>)` and skip it.
#if !__has_include(<experimental/simd>)
#error "fp/simd.hpp requires <experimental/simd> (GCC/Clang); use scalar fp:: instead"
#else

#include "concurrent.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <experimental/simd>
#include <type_traits>
#include <vector>

namespace fp {
template <class T> using vec = std::experimental::native_simd<T>;

// Works on any contiguous run of T (vector, Buffer, span, array slice).
template <class T, class F> void map_inplace(std::span<T> data, F f) {
  using V = vec<T>;
  constexpr std::size_t width = V::size();
  const std::size_t n = data.size();
  T *p = data.data();
  std::size_t i = 0;
  for (; i + width <= n; i += width) {
    V chunk;
    chunk.copy_from(p + i, std::experimental::element_aligned);
    chunk = f(chunk);
    chunk.copy_to(p + i, std::experimental::element_aligned);
  }
  // Tail elements — broadcast the scalar into a vector so f (a SIMD lambda)
  // can be applied generically, then write back lane 0.
  for (; i < n; ++i) {
    V v_single(p[i]);
    v_single = f(v_single);
    p[i] = v_single[0];
  }
}

template <class T, class F> void map_inplace(std::vector<T> &data, F f) {
  map_inplace(std::span<T>(data), std::move(f));
}

template <class T> T reduce(std::vector<T> const &v, T init = T{}) {
  using Vec = fp::vec<T>;
  constexpr size_t lanes = Vec::size();
  Vec acc{init};
  size_t i = 0;
  for (; i + lanes <= v.size(); i += lanes) {
    Vec chunk;
    chunk.copy_from(&v[i], std::experimental::element_aligned);
    acc += chunk;
  }
  T total = init;
  for (size_t k = 0; k < lanes; ++k)
    total += acc[k];
  for (; i < v.size(); ++i)
    total += v[i];
  return total;
}

template <class T> T dot(std::vector<T> const &a, std::vector<T> const &b) {
  using Vec = fp::vec<T>;
  constexpr size_t lanes = Vec::size();
  Vec acc{};
  size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  for (; i + lanes <= n; i += lanes) {
    Vec x, y;
    x.copy_from(&a[i], std::experimental::element_aligned);
    y.copy_from(&b[i], std::experimental::element_aligned);
    acc += x * y;
  }
  T total = 0;
  for (size_t k = 0; k < lanes; ++k)
    total += acc[k];
  for (; i < n; ++i)
    total += a[i] * b[i];
  return total;
}

// y += a * x (the optimizer update primitive). Operates on the common prefix.
template <class T>
void axpy_inplace(std::vector<T> &y, T a, std::vector<T> const &x) {
  using V = vec<T>;
  constexpr std::size_t width = V::size();
  const std::size_t n = std::min(y.size(), x.size());
  T *py = y.data();
  T const *px = x.data();

  std::size_t i = 0;
  for (; i + width <= n; i += width) {
    V vy, vx;
    vy.copy_from(py + i, std::experimental::element_aligned);
    vx.copy_from(px + i, std::experimental::element_aligned);
    vy += V(a) * vx;
    vy.copy_to(py + i, std::experimental::element_aligned);
  }
  for (; i < n; ++i)
    py[i] += a * px[i];
}

template <class T, class F> auto map_to(std::vector<T> const &src, F f) {
  using VecT = fp::vec<T>;
  using VecR = std::invoke_result_t<F, VecT>;
  using R = typename VecR::value_type;
  constexpr size_t lanes = VecT::size();
  std::vector<R> out(src.size());
  size_t i = 0;
  for (; i + lanes <= src.size(); i += lanes) {
    VecT x;
    x.copy_from(&src[i], std::experimental::element_aligned);
    VecR y = f(x);
    y.copy_to(&out[i], std::experimental::element_aligned);
  }
  for (; i < src.size(); ++i)
    out[i] = f(VecT(src[i]))[0];
  return out;
}

template <class T, size_t N, class F>
void map_inplace_fixed(std::vector<T> &v, F f) {
  using Vec =
      std::experimental::simd<T, std::experimental::simd_abi::fixed_size<N>>;
  using Mask =
      std::experimental::simd<T, std::experimental::simd_abi::fixed_size<N>>;
  size_t i = 0;
  for (; i + N <= v.size(); i += N) {
    Vec x;
    x.copy_from(&v[i], std::experimental::element_aligned);
    f(x).copy_to(&v[i], std::experimental::element_aligned);
  }
  size_t tail = v.size() - i;
  if (tail == 0)
    return;
  Mask m([&](size_t k) { return k < tail; });
  Vec x;
  x.copy_from(&v[i], std::experimental::element_aligned, m);
  Vec y = f(x);
  std::experimental::where(m, y).copy_to(&v[i],
                                         std::experimental::element_aligned);
}

template <class T, size_t N>
using simd =
    std::experimental::simd<T, std::experimental::simd_abi::fixed_size<N>>;

template <class T, size_t N>
using simd_mask =
    std::experimental::simd_mask<T, std::experimental::simd_abi::fixed_size<N>>;

using vec2f = simd<float, 2>;
using vec4f = simd<float, 4>;
using vec8f = simd<float, 8>;
using vec16f = simd<float, 16>;

using vec2d = simd<double, 2>;
using vec4d = simd<double, 4>;
using vec8d = simd<double, 8>;

template <class T, class Math> void map_math(std::vector<T> &v, Math m) {
  map_inplace(v, m);
}

template <class T> void map_sqrt(std::vector<T> &v) {
  map_inplace(v, [](fp::vec<T> x) { return std::experimental::sqrt(x); });
}

template <class T> void map_exp(std::vector<T> &v) {
  map_inplace(v, [](fp::vec<T> x) { return std::experimental::exp(x); });
}

template <class T> void clamp_inplace(std::vector<T> &v, T lo, T hi) {
  using Vec = fp::vec<T>;
  map_inplace(v, [=](Vec x) {
    return std::experimental::min(std::experimental::max(x, Vec(lo)), Vec(hi));
  });
}

template <class T> void normalize(std::vector<T> &v) {
  T m = 0;
  for (T x : v)
    m = std::max(m, std::abs(x));
  if (m == T{})
    return;
  using Vec = fp::vec<T>;
  map_inplace(v, [=](Vec x) { return x / Vec(m); });
}

template <class T, class F>
void par_map_inplace(ThreadPool &pool, std::vector<T> &v, F f) {
  const std::size_t n = v.size();
  if (n == 0)
    return;
  const std::size_t chunks = detail::chunk_count(n, pool.size());
  const std::size_t chunk = (n + chunks - 1) / chunks;
  // Each chunk maps its own slice of the same buffer: no copy out and back.
  detail::par_run(pool, chunks, [&v, &f, chunk, n](std::size_t c) {
    const std::size_t s = c * chunk;
    const std::size_t e = std::min(n, s + chunk);
    map_inplace(std::span<T>(v.data() + s, e - s), f);
  });
}

template <class T>
void threshold_inplace(std::vector<T> &v, T lo, T hi, T replace) {
  using Vec = fp::vec<T>;
  map_inplace(v, [=](Vec x) {
    auto mask = (x < Vec(lo)) || (x > Vec(hi));
    // libstdc++ `where` yields a proxy; there is no 3-arg ternary select.
    std::experimental::where(mask, x) = Vec(replace);
    return x;
  });
}

template <class T>
std::vector<T> gather(std::vector<T> const &v, std::vector<size_t> const &idx) {
  std::vector<T> out;
  out.reserve(idx.size());
  for (size_t i : idx) {
    assert(i < v.size()); // precondition: every index is in range
    out.push_back(v[i]);
  }
  return out;
}
} // namespace fp

#endif // __has_include(<experimental/simd>)

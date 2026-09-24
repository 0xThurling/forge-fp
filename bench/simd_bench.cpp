// Benchmark the SIMD module against scalar baselines.
#include <fp/simd.hpp>
#include <cmath>
#include <numeric>
#include <vector>

#include "bench.hpp"

int main() {
  std::printf("SIMD width: %zu lanes (native_simd<double>)\n",
              fp::vec<double>::size());

  // sizes: one multiple of every lane width (1<<22), one with a tail (+3)
  std::vector<double> a(1 << 22);
  std::vector<double> b(1 << 22);
  for (std::size_t i = 0; i < a.size(); ++i) {
    a[i] = 0.5 + static_cast<double>(i % 13);
    b[i] = 1.0 - static_cast<double>(i % 7);
  }

  // --- reduce ---
  bench::measure("std::accumulate", [&] {
    double r = std::accumulate(a.begin(), a.end(), 0.0);
    bench::keep(&r);
  });
  bench::measure("fp::reduce", [&] {
    double r = fp::reduce(a);
    bench::keep(&r);
  });

  // --- dot ---
  bench::measure("naive product-sum", [&] {
    double r = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
      r += a[i] * b[i];
    bench::keep(&r);
  });
  bench::measure("fp::dot", [&] {
    double r = fp::dot(a, b);
    bench::keep(&r);
  });

  // --- map_inplace (tail included) ---
  std::printf("map_inplace (fp64, %zu elems, tail included)\n", a.size() + 3);
  auto v = a;
  v.push_back(0.1);
  v.push_back(0.2);
  v.push_back(0.3);
  bench::measure("scalar loop (*2+1)", [&] {
    for (auto& x : v)
      x = x * 2.0 + 1.0;
    bench::keep(&v[0]);
  });
  bench::measure("fp::map_inplace (*2+1)", [&] {
    fp::map_inplace(v, [](fp::vec<double> x) { return x * 2.0 + 1.0; });
    bench::keep(&v[0]);
  });

  // --- math: SIMD sqrt beats a libm sqrt per element (compiler can't vectorize) ---
  std::printf("map_sqrt (fp64, %zu elems)\n", a.size());
  auto w = a;
  bench::measure("scalar loop (std::sqrt)", [&] {
    for (auto& x : w)
      x = std::sqrt(x);
    bench::keep(&w[0]);
  });
  bench::measure("fp::map_sqrt", [&] {
    fp::map_sqrt(w);
    bench::keep(&w[0]);
  });

  // --- parallel map over 8 threads: the chunk-copy version used to move the
  //     data out and back; this should be close to map_inplace / threads ---
  {
    fp::ThreadPool pool(8);
    auto big = a; // 4M doubles
    bench::measure("par_map_inplace (8 threads)", [&] {
      fp::par_map_inplace(pool, big,
                          [](fp::vec<double> x) { return x * 2.0 + 1.0; });
      bench::keep(&big[0]);
    });
    bench::measure("map_inplace (1 thread, same data)", [&] {
      fp::map_inplace(big, [](fp::vec<double> x) { return x * 2.0 + 1.0; });
      bench::keep(&big[0]);
    });
  }
}
// Benchmark the zero-cost iteration and memory primitives against hand loops.
#include <fp/arena.hpp>
#include <fp/inplace.hpp>
#include <fp/memory.hpp>
#include <fp/scope.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <span>
#include <vector>

#include "bench.hpp"

int main() {
  constexpr std::size_t n = 1 << 22;

  std::vector<double> data(n);
  for (std::size_t i = 0; i < n; ++i)
    data[i] = static_cast<double>(i % 17);

  double sink = 0.0;

  bench::measure("sum: hand for loop", [&] {
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
      sum += data[i];
    sink = sum;
    bench::keep(&sink);
  });
  bench::measure("sum: fp::for_each", [&] {
    double sum = 0.0;
    fp::for_each(data, [&](double x) { sum += x; });
    sink = sum;
    bench::keep(&sink);
  });

  bench::measure("map: hand loop", [&] {
    for (std::size_t i = 0; i < n; ++i)
      data[i] = data[i] * 0.5 + 1.0;
    bench::keep(data.data());
  });
  bench::measure("map: fp::transform_inplace", [&] {
    fp::transform_inplace(data, [](double x) { return x * 0.5 + 1.0; });
    bench::keep(data.data());
  });

  std::vector<double> copy = data;
  bench::measure("sort: std::sort", [&] {
    std::sort(copy.begin(), copy.end());
    bench::keep(copy.data());
  });
  copy = data;
  bench::measure("sort: fp::sort_inplace", [&] {
    fp::sort_inplace(copy);
    bench::keep(copy.data());
  });

  bench::measure("alloc: std::vector<double>(n)", [&] {
    std::vector<double> v(n, 1.5);
    sink = v[n / 2];
    bench::keep(&sink);
  });
  bench::measure("alloc: fp::Buffer<double>::alloc+fill", [&] {
    auto b = fp::Buffer<double>::alloc(n);
    b.value().fill(1.5);
    sink = b.value()[n / 2];
    bench::keep(&sink);
  });
  bench::measure("alloc: new[] + fill + delete[]", [&] {
    auto *p = new double[n];
    std::fill_n(p, n, 1.5);
    sink = p[n / 2];
    delete[] p;
    bench::keep(&sink);
  });
  bench::measure("alloc: fp::with_buffer fill", [&] {
    auto r = fp::with_buffer<double>(n, [](std::span<double> s) {
      fp::fill(s, 1.5);
      return s[s.size() / 2];
    });
    sink = r.value();
    bench::keep(&sink);
  });
}

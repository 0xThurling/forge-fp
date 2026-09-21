// Benchmark fp::linalg kernels against naive hand loops.
#include <fp/linalg.hpp>

#include <cstddef>
#include <vector>

#include "bench.hpp"

int main() {
  constexpr std::size_t n = 256;

  std::vector<std::vector<double>> a(n, std::vector<double>(n));
  std::vector<std::vector<double>> b(n, std::vector<double>(n));
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      a[i][j] = 0.1 * static_cast<double>((i + j) % 7 + 1);
      b[i][j] = 0.2 * static_cast<double>((i * 3 + j) % 5 + 1);
    }
  }

  double sink = 0.0;

  bench::measure("matmul: i-j-k naive", [&] {
    std::vector<std::vector<double>> out(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t j = 0; j < n; ++j) {
        double acc = 0.0;
        for (std::size_t k = 0; k < n; ++k)
          acc += a[i][k] * b[k][j];
        out[i][j] = acc;
      }
    }
    sink = out[n / 2][n / 2];
    bench::keep(&sink);
  });

  bench::measure("matmul: fp::matmul (i-k-j)", [&] {
    auto out = fp::matmul(a, b);
    sink = out[n / 2][n / 2];
    bench::keep(&sink);
  });

  std::vector<double> x(n, 0.5);

  bench::measure("matvec: hand loop", [&] {
    std::vector<double> out(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
      double acc = 0.0;
      for (std::size_t j = 0; j < n; ++j)
        acc += a[i][j] * x[j];
      out[i] = acc;
    }
    sink = out[n / 2];
    bench::keep(&sink);
  });

  bench::measure("matvec: fp::matvec", [&] {
    auto out = fp::matvec(a, x);
    sink = out[n / 2];
    bench::keep(&sink);
  });
}

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

  // --- larger matmul (tiling matters from here up) ---
  {
    constexpr std::size_t big = 512;
    std::vector<std::vector<double>> A(big, std::vector<double>(big, 0.5));
    std::vector<std::vector<double>> B(big, std::vector<double>(big, 0.25));
    bench::measure("matmul 512: fp::matmul", 300'000'000, 3, [&] {
      auto out = fp::matmul(A, B);
      sink = out[big / 2][big / 2];
      bench::keep(&sink);
    });
  }

  // --- flat reductions, 1M doubles ---
  {
    constexpr std::size_t rn = 1 << 20;
    std::vector<double> u(rn), w(rn);
    for (std::size_t i = 0; i < rn; ++i) {
      u[i] = 0.001 * static_cast<double>(i % 997);
      w[i] = 0.002 * static_cast<double>(i % 499);
    }

    bench::measure("dot 1M: fp::dot", [&] {
      sink = fp::dot<double>(u, w);
      bench::keep(&sink);
    });
    bench::measure("dot 1M: hand loop", [&] {
      double acc = 0.0;
      for (std::size_t i = 0; i < rn; ++i)
        acc += u[i] * w[i];
      sink = acc;
      bench::keep(&sink);
    });
    bench::measure("norm_l2 1M: fp::norm_l2", [&] {
      sink = fp::norm_l2<double>(u);
      bench::keep(&sink);
    });
    bench::measure("mean 1M: fp::mean", [&] {
      sink = fp::mean(u);
      bench::keep(&sink);
    });
    bench::measure("variance 1M: fp::variance", [&] {
      sink = fp::variance(u);
      bench::keep(&sink);
    });
    bench::measure("scale 1M: fp::scale", [&] {
      auto r = fp::scale(u, 2.0);
      sink = r[0];
      bench::keep(&sink);
    });
  }

  // --- grid transpose, 1024 x 1024 (blocked vs naive hand loop) ---
  {
    constexpr std::size_t g = 1024;
    std::vector<std::vector<double>> grid(g, std::vector<double>(g, 0.5));
    bench::measure("transpose 1024x1024: fp", [&] {
      auto r = fp::transpose(grid);
      sink = r[0][0];
      bench::keep(&sink);
    });
    bench::measure("transpose 1024x1024: naive loop", [&] {
      std::vector<std::vector<double>> out(g, std::vector<double>(g));
      for (std::size_t i = 0; i < g; ++i)
        for (std::size_t j = 0; j < g; ++j)
          out[j][i] = grid[i][j];
      sink = out[0][0];
      bench::keep(&sink);
    });
  }

  // --- grid reductions, 1024 x 1024 ---
  {
    constexpr std::size_t g = 1024;
    std::vector<std::vector<double>> grid(g, std::vector<double>(g, 0.5));
    bench::measure("row_sums 1024x1024: fp", [&] {
      auto r = fp::row_sums(grid);
      sink = r[0];
      bench::keep(&sink);
    });
    bench::measure("col_sums 1024x1024: fp", [&] {
      auto r = fp::col_sums(grid);
      sink = r[0];
      bench::keep(&sink);
    });
  }
}

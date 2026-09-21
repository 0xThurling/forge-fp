// Benchmark the numerics primitives against hand-rolled equivalents.
#include <fp/numerics.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "bench.hpp"

int main() {
  constexpr std::size_t n = 1 << 20;

  std::vector<double> logits(n);
  for (std::size_t i = 0; i < n; ++i)
    logits[i] = std::sin(static_cast<double>(i) * 0.001) * 10.0;

  double sink = 0.0;

  bench::measure("softmax: hand loop", [&] {
    double m = -std::numeric_limits<double>::infinity();
    for (double x : logits)
      m = std::max(m, x);
    std::vector<double> out(n);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      out[i] = std::exp(logits[i] - m);
      sum += out[i];
    }
    for (double &x : out)
      x /= sum;
    sink = out[n / 2];
    bench::keep(&sink);
  });

  bench::measure("softmax: fp::softmax", [&] {
    auto out = fp::softmax(logits);
    sink = out[n / 2];
    bench::keep(&sink);
  });

  bench::measure("logsumexp: fp::logsumexp", [&] {
    sink = fp::logsumexp(logits);
    bench::keep(&sink);
  });
}

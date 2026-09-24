// Benchmark the RNG helpers against hand-written equivalents.
#include <fp/random.hpp>

#include <cstdio>
#include <random>
#include <vector>

#include "bench.hpp"

int main() {
  fp::Rng rng(1234);
  std::mt19937_64 engine(1234);

  // A skewed 10k-category distribution (LLM-ish shape: most mass on few ids).
  std::vector<double> weights(10'000);
  for (std::size_t i = 0; i < weights.size(); ++i)
    weights[i] = 1.0 + (i % 7 == 0 ? 50.0 : 0.0);
  std::vector<double> small_weights{1.0, 3.0, 6.0, 2.0, 8.0};

  double sink_d = 0.0;
  std::size_t sink_i = 0;

  bench::measure("uniform: fp", [&] {
    sink_d = rng.uniform(0.0, 1.0);
    bench::keep(&sink_d);
  });
  bench::measure("randint: fp", [&] {
    sink_i = static_cast<std::size_t>(rng.randint(1, 6));
    bench::keep(&sink_i);
  });
  bench::measure("normal: fp", [&] {
    sink_d = rng.normal(0.0, 1.0);
    bench::keep(&sink_d);
  });
  bench::measure("normal: fresh distribution (old style)", [&] {
    std::normal_distribution<double> dist(0.0, 1.0);
    sink_d = dist(engine);
    bench::keep(&sink_d);
  });
  bench::measure("normal: held distribution", [&] {
    static std::normal_distribution<double> dist(0.0, 1.0);
    sink_d = dist(engine);
    bench::keep(&sink_d);
  });
  bench::measure("bernoulli: fp", [&] {
    sink_i = rng.bernoulli(0.3) ? 1u : 0u;
    bench::keep(&sink_i);
  });
  bench::measure("categorical k=5: fp", [&] {
    sink_i = rng.categorical(small_weights);
    bench::keep(&sink_i);
  });
  bench::measure("categorical k=10k: fp", [&] {
    sink_i = rng.categorical(weights);
    bench::keep(&sink_i);
  });
  {
    static const fp::Categorical dist(weights);
    bench::measure("Categorical k=10k draw: fp", [&] {
      sink_i = dist.draw(rng);
      bench::keep(&sink_i);
    });
  }
  bench::measure("next_double: fp", [&] {
    sink_d = rng.next_double();
    bench::keep(&sink_d);
  });
  bench::measure("below(10k): fp", [&] {
    sink_i = rng.below(10'000);
    bench::keep(&sink_i);
  });
  bench::measure("sample_indices n=1M k=10: fp", [&] {
    auto v = rng.sample_indices(1'000'000, 10);
    sink_i = v[0];
    bench::keep(&sink_i);
  });
  bench::measure("shuffle 100k: fp", [&] {
    static std::vector<int> v(100'000);
    for (std::size_t i = 0; i < v.size(); ++i)
      v[i] = static_cast<int>(i);
    rng.shuffle(v);
    sink_i = static_cast<std::size_t>(v[0]);
    bench::keep(&sink_i);
  });
}

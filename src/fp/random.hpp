#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fp {

// The only source of randomness in the library: seedable, explicit, and
// reproducible. Library code never constructs an Rng internally; callers pass
// one in and seed it explicitly.
class Rng {
public:
  explicit Rng(std::uint64_t seed) : seed_(seed), engine_(seed) {}

  std::uint64_t next_u64() { return engine_(); }

  // Inclusive range [lo, hi].
  int randint(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(engine_);
  }

  double uniform(double lo = 0.0, double hi = 1.0) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(engine_);
  }

  double normal(double mean = 0.0, double stddev = 1.0) {
    std::normal_distribution<double> dist(mean, stddev);
    return dist(engine_);
  }

  bool bernoulli(double p) {
    std::bernoulli_distribution dist(p);
    return dist(engine_);
  }

  template <class T> void shuffle(std::vector<T> &v) {
    std::shuffle(v.begin(), v.end(), engine_);
  }

  // k distinct indices from [0, n), in random order. k is clamped to n.
  // Dense draws use a Fisher-Yates prefix; sparse ones (k << n) sample by
  // rejection into a set, so 10-of-a-million is O(k) instead of O(n).
  std::vector<std::size_t> sample_indices(std::size_t n, std::size_t k) {
    k = std::min(k, n);
    std::vector<std::size_t> out;
    if (k == 0)
      return out;
    if (k >= (n + 3) / 4) {
      std::vector<std::size_t> idx(n);
      for (std::size_t i = 0; i < n; ++i)
        idx[i] = i;
      for (std::size_t i = 0; i < k; ++i) {
        std::uniform_int_distribution<std::size_t> dist(i, n - 1);
        std::swap(idx[i], idx[dist(engine_)]);
      }
      idx.resize(k);
      return idx;
    }
    std::unordered_set<std::size_t> seen;
    seen.reserve(k * 2);
    out.reserve(k);
    std::uniform_int_distribution<std::size_t> dist(0, n - 1);
    while (out.size() < k) {
      const std::size_t i = dist(engine_);
      if (seen.insert(i).second)
        out.push_back(i);
    }
    return out;
  }

  // Weighted choice; weights need not be normalized. Requires a non-empty
  // vector with a positive total (asserted).
  std::size_t categorical(std::vector<double> const &weights) {
    assert(!weights.empty());
    double total = 0.0;
    for (double w : weights)
      total += w;
    assert(total > 0.0);

    std::uniform_real_distribution<double> dist(0.0, total);
    const double r = dist(engine_);
    double acc = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
      acc += weights[i];
      if (r <= acc)
        return i;
    }
    return weights.size() - 1;
  }

  std::size_t weighted_choice(std::vector<double> const &weights) {
    return categorical(weights);
  }

  std::uint64_t seed() const { return seed_; }

private:
  std::uint64_t seed_;
  std::mt19937_64 engine_;
};

} // namespace fp

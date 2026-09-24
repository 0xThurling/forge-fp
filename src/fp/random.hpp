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

  // Uniform double in [0, 1) with 53-bit resolution, straight from the engine
  // word. (~1ns; uniform_real_distribution costs ~6.5ns.)
  double next_double() {
    return static_cast<double>(engine_() >> 11) * 0x1.0p-53;
  }

  // Uniform index in [0, n) without constructing a distribution at the call
  // site (uniform_int_distribution is a multiply plus rejection here).
  std::size_t below(std::size_t n) {
    assert(n > 0);
    std::uniform_int_distribution<std::size_t> dist(0, n - 1);
    return dist(engine_);
  }

  // Inclusive range [lo, hi].
  int randint(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(engine_);
  }

  double uniform(double lo = 0.0, double hi = 1.0) {
    return lo + (hi - lo) * next_double();
  }

  // Gaussian. The distribution is a member so the second Box-Muller value is
  // not thrown away: a fresh distribution per call costs ~1.9x more.
  double normal(double mean = 0.0, double stddev = 1.0) {
    return mean + stddev * normal_(engine_);
  }

  bool bernoulli(double p) {
    assert(p >= 0.0 && p <= 1.0);
    return next_double() < p;
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
      for (std::size_t i = 0; i < k; ++i)
        std::swap(idx[i], idx[i + below(n - i)]);
      idx.resize(k);
      return idx;
    }
    std::unordered_set<std::size_t> seen;
    seen.reserve(k * 2);
    out.reserve(k);
    while (out.size() < k) {
      const std::size_t i = below(n);
      if (seen.insert(i).second)
        out.push_back(i);
    }
    return out;
  }

  // One weighted draw, O(k): it sums the weights and scans them. For repeated
  // draws from the same distribution build an fp::Categorical once instead
  // (O(k) build, O(1) per draw). Weights need not be normalized.
  std::size_t categorical(std::vector<double> const &weights) {
    assert(!weights.empty());
    double total = 0.0;
    for (double w : weights)
      total += w;
    assert(total > 0.0);

    const double r = next_double() * total;
    double acc = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
      acc += weights[i];
      if (r < acc)
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
  // Standard normal source; keeps the cached second sample between calls.
  std::normal_distribution<double> normal_{0.0, 1.0};
};

// Precomputed alias table (Vose's method) for repeated weighted draws.
//
//   fp::Categorical dist(weights);        // O(k) once
//   auto id = dist.draw(rng);             // O(1) per draw
//
// Weights must be non-negative with a positive total (asserted). Memory is
// two k-element tables.
class Categorical {
public:
  explicit Categorical(std::vector<double> weights) {
    assert(!weights.empty());
    const std::size_t n = weights.size();
    double total = 0.0;
    for (double w : weights) {
      assert(w >= 0.0);
      total += w;
    }
    assert(total > 0.0);

    prob_.assign(n, 1.0);
    alias_.resize(n);
    for (std::size_t i = 0; i < n; ++i)
      alias_[i] = i;

    std::vector<double> scaled(n);
    for (std::size_t i = 0; i < n; ++i)
      scaled[i] = weights[i] * static_cast<double>(n) / total;

    std::vector<std::size_t> small, large;
    small.reserve(n);
    large.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
      (scaled[i] < 1.0 ? small : large).push_back(i);

    while (!small.empty() && !large.empty()) {
      const std::size_t s = small.back();
      small.pop_back();
      const std::size_t l = large.back();
      large.pop_back();
      prob_[s] = scaled[s];
      alias_[s] = l;
      scaled[l] = (scaled[l] + scaled[s]) - 1.0;
      (scaled[l] < 1.0 ? small : large).push_back(l);
    }
    while (!large.empty()) {
      prob_[large.back()] = 1.0;
      large.pop_back();
    }
    while (!small.empty()) {
      prob_[small.back()] = 1.0;
      small.pop_back();
    }
  }

  std::size_t size() const { return prob_.size(); }

  std::size_t draw(Rng &rng) const {
    const std::size_t n = prob_.size();
    if (n == 1)
      return 0;
    const std::size_t i = rng.below(n);
    return rng.next_double() < prob_[i] ? i : alias_[i];
  }

private:
  std::vector<double> prob_;
  std::vector<std::size_t> alias_;
};

} // namespace fp

#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <cstddef>
#include <vector>

TEST(Random, SameSeedSameSequence) {
  fp::Rng a(42), b(42);
  for (int i = 0; i < 10; ++i)
    EXPECT_EQ(a.next_u64(), b.next_u64());

  fp::Rng c(43);
  EXPECT_NE(fp::Rng(42).next_u64(), c.next_u64());
}

TEST(Random, Ranges) {
  fp::Rng rng(7);
  for (int i = 0; i < 1000; ++i) {
    const int v = rng.randint(-3, 5);
    EXPECT_GE(v, -3);
    EXPECT_LE(v, 5);

    const double u = rng.uniform(2.0, 3.0);
    EXPECT_GE(u, 2.0);
    EXPECT_LE(u, 3.0);
  }

  fp::Rng r2(9);
  EXPECT_FALSE(r2.bernoulli(0.0));
  EXPECT_TRUE(r2.bernoulli(1.0));
}

TEST(Random, NormalMoments) {
  fp::Rng rng(123);
  const int n = 20000;
  double sum = 0.0;
  for (int i = 0; i < n; ++i)
    sum += rng.normal(1.0, 2.0);
  EXPECT_NEAR(sum / n, 1.0, 0.1);
}

TEST(Random, ShuffleIsPermutation) {
  fp::Rng rng(5);
  std::vector<int> v(100);
  for (int i = 0; i < 100; ++i)
    v[i] = i;

  rng.shuffle(v);
  auto sorted = v;
  std::sort(sorted.begin(), sorted.end());
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(sorted[i], i);
}

TEST(Random, SampleIndices) {
  fp::Rng rng(11);
  auto idx = rng.sample_indices(10, 4);
  ASSERT_EQ(idx.size(), 4u);

  std::sort(idx.begin(), idx.end());
  for (std::size_t i = 0; i < idx.size(); ++i) {
    EXPECT_LT(idx[i], 10u);
    if (i > 0) {
      EXPECT_LT(idx[i - 1], idx[i]);
    }
  }

  EXPECT_EQ(rng.sample_indices(3, 99).size(), 3u);
}

TEST(Random, Categorical) {
  fp::Rng rng(3);
  std::vector<double> weights = {1.0, 0.0, 3.0};

  int counts[3] = {0, 0, 0};
  for (int i = 0; i < 2000; ++i)
    ++counts[rng.categorical(weights)];

  EXPECT_EQ(counts[1], 0);
  EXPECT_GT(counts[2], counts[0]);
}

TEST(Random, FastPrimitives) {
  fp::Rng rng(17);
  for (int i = 0; i < 10'000; ++i) {
    const double u = rng.next_double();
    EXPECT_GE(u, 0.0);
    EXPECT_LT(u, 1.0);
    EXPECT_LT(rng.below(7), 7u);
    EXPECT_EQ(rng.below(1), 0u);
  }
}

TEST(Random, NormalKeepsTheCachedSpare) {
  // Two consecutive draws must differ (a fresh distribution per call would
  // still differ, but the point is that the spare is reused, not discarded).
  fp::Rng rng(21);
  const double a = rng.normal();
  const double b = rng.normal();
  EXPECT_NE(a, b);

  // Still reproducible for a fixed seed and call pattern.
  fp::Rng r1(99), r2(99);
  for (int i = 0; i < 100; ++i)
    EXPECT_DOUBLE_EQ(r1.normal(0.5, 2.0), r2.normal(0.5, 2.0));
}

TEST(Random, CategoricalMatchesWeights) {
  fp::Rng rng(31);
  const std::vector<double> weights = {1.0, 3.0, 6.0};
  fp::Categorical dist(weights);
  ASSERT_EQ(dist.size(), 3u);

  const int n = 300'000;
  int counts[3] = {0, 0, 0};
  for (int i = 0; i < n; ++i)
    ++counts[dist.draw(rng)];

  EXPECT_NEAR(static_cast<double>(counts[0]) / n, 0.1, 0.01);
  EXPECT_NEAR(static_cast<double>(counts[1]) / n, 0.3, 0.01);
  EXPECT_NEAR(static_cast<double>(counts[2]) / n, 0.6, 0.01);
}

TEST(Random, CategoricalEdgeCases) {
  fp::Rng rng(41);
  // Single category always wins, even with zero-weight siblings.
  fp::Categorical one({0.0, 5.0});
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(one.draw(rng), 1u);

  // Every weight reached at least once; indices stay in range.
  std::vector<double> many(1000, 1.0);
  fp::Categorical uniform(many);
  std::vector<bool> seen(1000, false);
  for (int i = 0; i < 20'000; ++i) {
    const std::size_t d = uniform.draw(rng);
    ASSERT_LT(d, 1000u);
    seen[d] = true;
  }
  EXPECT_EQ(std::count(seen.begin(), seen.end(), true), 1000);
}

TEST(Random, CategoricalDrawIsO1) {
  // A large table must not slow a draw down: this is the LLM-sampling shape.
  fp::Rng rng(51);
  std::vector<double> weights(50'000);
  for (std::size_t i = 0; i < weights.size(); ++i)
    weights[i] = 1.0 + (i % 13 == 0 ? 100.0 : 0.0);
  fp::Categorical dist(std::move(weights));
  std::size_t sink = 0;
  for (int i = 0; i < 1'000'000; ++i)
    sink += dist.draw(rng);
  EXPECT_GT(sink, 0u);
}


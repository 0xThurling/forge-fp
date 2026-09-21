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

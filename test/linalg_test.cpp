#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

TEST(Linalg, MatmulAndMatvec) {
  std::vector<std::vector<int>> a = {{1, 2, 3}, {4, 5, 6}};      // 2x3
  std::vector<std::vector<int>> b = {{7, 8}, {9, 10}, {11, 12}}; // 3x2

  EXPECT_EQ(fp::matmul(a, b), (std::vector<std::vector<int>>{{58, 64}, {139, 154}}));
  EXPECT_EQ(fp::matvec(a, std::vector<int>{1, 0, -1}),
            (std::vector<int>{-2, -2}));
}

TEST(Linalg, MatmulProperties) {
  std::vector<std::vector<double>> a = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
  std::vector<std::vector<double>> b = {{7.0, 8.0}, {9.0, 10.0}, {11.0, 12.0}};

  auto ab_t = fp::transpose(fp::matmul(a, b));
  auto bt_at = fp::matmul(fp::transpose(b), fp::transpose(a));
  ASSERT_EQ(ab_t.size(), bt_at.size());
  for (std::size_t i = 0; i < ab_t.size(); ++i)
    for (std::size_t j = 0; j < ab_t[i].size(); ++j)
      EXPECT_DOUBLE_EQ(ab_t[i][j], bt_at[i][j]);

  std::vector<std::vector<double>> i3 = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  EXPECT_EQ(fp::matmul(a, i3), a);
}

TEST(Linalg, BatchedOuterHadamardScale) {
  std::vector<std::vector<std::vector<int>>> ba = {{{1, 2}}, {{3, 4}}};
  std::vector<std::vector<std::vector<int>>> bb = {{{5}, {6}}, {{7}, {8}}};

  auto bc = fp::batched_matmul(ba, bb);
  ASSERT_EQ(bc.size(), 2u);
  EXPECT_EQ(bc[0], (std::vector<std::vector<int>>{{17}}));
  EXPECT_EQ(bc[1], (std::vector<std::vector<int>>{{53}}));

  EXPECT_EQ(fp::outer(std::vector<int>{1, 2}, std::vector<int>{3, 4, 5}),
            (std::vector<std::vector<int>>{{3, 4, 5}, {6, 8, 10}}));
  EXPECT_EQ(fp::hadamard(std::vector<int>{1, 2, 3}, std::vector<int>{4, 5, 6}),
            (std::vector<int>{4, 10, 18}));
  EXPECT_EQ(fp::scale(std::vector<int>{1, 2}, 3), (std::vector<int>{3, 6}));

  EXPECT_EQ(fp::add_row_broadcast(std::vector<std::vector<int>>{{1, 2}, {3, 4}},
                                  std::vector<int>{10, 20}),
            (std::vector<std::vector<int>>{{11, 22}, {13, 24}}));
}

TEST(Linalg, Solve) {
  // 2x + 3y = 8 ; 5x - y = 3 -> x = 1, y = 2
  auto x = fp::solve(std::vector<std::vector<double>>{{2.0, 3.0}, {5.0, -1.0}},
                     std::vector<double>{8.0, 3.0});
  ASSERT_TRUE(x.is_ok());
  EXPECT_NEAR(x.value()[0], 1.0, 1e-12);
  EXPECT_NEAR(x.value()[1], 2.0, 1e-12);

  auto singular =
      fp::solve(std::vector<std::vector<double>>{{1.0, 2.0}, {2.0, 4.0}},
                std::vector<double>{1.0, 2.0});
  EXPECT_FALSE(singular.is_ok());

  auto bad_shape = fp::solve(std::vector<std::vector<double>>{{1.0, 2.0, 3.0}},
                             std::vector<double>{1.0});
  EXPECT_FALSE(bad_shape.is_ok());
}

TEST(Linalg, Reductions) {
  std::vector<double> v = {3.0, 1.0, 4.0, 1.0, 5.0};
  EXPECT_DOUBLE_EQ(fp::dot<double>(std::span<const double>(v),
                                   std::span<const double>(v)),
                   52.0);
  EXPECT_DOUBLE_EQ(fp::norm_l1<double>(v), 14.0);
  EXPECT_NEAR(fp::norm_l2<double>(v), std::sqrt(52.0), 1e-12);

  EXPECT_EQ(fp::argmax(std::vector<int>{3, 7, 7, 2}).value(), 1u);
  EXPECT_EQ(fp::argmin(std::vector<int>{3, 7, 2, 2}).value(), 2u);
  EXPECT_FALSE(fp::argmax(std::vector<int>{}).has_value());
}

TEST(Linalg, MeanVariance) {
  EXPECT_DOUBLE_EQ(fp::mean(std::vector<int>{1, 2, 3, 4}), 2.5);
  EXPECT_DOUBLE_EQ(fp::variance(std::vector<int>{1, 2, 3, 4}, 0), 1.25);
  EXPECT_NEAR(fp::variance(std::vector<int>{1, 2, 3, 4}, 1), 5.0 / 3.0, 1e-12);
}

TEST(Linalg, GridReductions) {
  std::vector<std::vector<int>> g = {{1, 2, 3}, {4, 5, 6}};

  EXPECT_EQ(fp::row_sums(g), (std::vector<int>{6, 15}));
  EXPECT_EQ(fp::col_sums(g), (std::vector<int>{5, 7, 9}));
  EXPECT_EQ(fp::row_means(g), (std::vector<int>{2, 5}));
  EXPECT_EQ(fp::col_means(g), (std::vector<int>{2, 3, 4}));

  std::vector<std::vector<int>> h = {{1, 9, 2}, {5, 3, 4}};
  EXPECT_EQ(fp::argmax_rows(h), (std::vector<std::size_t>{1, 0}));
  EXPECT_EQ(fp::argmin_rows(h), (std::vector<std::size_t>{0, 1}));
}

TEST(Linalg, MultiAccumulatorReductionsMatchNaiveSums) {
  std::vector<double> v(10'000);
  for (std::size_t i = 0; i < v.size(); ++i)
    v[i] = 0.001 * static_cast<double>(i % 977) - 0.4;

  double naive = 0.0;
  for (double x : v)
    naive += x;
  EXPECT_NEAR(fp::mean(v) * static_cast<double>(v.size()), naive, 1e-9);

  double sq = 0.0;
  for (double x : v)
    sq += x * x;
  EXPECT_NEAR(fp::dot<double>(v, v), sq, 1e-9);
  EXPECT_NEAR(fp::norm_l2<double>(v), std::sqrt(sq), 1e-9);

  // matvec against the hand loop, with a ragged (non-multiple-of-4) width
  std::vector<std::vector<double>> a(7, std::vector<double>(13, 0.5));
  std::vector<double> x(13, 2.0);
  auto got = fp::matvec(a, x);
  ASSERT_EQ(got.size(), 7u);
  for (double y : got)
    EXPECT_DOUBLE_EQ(y, 13.0);
}

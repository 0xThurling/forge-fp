// Instantiates every public API of linalg.hpp with *only* linalg.hpp included
// (see api_vec_test.cpp for why: another module's overload must not mask a
// compile error in this one). Also pins the transpose re-export: it is defined
// in grid.hpp, which linalg.hpp includes.
#include <fp/linalg.hpp>

#include <gtest/gtest.h>
#include <cmath>
#include <span>
#include <vector>

TEST(ApiLinalg, Products) {
  const std::vector<std::vector<int>> a{{1, 2, 3}, {4, 5, 6}};    // 2x3
  const std::vector<std::vector<int>> b{{7, 8}, {9, 10}, {11, 12}}; // 3x2

  EXPECT_EQ(fp::matmul(a, b),
            (std::vector<std::vector<int>>{{58, 64}, {139, 154}}));
  EXPECT_EQ(fp::matvec(a, std::vector<int>{1, 0, -1}),
            (std::vector<int>{-2, -2}));
  EXPECT_EQ(fp::outer(std::vector<int>{1, 2}, std::vector<int>{3, 4}),
            (std::vector<std::vector<int>>{{3, 4}, {6, 8}}));
  EXPECT_EQ(fp::batched_matmul(std::vector<std::vector<std::vector<int>>>{a},
                               std::vector<std::vector<std::vector<int>>>{b})
                .size(),
            1u);
  EXPECT_EQ(fp::transpose(a),
            (std::vector<std::vector<int>>{{1, 4}, {2, 5}, {3, 6}}));

  // Flat-span form, including ragged sizes that exercise the tile tails.
  const std::vector<double> fa{1, 2, 3, 4, 5, 6};
  const std::vector<double> fb{7, 8, 9, 10, 11, 12};
  auto flat = fp::matmul<double>(fa, 2, 3, fb, 2);
  ASSERT_EQ(flat.size(), 4u);
  EXPECT_DOUBLE_EQ(flat[0], 58.0);
  EXPECT_DOUBLE_EQ(flat[3], 154.0);
}

TEST(ApiLinalg, SolveAndElementwise) {
  const std::vector<std::vector<double>> a{{2.0, 1.0}, {1.0, 3.0}};
  auto x = fp::solve(a, std::vector<double>{3.0, 5.0});
  ASSERT_TRUE(x.is_ok());
  EXPECT_NEAR(x.value()[0], 0.8, 1e-12);
  EXPECT_NEAR(x.value()[1], 1.4, 1e-12);
  EXPECT_FALSE(fp::solve(std::vector<std::vector<double>>{{1.0, 2.0}, {2.0, 4.0}},
                         std::vector<double>{1.0, 2.0})
                   .is_ok());

  EXPECT_EQ(fp::hadamard(std::vector<int>{1, 2, 3}, std::vector<int>{4, 5, 6}),
            (std::vector<int>{4, 10, 18}));
  EXPECT_EQ(fp::scale(std::vector<int>{1, 2}, 3), (std::vector<int>{3, 6}));
  EXPECT_EQ(fp::add_row_broadcast(std::vector<std::vector<int>>{{1, 1}},
                                  std::vector<int>{10, 20}),
            (std::vector<std::vector<int>>{{11, 21}}));
}

TEST(ApiLinalg, Reductions) {
  const std::vector<int> v{1, 2, 3, 4};
  const std::vector<double> d{1.0, 2.0, 3.0, 4.0};

  EXPECT_EQ(fp::dot<int>(v, v), 30);
  EXPECT_EQ(fp::norm_l1<double>(d), 10.0);
  EXPECT_NEAR(fp::norm_l2<double>(d), std::sqrt(30.0), 1e-12);
  EXPECT_EQ(fp::argmax(v).value(), 3u);
  EXPECT_EQ(fp::argmin(v).value(), 0u);
  EXPECT_FALSE(fp::argmax(std::vector<int>{}).has_value());
  EXPECT_DOUBLE_EQ(fp::mean(v), 2.5);
  EXPECT_DOUBLE_EQ(fp::variance(v, 0), 1.25);
  EXPECT_NEAR(fp::variance(v, 1), 5.0 / 3.0, 1e-12);
}

TEST(ApiLinalg, GridReductions) {
  const std::vector<std::vector<int>> g{{1, 2, 3}, {4, 5, 6}};
  EXPECT_EQ(fp::row_sums(g), (std::vector<int>{6, 15}));
  EXPECT_EQ(fp::col_sums(g), (std::vector<int>{5, 7, 9}));
  EXPECT_EQ(fp::row_means(g), (std::vector<int>{2, 5}));
  EXPECT_EQ(fp::col_means(g), (std::vector<int>{2, 3, 4}));
  EXPECT_EQ(fp::argmax_rows(g), (std::vector<std::size_t>{2, 2}));
  EXPECT_EQ(fp::argmin_rows(g), (std::vector<std::size_t>{0, 0}));
  EXPECT_EQ(fp::row_sums(std::vector<std::vector<int>>{}),
            (std::vector<int>{}));
  EXPECT_EQ(fp::col_sums(std::vector<std::vector<int>>{}),
            (std::vector<int>{}));
}

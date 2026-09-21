#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

TEST(Numerics, SigmoidReluClamp) {
  EXPECT_DOUBLE_EQ(fp::sigmoid(0.0), 0.5);
  EXPECT_NEAR(fp::sigmoid(100.0), 1.0, 1e-12);
  EXPECT_NEAR(fp::sigmoid(-100.0), 0.0, 1e-12);
  EXPECT_TRUE(std::isfinite(fp::sigmoid(1000.0)));
  EXPECT_DOUBLE_EQ(fp::relu(-2.0), 0.0);
  EXPECT_DOUBLE_EQ(fp::relu(2.0), 2.0);
  EXPECT_EQ(fp::clamp(5, 0, 3), 3);
}

TEST(Numerics, SoftmaxAndLogSoftmax) {
  auto s = fp::softmax(std::vector<double>{1000.0, 1000.0});
  ASSERT_EQ(s.size(), 2u);
  EXPECT_DOUBLE_EQ(s[0], 0.5);
  EXPECT_DOUBLE_EQ(s[1], 0.5);

  auto p = fp::softmax(std::vector<double>{1.0, 2.0, 3.0});
  double sum = 0.0;
  for (double x : p)
    sum += x;
  EXPECT_NEAR(sum, 1.0, 1e-12);

  auto lp = fp::log_softmax(std::vector<double>{1.0, 2.0, 3.0});
  ASSERT_EQ(lp.size(), p.size());
  for (std::size_t i = 0; i < p.size(); ++i)
    EXPECT_NEAR(lp[i], std::log(p[i]), 1e-12);
}

TEST(Numerics, SoftmaxRows) {
  std::vector<std::vector<double>> g = {{1.0, 2.0, 3.0}, {0.0, 0.0}};
  fp::softmax_rows(g);

  double sum = 0.0;
  for (double x : g[0])
    sum += x;
  EXPECT_NEAR(sum, 1.0, 1e-12);
  EXPECT_DOUBLE_EQ(g[1][0], 0.5);
  EXPECT_DOUBLE_EQ(g[1][1], 0.5);
}

TEST(Numerics, LogSumExp) {
  EXPECT_DOUBLE_EQ(fp::logsumexp(std::vector<double>{0.0}), 0.0);
  EXPECT_DOUBLE_EQ(fp::logsumexp(std::vector<double>{1000.0, 1000.0}),
                   1000.0 + std::log(2.0));
  EXPECT_TRUE(std::isinf(fp::logsumexp(std::vector<double>{})));
}

TEST(Numerics, LinspaceAndArange) {
  EXPECT_EQ(fp::linspace(0.0, 1.0, 3), (std::vector<double>{0.0, 0.5, 1.0}));
  EXPECT_EQ(fp::linspace(5.0, 5.0, 1), (std::vector<double>{5.0}));
  EXPECT_TRUE(fp::linspace(0.0, 1.0, 0).empty());

  auto a = fp::arange(0.0, 1.0, 0.25);
  ASSERT_EQ(a.size(), 4u);
  EXPECT_DOUBLE_EQ(a[3], 0.75);
  EXPECT_TRUE(fp::arange(0.0, 1.0, 0.0).empty());
  EXPECT_EQ(fp::arange(3.0, 0.0, -1.0), (std::vector<double>{3.0, 2.0, 1.0}));
}

TEST(Numerics, PredicatesAndDerivative) {
  EXPECT_TRUE(fp::approx_equal(1.0, 1.0 + 1e-12));
  EXPECT_FALSE(fp::approx_equal(1.0, 1.1));
  EXPECT_TRUE(fp::is_finite(1.0));
  EXPECT_FALSE(fp::is_finite(std::nan("")));
  EXPECT_DOUBLE_EQ(fp::nan_to_num(std::nan("")), 0.0);

  auto f = [](double x) { return x * x; };
  EXPECT_NEAR(fp::central_difference(f, 3.0), 6.0, 1e-6);
}

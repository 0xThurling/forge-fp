#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(Ops, ElementwiseMath) {
  EXPECT_EQ(fp::abs(-3), 3);
  EXPECT_EQ(fp::abs(3u), 3u);
  EXPECT_EQ(fp::sign(-2.5), -1);
  EXPECT_EQ(fp::sign(0.0), 0);
  EXPECT_EQ(fp::sign(7), 1);
  EXPECT_DOUBLE_EQ(fp::sqrt(9.0), 3.0);
  EXPECT_DOUBLE_EQ(fp::exp(0.0), 1.0);
  EXPECT_DOUBLE_EQ(fp::log(1.0), 0.0);
  EXPECT_DOUBLE_EQ(fp::log1p(0.0), 0.0);
  EXPECT_DOUBLE_EQ(fp::sin(0.0), 0.0);
  EXPECT_DOUBLE_EQ(fp::cos(0.0), 1.0);
  EXPECT_DOUBLE_EQ(fp::tanh(0.0), 0.0);
  EXPECT_NEAR(fp::tanh(1.0), std::tanh(1.0), 1e-15);
  EXPECT_NEAR(fp::sin(0.5), std::sin(0.5), 1e-15);
}

TEST(Ops, ElementwiseMathInPipelines) {
  // The elementwise math is a plain lambda, so it drops into fp::map and the
  // piped stages like any other unary function.
  const std::vector<double> xs{0.0, 0.5, -0.5};
  const auto ys = fp::map(xs, fp::tanh);
  ASSERT_EQ(ys.size(), xs.size());
  for (std::size_t i = 0; i < xs.size(); ++i)
    EXPECT_DOUBLE_EQ(ys[i], std::tanh(xs[i]));

  const auto zs = fp::out(fp::into(xs) | fp::map(fp::sin));
  ASSERT_EQ(zs.size(), xs.size());
  EXPECT_DOUBLE_EQ(zs[1], std::sin(0.5));
}

TEST(Ops, CurriedMinMaxPowClamp) {
  EXPECT_EQ(fp::min_(0)(5), 0);
  EXPECT_EQ(fp::min_(0)(-5), -5);
  EXPECT_EQ(fp::max_(0)(-5), 0);
  EXPECT_EQ(fp::max_(0)(5), 5);
  EXPECT_DOUBLE_EQ(fp::pow(2)(3.0), 9.0);
  EXPECT_EQ(fp::clamp(0, 10)(5), 5);
  EXPECT_EQ(fp::clamp(0, 10)(-1), 0);
  EXPECT_EQ(fp::clamp(0, 10)(11), 10);
}

TEST(Ops, Pipelines) {
  std::vector<double> v = {-2.0, -1.0, 0.0, 1.0, 2.0};
  EXPECT_EQ(fp::to_vector(fp::map(v, fp::abs)),
            (std::vector<double>{2, 1, 0, 1, 2}));
  EXPECT_EQ(fp::to_vector(fp::map(v, fp::max_(0.0))),
            (std::vector<double>{0, 0, 0, 1, 2}));
}

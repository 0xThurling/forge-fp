#include <fp/autodiff.hpp>

#include <gtest/gtest.h>
#include <cmath>

TEST(Autodiff, ArithmeticRules) {
  using D = fp::Dual<double>;
  const D x(2.0, 1.0);

  const D y = x * x + 3.0 * x - 5.0;
  EXPECT_DOUBLE_EQ(y.value, 4.0 + 6.0 - 5.0);
  EXPECT_DOUBLE_EQ(y.deriv, 2.0 * 2.0 + 3.0);

  const D z = (x + 1.0) / (x - 1.0);
  EXPECT_DOUBLE_EQ(z.value, 3.0);
  EXPECT_DOUBLE_EQ(z.deriv, -2.0 / 1.0);
}

TEST(Autodiff, ElementaryFunctions) {
  using fp::ad::cos;
  using fp::ad::exp;
  using fp::ad::log;
  using fp::ad::pow;
  using fp::ad::sin;
  using fp::ad::tanh;

  using D = fp::Dual<double>;

  EXPECT_NEAR(fp::derivative([](D x) { return exp(x) * x; }, 1.0), 2.0 * std::exp(1.0), 1e-12);
  EXPECT_NEAR(fp::derivative([](D x) { return log(x); }, 2.0), 0.5, 1e-12);
  EXPECT_NEAR(fp::derivative([](D x) { return sin(x); }, 0.0), 1.0, 1e-12);
  EXPECT_NEAR(fp::derivative([](D x) { return cos(x); }, 0.0), 0.0, 1e-12);
  EXPECT_NEAR(fp::derivative([](D x) { return tanh(x); }, 0.0), 1.0, 1e-12);
  EXPECT_NEAR(fp::derivative([](D x) { return pow(x, 3.0); }, 2.0), 12.0, 1e-12);
}

TEST(Autodiff, ChainAndScalarMix) {
  using fp::ad::exp;
  using fp::ad::log;
  using D = fp::Dual<double>;

  // d/dx log(exp(x)) == 1
  EXPECT_NEAR(fp::derivative([](D x) { return log(exp(x)); }, 3.0), 1.0, 1e-12);
  // d/dx (3 - 2x)^2 at x = 1 -> -4 * (3 - 2) = -4
  EXPECT_NEAR(fp::derivative([](D x) { return (3.0 - 2.0 * x) * (3.0 - 2.0 * x); }, 1.0),
              -4.0, 1e-12);
}

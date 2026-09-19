#include <fp/simd.hpp>

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

TEST(Simd, TypesExist) {
  EXPECT_EQ(fp::vec4f::size(), 4u);
  EXPECT_EQ(fp::vec8d::size(), 8u);
  fp::simd<float, 2> x(1.0f);
  EXPECT_EQ(x[0], 1.0f);
}

TEST(Simd, MapInplaceReduceDot) {
  std::vector<float> v(100);
  std::iota(v.begin(), v.end(), 1.0f);
  fp::map_inplace(v, [](fp::vec<float> x) { return x * fp::vec<float>(2.0f); });
  EXPECT_FLOAT_EQ(v[99], 200.0f);
  EXPECT_FLOAT_EQ(fp::reduce(v), 10100.0f);
  EXPECT_FLOAT_EQ(fp::dot(std::vector<float>{1, 2, 3}, std::vector<float>{4, 5, 6}),
                  32.0f);
}

TEST(Simd, MapInplaceTailHandling) {
  std::vector<int> v(11);
  std::iota(v.begin(), v.end(), 1);
  fp::map_inplace(v, [](fp::vec<int> x) { return x + fp::vec<int>(1); });
  ASSERT_EQ(v.size(), 11u);
  EXPECT_EQ(v.front(), 2);
  EXPECT_EQ(v.back(), 12);
}

TEST(Simd, MapTo) {
  std::vector<float> v = {1, 2, 3, 4, 5};
  auto roots = fp::map_to(v, [](fp::vec<float> x) { return std::experimental::sqrt(x); });
  ASSERT_EQ(roots.size(), 5u);
  EXPECT_NEAR(roots[4], std::sqrt(5.0f), 1e-5);
}

TEST(Simd, SqrtExpClampNormalize) {
  std::vector<float> s = {1, 4, 9, 16};
  fp::map_sqrt(s);
  EXPECT_NEAR(s[3], 4.0f, 1e-5);

  std::vector<float> e = {0, 1};
  fp::map_exp(e);
  EXPECT_NEAR(e[1], std::exp(1.0f), 1e-5);

  std::vector<float> c = {-1, 0, 2, 5};
  fp::clamp_inplace(c, 0.0f, 3.0f);
  EXPECT_FLOAT_EQ(c[0], 0.0f);
  EXPECT_FLOAT_EQ(c[1], 0.0f);
  EXPECT_FLOAT_EQ(c[2], 2.0f);
  EXPECT_FLOAT_EQ(c[3], 3.0f);

  std::vector<float> n = {3, 4};
  fp::normalize(n);
  EXPECT_NEAR(n[0], 0.75f, 1e-6);
  EXPECT_NEAR(n[1], 1.0f, 1e-6);
}

TEST(Simd, ThresholdAndGather) {
  std::vector<float> t = {0, 5, 10, 20};
  fp::threshold_inplace(t, 1.0f, 15.0f, -1.0f);
  EXPECT_FLOAT_EQ(t[0], -1.0f);
  EXPECT_FLOAT_EQ(t[1], 5.0f);
  EXPECT_FLOAT_EQ(t[2], 10.0f);
  EXPECT_FLOAT_EQ(t[3], -1.0f);

  EXPECT_EQ(fp::gather(std::vector<int>{10, 20, 30}, std::vector<std::size_t>{2, 0}),
            (std::vector<int>{30, 10}));
}

TEST(Simd, ParMapInplace) {
  fp::ThreadPool pool(4);
  std::vector<int> v(1000);
  std::iota(v.begin(), v.end(), 0);
  fp::par_map_inplace(pool, v, [](fp::vec<int> x) { return x * fp::vec<int>(2); });
  EXPECT_EQ(v[999], 1998);
}

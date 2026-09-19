#include <fp/all.hpp>

#include <gtest/gtest.h>

TEST(Smoke, MapAndOps) {
  auto out = fp::map(std::vector<int>{1, 2, 3}, fp::plus(1));
  EXPECT_EQ(out, (std::vector<int>{2, 3, 4}));
}

TEST(Smoke, ResultChain) {
  auto r = fp::str::to_int("21") >>= [](int x) { return fp::ok(x * 2); };
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(r.value(), 42);
}

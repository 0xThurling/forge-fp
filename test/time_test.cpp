#include <fp/all.hpp>

#include <gtest/gtest.h>

TEST(Time, NowAndElapsed) {
  const double t0 = fp::now_seconds();
  const double t1 = fp::now_seconds();
  EXPECT_GE(t1, t0);
  EXPECT_GE(fp::elapsed_seconds(t0), 0.0);
}

TEST(Time, Stopwatch) {
  fp::Stopwatch sw;
  EXPECT_GE(sw.elapsed(), 0.0);
  EXPECT_GE(sw.lap(), 0.0);
  EXPECT_GE(sw.lap(), 0.0);
  sw.reset();
  EXPECT_GE(sw.elapsed(), 0.0);
}

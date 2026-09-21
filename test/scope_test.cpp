#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <stdexcept>
#include <utility>

TEST(Scope, DeferRunsOnExit) {
  bool ran = false;
  {
    auto guard = fp::defer([&] { ran = true; });
    EXPECT_FALSE(ran);
  }
  EXPECT_TRUE(ran);
}

TEST(Scope, ReleaseCancels) {
  bool ran = false;
  {
    auto guard = fp::defer([&] { ran = true; });
    guard.release();
  }
  EXPECT_FALSE(ran);
}

TEST(Scope, ScopeExitAlias) {
  int calls = 0;
  {
    auto guard = fp::scope_exit([&] { ++calls; });
  }
  EXPECT_EQ(calls, 1);
}

TEST(Scope, SuccessAndFail) {
  bool success = false;
  bool failed = false;
  {
    auto ok_guard = fp::scope_success([&] { success = true; });
    auto fail_guard = fp::scope_fail([&] { failed = true; });
  }
  EXPECT_TRUE(success);
  EXPECT_FALSE(failed);

  success = false;
  failed = false;
  EXPECT_THROW(
      {
        auto ok_guard = fp::scope_success([&] { success = true; });
        auto fail_guard = fp::scope_fail([&] { failed = true; });
        throw std::runtime_error("boom");
      },
      std::runtime_error);
  EXPECT_FALSE(success);
  EXPECT_TRUE(failed);
}

TEST(Scope, MovableAndRunsOnce) {
  int calls = 0;
  {
    auto outer = fp::defer([&] { ++calls; });
    auto inner = std::move(outer);
    (void)inner;
  }
  EXPECT_EQ(calls, 1);
}

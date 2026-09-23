// vec.hpp and ranges.hpp each provide the common combinators. Their results
// must be identical for the same input, so a typo in one implementation is
// caught here instead of only in the module that happens to be included alone.
#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {
std::vector<int> const input{3, 1, 2, 1, 4};
std::vector<int> const empty;
} // namespace

TEST(Parity, VecEqualsRanges) {
  // Same result whether the vector overload or the range overload runs: force
  // each by calling through a reference type they both accept.
  const std::vector<int> &v = input;

  EXPECT_EQ(fp::unique(v), fp::unique(std::vector<int>(v)));
  EXPECT_EQ(fp::sort(v), fp::sort(std::vector<int>(v)));
  EXPECT_EQ(fp::reverse(v), fp::reverse(std::vector<int>(v)));
  EXPECT_EQ(fp::take(v, 2), fp::take(std::vector<int>(v), 2));
  EXPECT_EQ(fp::drop(v, 2), fp::drop(std::vector<int>(v), 2));
  EXPECT_EQ(fp::map(v, [](int x) { return x * 2; }),
            fp::map(std::vector<int>(v), [](int x) { return x * 2; }));
  EXPECT_EQ(fp::filter(v, [](int x) { return x > 1; }),
            fp::filter(std::vector<int>(v), [](int x) { return x > 1; }));
  EXPECT_EQ(fp::flat_map(v, [](int x) { return std::vector<int>{x, x}; }),
            fp::flat_map(std::vector<int>(v), [](int x) { return std::vector<int>{x, x}; }));
  EXPECT_EQ(fp::take_while(v, [](int x) { return x > 1; }),
            fp::take_while(std::vector<int>(v), [](int x) { return x > 1; }));
  EXPECT_EQ(fp::drop_while(v, [](int x) { return x > 1; }),
            fp::drop_while(std::vector<int>(v), [](int x) { return x > 1; }));
  EXPECT_EQ(fp::sort_by(v, [](int x) { return -x; }),
            fp::sort_by(std::vector<int>(v), [](int x) { return -x; }));
  EXPECT_EQ(fp::enumerate(v).size(), fp::enumerate(std::vector<int>(v)).size());
  EXPECT_EQ(fp::minimum(v), fp::minimum(std::vector<int>(v)));
  EXPECT_EQ(fp::maximum(v), fp::maximum(std::vector<int>(v)));
  EXPECT_EQ(fp::scan(v, 0, [](int a, int b) { return a + b; }),
            fp::scan(std::vector<int>(v), 0, [](int a, int b) { return a + b; }));
  EXPECT_EQ(fp::chunk(v, 2), fp::chunk(std::vector<int>(v), 2));
  EXPECT_EQ(fp::span(v, [](int x) { return x > 1; }),
            fp::span(std::vector<int>(v), [](int x) { return x > 1; }));
  EXPECT_EQ(fp::partition(v, [](int x) { return x > 1; }),
            fp::partition(std::vector<int>(v), [](int x) { return x > 1; }));
  EXPECT_EQ(fp::zip(v, v), fp::zip(std::vector<int>(v), std::vector<int>(v)));
  EXPECT_EQ(fp::group_by(v, [](int x) { return x % 2; }).size(),
            fp::group_by(std::vector<int>(v), [](int x) { return x % 2; }).size());
  EXPECT_EQ(fp::all(v, [](int x) { return x > 0; }),
            fp::all(std::vector<int>(v), [](int x) { return x > 0; }));
  EXPECT_EQ(fp::any(v, [](int x) { return x == 2; }),
            fp::any(std::vector<int>(v), [](int x) { return x == 2; }));
  EXPECT_EQ(fp::none(v, [](int x) { return x > 9; }),
            fp::none(std::vector<int>(v), [](int x) { return x > 9; }));
  EXPECT_EQ(fp::concat(std::vector<std::vector<int>>{{1}, {2, 3}}),
            fp::concat(std::vector<std::vector<int>>{{1}, {2, 3}}));
  EXPECT_EQ(fp::to_vector(v), v);
}

TEST(Parity, BothHandleEmptyInputs) {
  const std::vector<int> &e = empty;
  EXPECT_TRUE(fp::unique(e).empty());
  EXPECT_TRUE(fp::sort(e).empty());
  EXPECT_TRUE(fp::filter(e, [](int) { return true; }).empty());
  EXPECT_TRUE(fp::windows(e, 3).empty());
  EXPECT_TRUE(fp::chunk(e, 2).empty());
  EXPECT_FALSE(fp::minimum(e).has_value());
  EXPECT_FALSE(fp::maximum(e).has_value());
  EXPECT_TRUE(fp::zip(e, e).empty());
  EXPECT_TRUE(fp::enumerate(e).empty());
  EXPECT_TRUE(fp::scan(e, 0, [](int a, int b) { return a + b; }).empty());
  const auto [head, tail] = fp::span(e, [](int) { return true; });
  EXPECT_TRUE(head.empty());
  EXPECT_TRUE(tail.empty());
}

// Instantiates every public API of vec.hpp with *only* vec.hpp included.
//
// This is the guard that would have caught the broken `fp::unique` overload:
// with more headers in scope another overload can silently win, so each module
// gets a TU of its own where its own overloads are the only candidates.
#include <fp/vec.hpp>

#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(ApiVec, MapFilterTakeDrop) {
  const std::vector<int> v{3, 1, 2, 1, 4};
  EXPECT_EQ(fp::map(v, [](int x) { return x * 2; }),
            (std::vector<int>{6, 2, 4, 2, 8}));
  EXPECT_EQ(fp::filter(v, [](int x) { return x > 1; }),
            (std::vector<int>{3, 2, 4}));
  EXPECT_EQ(fp::take(v, 2), (std::vector<int>{3, 1}));
  EXPECT_EQ(fp::drop(v, 2), (std::vector<int>{2, 1, 4}));
  EXPECT_EQ(fp::take_while(v, [](int x) { return x > 1; }),
            (std::vector<int>{3}));
  EXPECT_EQ(fp::drop_while(v, [](int x) { return x > 1; }),
            (std::vector<int>{1, 2, 1, 4}));
}

TEST(ApiVec, Accessors) {
  const std::vector<int> v{3, 1, 2};
  const std::vector<int> empty;
  EXPECT_EQ(fp::head(v), 3);
  EXPECT_EQ(fp::last(v), 2);
  EXPECT_EQ(fp::init(v), (std::vector<int>{3, 1}));
  EXPECT_EQ(fp::tail(v), (std::vector<int>{1, 2}));
  EXPECT_FALSE(fp::head(empty).has_value());
  EXPECT_FALSE(fp::last(empty).has_value());
  EXPECT_TRUE(fp::init(empty).empty());
  EXPECT_TRUE(fp::tail(empty).empty());
}

TEST(ApiVec, Transformations) {
  const std::vector<int> v{3, 1, 2, 1, 4};
  const std::vector<std::vector<int>> vv{{1}, {2, 3}};
  EXPECT_EQ(fp::reverse(v), (std::vector<int>{4, 1, 2, 1, 3}));
  EXPECT_EQ(fp::sort(v), (std::vector<int>{1, 1, 2, 3, 4}));
  EXPECT_EQ(fp::sort_by(v, [](int x) { return -x; }),
            (std::vector<int>{4, 3, 2, 1, 1}));
  // consecutive de-duplication (regression: the third argument used to be
  // passed in the predicate slot)
  EXPECT_EQ(fp::unique(v), (std::vector<int>{3, 1, 2, 1, 4}));
  const std::vector<int> dup{1, 1, 2, 3, 3, 3};
  EXPECT_EQ(fp::unique(dup), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(fp::chunk(v, 2), (std::vector<std::vector<int>>{{3, 1}, {2, 1}, {4}}));
  EXPECT_EQ(fp::chunk(v, 0), (std::vector<std::vector<int>>{}));
  EXPECT_EQ(fp::concat(vv), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(fp::enumerate(std::vector<std::string>{"a", "b"}).size(), 2u);
}

TEST(ApiVec, PredicatesAndFolds) {
  const std::vector<int> v{3, 1, 2};
  EXPECT_TRUE(fp::all(v, [](int x) { return x > 0; }));
  EXPECT_TRUE(fp::any(v, [](int x) { return x == 2; }));
  EXPECT_TRUE(fp::none(v, [](int x) { return x > 9; }));
  EXPECT_TRUE(fp::contains(v, 2));
  EXPECT_EQ(fp::count_if(v, [](int x) { return x > 1; }), 2u);
  EXPECT_EQ(fp::find(v, [](int x) { return x == 2; }), 2u);
  EXPECT_FALSE(fp::find(v, [](int x) { return x == 9; }).has_value());
  EXPECT_EQ(fp::sum(v), 6);
  EXPECT_EQ(fp::product(v), 6);
  EXPECT_EQ(fp::minimum(v), 1);
  EXPECT_EQ(fp::maximum(v), 3);
  EXPECT_FALSE(fp::minimum(std::vector<int>{}).has_value());
  EXPECT_EQ(fp::scan(v, 0, [](int a, int b) { return a + b; }),
            (std::vector<int>{3, 4, 6}));
  EXPECT_EQ(fp::zip_with(v, v, [](int a, int b) { return a + b; }),
            (std::vector<int>{6, 2, 4}));
  EXPECT_EQ(fp::zip3(v, v, v).size(), 3u);
  EXPECT_EQ(fp::zip_with3(v, v, v, [](int a, int b, int c) { return a + b + c; }),
            (std::vector<int>{9, 3, 6}));
}

TEST(ApiVec, GroupingAndPairs) {
  const std::vector<int> v{1, 2, 3, 4};
  const auto [lo, hi] = fp::span(v, [](int x) { return x < 3; });
  EXPECT_EQ(lo, (std::vector<int>{1, 2}));
  EXPECT_EQ(hi, (std::vector<int>{3, 4}));
  const auto [yes, no] = fp::partition(v, [](int x) { return x % 2 == 0; });
  EXPECT_EQ(yes, (std::vector<int>{2, 4}));
  EXPECT_EQ(no, (std::vector<int>{1, 3}));
  EXPECT_EQ(fp::group_by(v, [](int x) { return x % 2; }).size(), 2u);
  const auto [as, bs] = fp::unzip(fp::zip(v, std::vector<std::string>{"a", "b", "c", "d"}));
  EXPECT_EQ(as, v);
  EXPECT_EQ(bs, (std::vector<std::string>{"a", "b", "c", "d"}));
}

TEST(ApiVec, Generators) {
  EXPECT_EQ(fp::replicate(3, 7), (std::vector<int>{7, 7, 7}));
  EXPECT_EQ(fp::range(0, 4), (std::vector<int>{0, 1, 2, 3}));
  EXPECT_TRUE(fp::range(0, 4, 0).empty());
  EXPECT_EQ(fp::intersperse(std::vector<int>{1, 2}, 0),
            (std::vector<int>{1, 0, 2}));
  EXPECT_EQ(fp::intercalate(std::vector<std::vector<int>>{{1}, {2}},
                            std::vector<int>{0}),
            (std::vector<int>{1, 0, 2}));
  EXPECT_EQ(fp::flat_map(std::vector<int>{1, 2},
                         [](int x) { return std::vector<int>{x, x}; }),
            (std::vector<int>{1, 1, 2, 2}));
}

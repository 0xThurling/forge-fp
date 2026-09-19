#include "fp/ranges.hpp"
#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {
std::vector<int> nums() { return {3, 1, 4, 1, 5, 9, 2, 6}; }
} // namespace

TEST(Vec, MapFilterZip) {
  EXPECT_EQ(fp::map(nums(), [](int x) { return x * 2; }),
            (std::vector<int>{6, 2, 8, 2, 10, 18, 4, 12}));
  EXPECT_EQ(fp::filter(nums(), fp::gt(4)), (std::vector<int>{5, 9, 6}));
  EXPECT_EQ(fp::zip(std::vector<int>{1, 2, 3}, std::vector<std::string>{"a", "b"}),
            (std::vector<std::pair<int, std::string>>{{1, "a"}, {2, "b"}}));
}

TEST(Vec, HeadLastTailInit) {
  EXPECT_EQ(fp::head(nums()).value(), 3);
  EXPECT_FALSE(fp::head(std::vector<int>{}).has_value());
  EXPECT_EQ(fp::last(nums()).value(), 6);
  EXPECT_EQ(fp::tail(nums()), (std::vector<int>{1, 4, 1, 5, 9, 2, 6}));
  EXPECT_EQ(fp::init(nums()), (std::vector<int>{3, 1, 4, 1, 5, 9, 2}));
}

TEST(Vec, PartitionGroupChunk) {
  auto [evens, odds] = fp::partition(nums(), [](int x) { return x % 2 == 0; });
  EXPECT_EQ(evens, (std::vector<int>{4, 2, 6}));
  EXPECT_EQ(odds, (std::vector<int>{3, 1, 1, 5, 9}));

  auto g = fp::group_by(std::vector<int>{1, 2, 3, 4}, [](int x) { return x % 2; });
  EXPECT_EQ(g[0], (std::vector<int>{2, 4}));
  EXPECT_EQ(g[1], (std::vector<int>{1, 3}));

  EXPECT_EQ(fp::chunk(std::vector<int>{1, 2, 3, 4, 5}, 2),
            (std::vector<std::vector<int>>{{1, 2}, {3, 4}, {5}}));
}

TEST(Vec, ZipWithAndThreeWay) {
  EXPECT_EQ(fp::zip_with(std::vector<int>{1, 2}, std::vector<int>{10, 20}, std::plus<>{}),
            (std::vector<int>{11, 22}));
  auto z3 = fp::zip3(std::vector<int>{1}, std::vector<std::string>{"a"}, std::vector<double>{2.5});
  EXPECT_EQ(std::get<0>(z3[0]), 1);
  EXPECT_EQ(fp::zip_with3(std::vector<int>{1, 2}, std::vector<int>{10, 20},
                          std::vector<int>{100, 200},
                          [](int a, int b, int c) { return a + b + c; }),
            (std::vector<int>{111, 222}));
  auto [as, bs] = fp::unzip(std::vector<std::pair<int, std::string>>{{1, "a"}, {2, "b"}});
  EXPECT_EQ(as, (std::vector<int>{1, 2}));
  EXPECT_EQ(bs, (std::vector<std::string>{"a", "b"}));
}

TEST(Vec, TakeDropWhile) {
  EXPECT_EQ(fp::take(nums(), 3), (std::vector<int>{3, 1, 4}));
  EXPECT_EQ(fp::drop(nums(), 6), (std::vector<int>{2, 6}));
  EXPECT_EQ(fp::take_while(std::vector<int>{1, 2, 3, 0, 4}, fp::lt(3)),
            (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::drop_while(std::vector<int>{1, 2, 3, 0, 4}, fp::lt(3)),
            (std::vector<int>{3, 0, 4}));
}

TEST(Vec, SortReverseUnique) {
  EXPECT_EQ(fp::reverse(std::vector<int>{1, 2, 3}), (std::vector<int>{3, 2, 1}));
  EXPECT_EQ(fp::sort(std::vector<int>{3, 1, 2}), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(fp::sort_by(std::vector<std::string>{"bb", "a", "ccc"},
                        [](std::string const &s) { return s.size(); }),
            (std::vector<std::string>{"a", "bb", "ccc"}));
  EXPECT_EQ(fp::unique(std::vector<int>{1, 1, 2, 3, 3}), (std::vector<int>{1, 2, 3}));
}

TEST(Vec, PredicatesAndSearch) {
  EXPECT_TRUE(fp::all(nums(), fp::gt(0)));
  EXPECT_FALSE(fp::all(nums(), fp::gt(1)));
  EXPECT_TRUE(fp::any(nums(), fp::gt(8)));
  EXPECT_FALSE(fp::none(nums(), fp::gt(8)));
  EXPECT_EQ(fp::find(nums(), fp::eq(5)).value(), 4u);
  EXPECT_FALSE(fp::find(nums(), fp::eq(100)).has_value());
  EXPECT_TRUE(fp::contains(nums(), 9));
  EXPECT_EQ(fp::count_if(nums(), fp::gt(3)), 4u);
}

TEST(Vec, FlatteningAndEnumeration) {
  EXPECT_EQ(fp::concat(std::vector<std::vector<int>>{{1, 2}, {3}}), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(fp::flat_map(std::vector<int>{1, 2}, [](int x) { return std::vector<int>{x, -x}; }),
            (std::vector<int>{1, -1, 2, -2}));
  EXPECT_EQ(fp::enumerate(std::vector<std::string>{"a", "b"}),
            (std::vector<std::pair<size_t, std::string>>{{0, "a"}, {1, "b"}}));
}

TEST(Vec, FoldsAndAggregates) {
  EXPECT_EQ(fp::fold_left(nums(), 0, std::plus<>{}), 31);
  EXPECT_EQ(fp::fold_right(nums(), 0, std::plus<>{}), 31);
  EXPECT_EQ(fp::sum(std::vector<int>{1, 2, 3}), 6);
  EXPECT_EQ(fp::product(std::vector<int>{2, 3, 4}), 24);
  EXPECT_EQ(fp::maximum(nums()).value(), 9);
  EXPECT_EQ(fp::minimum(nums()).value(), 1);
  EXPECT_FALSE(fp::maximum(std::vector<int>{}).has_value());
}

TEST(Vec, SpanScanAndFriends) {
  auto [small, big] = fp::span(nums(), fp::lt(5));
  EXPECT_EQ(small, (std::vector<int>{3, 1, 4, 1}));
  EXPECT_EQ(big, (std::vector<int>{5, 9, 2, 6}));

  EXPECT_EQ(fp::scan(std::vector<int>{1, 2, 3}, 0, std::plus<>{}),
            (std::vector<int>{1, 3, 6}));
  EXPECT_EQ(fp::intersperse(std::vector<int>{1, 2, 3}, 0), (std::vector<int>{1, 0, 2, 0, 3}));
  EXPECT_EQ(fp::intercalate(std::vector<std::vector<int>>{{1}, {2}}, std::vector<int>{0}),
            (std::vector<int>{1, 0, 2}));
  EXPECT_EQ(fp::replicate(3, std::string("x")),
            (std::vector<std::string>{"x", "x", "x"}));
}

TEST(Vec, Range) {
  EXPECT_EQ(fp::range(0, 5), (std::vector<int>{0, 1, 2, 3, 4}));
  EXPECT_EQ(fp::range(0, 10, 3), (std::vector<int>{0, 3, 6, 9}));
  EXPECT_TRUE(fp::range(5, 5).empty());
}

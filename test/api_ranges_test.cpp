// Instantiates every public API of ranges.hpp with *only* ranges.hpp included,
// so its overloads are the only candidates (see api_vec_test.cpp for why).
#include <fp/ranges.hpp>

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {
std::vector<int> const input{3, 1, 2, 1, 4};
}

TEST(ApiRanges, EagerCombinators) {
  EXPECT_EQ(fp::map(input, [](int x) { return x * 2; }),
            (std::vector<int>{6, 2, 4, 2, 8}));
  EXPECT_EQ(fp::filter(input, [](int x) { return x > 1; }),
            (std::vector<int>{3, 2, 4}));
  EXPECT_EQ(fp::take(input, 2), (std::vector<int>{3, 1}));
  EXPECT_EQ(fp::drop(input, 2), (std::vector<int>{2, 1, 4}));
  EXPECT_EQ(fp::take_while(input, [](int x) { return x > 1; }),
            (std::vector<int>{3}));
  EXPECT_EQ(fp::drop_while(input, [](int x) { return x > 1; }),
            (std::vector<int>{1, 2, 1, 4}));
  EXPECT_EQ(fp::unique(input), (std::vector<int>{3, 1, 2, 1, 4}));
  EXPECT_EQ(fp::unique(std::vector<int>{1, 1, 2}), (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::sort(input), (std::vector<int>{1, 1, 2, 3, 4}));
  EXPECT_EQ(fp::sort_by(input, [](int x) { return -x; }),
            (std::vector<int>{4, 3, 2, 1, 1}));
  EXPECT_EQ(fp::reverse(input), (std::vector<int>{4, 1, 2, 1, 3}));
  EXPECT_EQ(fp::to_vector(input), input);
}

TEST(ApiRanges, FoldsAndScans) {
  EXPECT_EQ(fp::fold_left(input, 0, [](int a, int b) { return a + b; }), 11);
  EXPECT_EQ(fp::fold_right(input, 0, [](int a, int b) { return a + b; }), 11);
  EXPECT_EQ(fp::scan(input, 0, [](int a, int b) { return a + b; }),
            (std::vector<int>{3, 4, 6, 7, 11}));
  EXPECT_EQ(fp::sum(input), 11);
  EXPECT_EQ(fp::minimum(input), 1);
  EXPECT_EQ(fp::maximum(input), 4);
  EXPECT_FALSE(fp::minimum(std::vector<int>{}).has_value());
  EXPECT_TRUE(fp::all(input, [](int x) { return x > 0; }));
  EXPECT_TRUE(fp::any(input, [](int x) { return x == 2; }));
  EXPECT_TRUE(fp::none(input, [](int x) { return x > 9; }));
  EXPECT_EQ(fp::count(input, 1), 2u);
  EXPECT_EQ(fp::count(input, [](int x) { return x > 1; }), 3u);
}

TEST(ApiRanges, ChunkWindowsSpanPartition) {
  EXPECT_EQ(fp::chunk(input, 2).size(), 3u);
  EXPECT_TRUE(fp::chunk(input, 0).empty());

  // windows: n == 0 yields nothing; a ring buffer keeps the window order
  EXPECT_TRUE(fp::windows(input, 0).empty());
  const auto w = fp::windows(input, 3);
  ASSERT_EQ(w.size(), 3u);
  EXPECT_EQ(w[0], (std::vector<int>{3, 1, 2}));
  EXPECT_EQ(w[1], (std::vector<int>{1, 2, 1}));
  EXPECT_EQ(w[2], (std::vector<int>{2, 1, 4}));
  EXPECT_TRUE(fp::windows(input, 99).empty());

  const auto [lo, hi] = fp::span(input, [](int x) { return x > 1; });
  EXPECT_EQ(lo, (std::vector<int>{3}));
  EXPECT_EQ(hi, (std::vector<int>{1, 2, 1, 4}));

  const auto [yes, no] = fp::partition(input, [](int x) { return x > 1; });
  EXPECT_EQ(yes, (std::vector<int>{3, 2, 4}));
  EXPECT_EQ(no, (std::vector<int>{1, 1}));
}

TEST(ApiRanges, ZipEnumerateFlatMapFilterMap) {
  const std::vector<std::string> s{"a", "b", "c", "d", "e"};
  EXPECT_EQ(fp::zip(input, s).size(), 5u);
  EXPECT_EQ(fp::enumerate(input).size(), 5u);
  EXPECT_EQ(fp::flat_map(input, [](int x) { return std::vector<int>{x, x}; }).size(),
            10u);
  const auto kept = fp::filter_map(input, [](int x) -> std::optional<int> {
    return x > 2 ? std::optional<int>(x) : std::nullopt;
  });
  EXPECT_EQ(kept, (std::vector<int>{3, 4}));
  EXPECT_EQ(fp::group_by(input, [](int x) { return x % 2; }).size(), 2u);
  EXPECT_EQ(fp::concat(std::vector<std::vector<int>>{{1}, {2, 3}}),
            (std::vector<int>{1, 2, 3}));
}

TEST(ApiRanges, CurriedForms) {
  // The closure forms need only ranges.hpp: `closure(range)`.
  const auto gt1 = [](int x) { return x > 1; };
  const auto plus1 = [](int x) { return x + 1; };
  EXPECT_EQ(fp::filter(gt1)(input), (std::vector<int>{3, 2, 4}));
  EXPECT_EQ(fp::map(plus1)(input), (std::vector<int>{4, 2, 3, 2, 5}));
  EXPECT_EQ(fp::take(2)(input), (std::vector<int>{3, 1}));
  EXPECT_EQ(fp::drop(2)(input), (std::vector<int>{2, 1, 4}));
  EXPECT_EQ(fp::take_while(gt1)(input), (std::vector<int>{3}));
  EXPECT_EQ(fp::drop_while(gt1)(input), (std::vector<int>{1, 2, 1, 4}));
  EXPECT_EQ(fp::unique()(input), (std::vector<int>{3, 1, 2, 1, 4}));
  EXPECT_EQ(fp::reverse()(input), (std::vector<int>{4, 1, 2, 1, 3}));
  EXPECT_EQ(fp::sort()(input), (std::vector<int>{1, 1, 2, 3, 4}));
  EXPECT_EQ(fp::chunk(2)(input).size(), 3u);
  EXPECT_EQ(fp::enumerate()(input).size(), 5u);
  EXPECT_EQ(fp::scan(0, [](int a, int b) { return a + b; })(input),
            (std::vector<int>{3, 4, 6, 7, 11}));
  EXPECT_EQ(fp::fold_left(0, [](int a, int b) { return a + b; })(input), 11);
  EXPECT_EQ(fp::fold_right(0, [](int a, int b) { return a + b; })(input), 11);
  const auto parts = fp::partition(gt1)(input);
  EXPECT_EQ(parts.first, (std::vector<int>{3, 2, 4}));
  const auto sp = fp::span(gt1)(input);
  EXPECT_EQ(sp.first, (std::vector<int>{3}));
  EXPECT_EQ(fp::flat_map([](int x) { return std::vector<int>{x}; })(input),
            input);
  EXPECT_EQ(fp::filter_map([](int x) -> std::optional<int> { return x; })(input),
            input);
  EXPECT_EQ(fp::group_by([](int x) { return x % 2; })(input).size(), 2u);
  EXPECT_EQ(fp::sort_by([](int x) { return -x; })(input),
            (std::vector<int>{4, 3, 2, 1, 1}));
  EXPECT_EQ(fp::zip_with(std::vector<int>{1, 1, 1, 1, 1},
                         [](int a, int b) { return a + b; })(input),
            (std::vector<int>{4, 2, 3, 2, 5}));
}

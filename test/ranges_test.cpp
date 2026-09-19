#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <array>
#include <list>
#include <ranges>
#include <string>
#include <vector>

TEST(Ranges, WorkOnAnyRange) {
  std::array<int, 4> a = {1, 2, 3, 4};
  EXPECT_EQ(fp::to_vector(fp::map(a, [](int x) { return x * 2; })),
            (std::vector<int>{2, 4, 6, 8}));

  std::list<int> l = {1, 2, 3, 4};
  EXPECT_EQ(fp::to_vector(fp::filter(l, fp::gt(2))), (std::vector<int>{3, 4}));

  auto iota = std::views::iota(0, 5);
  EXPECT_EQ(fp::to_vector(fp::map(iota, [](int x) { return x + 1; })),
            (std::vector<int>{1, 2, 3, 4, 5}));
  EXPECT_EQ(fp::fold_left(iota, 0, std::plus<>{}), 10);
}

TEST(Ranges, EagerSliceAndCombine) {
  std::vector<int> v = {1, 2, 3, 4, 5};
  EXPECT_EQ(fp::to_vector(fp::take(v, 2)), (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::to_vector(fp::drop(v, 3)), (std::vector<int>{4, 5}));
  EXPECT_EQ(fp::to_vector(fp::take_while(v, fp::lt(4))), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(fp::to_vector(fp::drop_while(v, fp::lt(4))), (std::vector<int>{4, 5}));
  EXPECT_EQ(fp::to_vector(fp::concat(std::vector<std::vector<int>>{{1}, {2, 3}})),
            (std::vector<int>{1, 2, 3}));
}

TEST(Ranges, SearchPredicatesAndCount) {
  std::vector<int> v = {1, 2, 3, 4};
  EXPECT_TRUE(fp::all(v, fp::gt(0)));
  EXPECT_TRUE(fp::any(v, fp::eq(3)));
  EXPECT_TRUE(fp::none(v, fp::eq(9)));
  EXPECT_EQ(fp::count(v, fp::gt(2)), 2u);
  EXPECT_EQ(fp::count(v, 3), 1u);
}

TEST(Ranges, ShapeChanging) {
  std::vector<int> v = {1, 2, 3, 4};
  EXPECT_EQ(fp::to_vector(fp::flat_map(v, [](int x) { return std::vector<int>{x, x}; })),
            (std::vector<int>{1, 1, 2, 2, 3, 3, 4, 4}));
  EXPECT_EQ(fp::to_vector(fp::filter_map(v, [](int x) -> std::optional<int> {
                            return x % 2 == 0 ? std::optional<int>(x) : std::nullopt;
                          })),
            (std::vector<int>{2, 4}));

  auto g = fp::group_by(v, [](int x) { return x % 2; });
  EXPECT_EQ(g[0], (std::vector<int>{2, 4}));

  auto zipped = fp::zip(v, std::vector<std::string>{"a", "b", "c", "d"});
  EXPECT_EQ(zipped.size(), 4u);
  EXPECT_EQ(zipped[0], std::make_pair(1, std::string("a")));

  auto en = fp::enumerate(v);
  EXPECT_EQ(en[2], std::make_pair(size_t{2}, 3));

  EXPECT_EQ(fp::to_vector(fp::chunk(v, 3)),
            (std::vector<std::vector<int>>{{1, 2, 3}, {4}}));
  EXPECT_EQ(fp::to_vector(fp::windows(v, 2)),
            (std::vector<std::vector<int>>{{1, 2}, {2, 3}, {3, 4}}));
}

TEST(Ranges, FoldsScanMinMax) {
  std::vector<int> v = {3, 1, 4, 1, 5};
  EXPECT_EQ(fp::fold_left(v, 0, std::plus<>{}), 14);
  EXPECT_EQ(fp::fold_right(v, 0, std::plus<>{}), 14);
  EXPECT_EQ(fp::scan(v, 0, std::plus<>{}), (std::vector<int>{3, 4, 8, 9, 14}));
  EXPECT_EQ(fp::minimum(v).value(), 1);
  EXPECT_EQ(fp::maximum(v).value(), 5);
}

TEST(Ranges, SortingUniquePartitionSpan) {
  std::vector<int> v = {3, 3, 1, 3, 2};
  EXPECT_EQ(fp::to_vector(fp::unique(v)), (std::vector<int>{3, 1, 3, 2}));
  EXPECT_EQ(fp::to_vector(fp::sort(v)), (std::vector<int>{1, 2, 3, 3, 3}));
  EXPECT_EQ(fp::to_vector(fp::sort_by(v, [](int x) { return -x; })),
            (std::vector<int>{3, 3, 3, 2, 1}));

  auto [evens, odds] = fp::partition(v, [](int x) { return x % 2 == 0; });
  EXPECT_EQ(fp::to_vector(evens), (std::vector<int>{2}));
  EXPECT_EQ(fp::to_vector(odds), (std::vector<int>{3, 3, 1, 3}));

  std::vector<int> w = {1, 2, 3, 3};
  auto [head, rest] = fp::span(w, fp::lt(3));
  EXPECT_EQ(fp::to_vector(head), (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::to_vector(rest), (std::vector<int>{3, 3}));
  (void)evens;
  (void)odds;
}

TEST(Ranges, CurriedPipeline) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6};
  auto r = fp::out(fp::into(v) | fp::filter([](int x) { return x % 2 == 0; }) |
                   fp::map([](int x) { return x * 10; }));
  EXPECT_EQ(r, (std::vector<int>{20, 40, 60}));

  auto r2 = fp::out(fp::into(v) | fp::take(2) | fp::reverse());
  EXPECT_EQ(r2, (std::vector<int>{2, 1}));

  auto r3 = fp::out(fp::into(v) | fp::drop_while(fp::lt(3)) | fp::sort_by([](int x) { return -x; }));
  EXPECT_EQ(r3, (std::vector<int>{6, 5, 4, 3}));

  auto r4 = fp::out(fp::into(v) | fp::take_while(fp::lt(4)) | fp::enumerate() | fp::unique());
  EXPECT_EQ(r4.size(), 3u);

  auto sum = fp::out(fp::into(v) | fp::fold_left(0, std::plus<>{}));
  EXPECT_EQ(sum, 21);

  auto groups = fp::out(fp::into(v) | fp::group_by([](int x) { return x % 3; }));
  EXPECT_EQ(groups[0].size(), 2u);

  auto chunks = fp::out(fp::into(v) | fp::chunk(4));
  EXPECT_EQ(chunks.size(), 2u);

  auto scanned = fp::out(fp::into(v) | fp::scan(0, std::plus<>{}));
  EXPECT_EQ(scanned.back(), 21);

  auto zipped = fp::out(fp::into(v) | fp::zip_with(std::vector<int>{1, 1, 1}, std::plus<>{}));
  EXPECT_EQ(zipped, (std::vector<int>{2, 3, 4}));

  auto folded = fp::out(fp::into(v) | fp::fold_right(0, std::plus<>{}));
  EXPECT_EQ(folded, 21);
}

TEST(Ranges, CurriedFilterMapFlatMapPartitionSpan) {
  std::vector<int> v = {1, 2, 3, 4};
  auto fm = fp::out(fp::into(v) | fp::filter_map([](int x) -> std::optional<int> {
                      return x % 2 ? std::optional<int>(x) : std::nullopt;
                    }));
  EXPECT_EQ(fm, (std::vector<int>{1, 3}));

  auto ff = fp::out(fp::into(v) | fp::flat_map([](int x) { return std::vector<int>{x, x}; }));
  EXPECT_EQ(ff.size(), 8u);

  auto [evens, odds] = fp::out(fp::into(v) | fp::partition([](int x) { return x % 2 == 0; }));
  EXPECT_EQ(fp::to_vector(evens), (std::vector<int>{2, 4}));
  EXPECT_EQ(fp::to_vector(odds), (std::vector<int>{1, 3}));

  auto [lo, hi] = fp::out(fp::into(v) | fp::span(fp::lt(3)));
  EXPECT_EQ(fp::to_vector(lo), (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::to_vector(hi), (std::vector<int>{3, 4}));
}

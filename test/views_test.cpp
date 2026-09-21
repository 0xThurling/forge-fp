#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

TEST(Views, Laziness) {
  auto calls = std::make_shared<int>(0);
  std::vector<int> v = {1, 2, 3, 4};

  auto view = v | fp::views::map([calls](int x) {
                ++*calls;
                return x * 2;
              });
  EXPECT_EQ(*calls, 0); // nothing has run yet

  EXPECT_EQ(*std::ranges::begin(view), 2);
  EXPECT_EQ(*calls, 1);

  // Views recompute on each pass; the earlier deref is not cached.
  EXPECT_EQ(fp::to_vector(view), (std::vector<int>{2, 4, 6, 8}));
  EXPECT_EQ(*calls, 5);
}

TEST(Views, MapFilterTakeDrop) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6};

  EXPECT_EQ(fp::to_vector(v | fp::views::map(fp::times(10))),
            (std::vector<int>{10, 20, 30, 40, 50, 60}));
  EXPECT_EQ(fp::to_vector(v | fp::views::filter([](int x) { return x % 2 == 0; })),
            (std::vector<int>{2, 4, 6}));
  EXPECT_EQ(fp::to_vector(v | fp::views::filter(fp::gt(3))),
            (std::vector<int>{4, 5, 6}));
  EXPECT_EQ(fp::to_vector(fp::views::take(v, 2)), (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::to_vector(fp::views::drop(v, 4)), (std::vector<int>{5, 6}));
  EXPECT_EQ(fp::to_vector(v | fp::views::take_while([](int x) { return x < 4; })),
            (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(fp::to_vector(v | fp::views::drop_while([](int x) { return x < 4; })),
            (std::vector<int>{4, 5, 6}));
}

TEST(Views, FilterMap) {
  std::vector<int> v = {1, 2, 3, 4};
  auto evens = v | fp::views::filter_map([](int x) -> std::optional<int> {
                 return x % 2 == 0 ? std::optional<int>(x) : std::nullopt;
               });
  EXPECT_EQ(fp::to_vector(evens), (std::vector<int>{2, 4}));
}

TEST(Views, FlatMap) {
  std::vector<int> v = {1, 2, 3};
  auto flat =
      v | fp::views::flat_map([](int x) { return std::vector<int>{x, -x}; });
  EXPECT_EQ(fp::to_vector(flat), (std::vector<int>{1, -1, 2, -2, 3, -3}));
}

TEST(Views, Enumerate) {
  std::vector<std::string> names = {"a", "b", "c"};
  auto en = fp::views::enumerate(names);
  std::vector<std::pair<std::size_t, std::string>> got(en.begin(), en.end());
  EXPECT_EQ(got, (std::vector<std::pair<std::size_t, std::string>>{
                     {0, "a"}, {1, "b"}, {2, "c"}}));

  // closure form through the fp pipe
  auto e2 = fp::out(fp::into(names) | fp::views::enumerate());
  std::vector<std::pair<std::size_t, std::string>> got2(e2.begin(), e2.end());
  EXPECT_EQ(got.size(), got2.size());
}

TEST(Views, Zip) {
  std::vector<int> a = {1, 2, 3};
  std::vector<std::string> b = {"x", "y", "z", "w"};

  std::vector<std::pair<int, std::string>> got;
  for (auto &&p : fp::views::zip(a, b))
    got.push_back(p);
  EXPECT_EQ(got, (std::vector<std::pair<int, std::string>>{
                     {1, "x"}, {2, "y"}, {3, "z"}}));

  // closure form captures `other` by value
  auto z = fp::out(fp::into(a) | fp::views::zip(b));
  std::vector<std::pair<int, std::string>> got2(z.begin(), z.end());
  EXPECT_EQ(got2, got);

  // heterogeneous ranges with different iterator types are fine
  std::array<int, 2> arr = {7, 8};
  std::vector<int> vc = {1, 2, 3};
  std::vector<std::pair<int, int>> got3;
  for (auto &&p : fp::views::zip(vc, arr))
    got3.push_back(p);
  EXPECT_EQ(got3, (std::vector<std::pair<int, int>>{{1, 7}, {2, 8}}));
}

TEST(Views, ReverseAndJoin) {
  std::vector<int> v = {1, 2, 3};
  EXPECT_EQ(fp::to_vector(v | fp::views::reverse), (std::vector<int>{3, 2, 1}));

  std::vector<std::vector<int>> nested = {{1, 2}, {3}};
  EXPECT_EQ(fp::to_vector(nested | fp::views::join), (std::vector<int>{1, 2, 3}));
}

TEST(Views, IotaPipeline) {
  auto squares = std::views::iota(1) | fp::views::map([](int x) { return x * x; }) |
                 fp::views::take(5);
  EXPECT_EQ(fp::to_vector(squares), (std::vector<int>{1, 4, 9, 16, 25}));
}

TEST(Views, IntoPipeComposition) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6};
  auto r = fp::out(fp::into(v) |
                   fp::views::filter([](int x) { return x % 2 == 0; }) |
                   fp::views::map([](int x) { return x * 10; }));
  EXPECT_EQ(fp::to_vector(r), (std::vector<int>{20, 40, 60}));
}

TEST(Views, OwningFromRvalue) {
  // The pipeline owns the vector, so the result outlives the expression.
  auto r = fp::out(fp::into(std::vector<int>{1, 2, 3, 4}) |
                   fp::views::filter([](int x) { return x > 2; }));
  EXPECT_EQ(fp::to_vector(r), (std::vector<int>{3, 4}));
}

TEST(Views, LazyWithFold) {
  std::vector<int> v = {1, 2, 3, 4, 5};
  auto sum = fp::fold_left(
      v | fp::views::map([](int x) { return x * 2; }) |
          fp::views::filter([](int x) { return x != 6; }),
      0, std::plus<>{});
  EXPECT_EQ(sum, 24);
}

TEST(Views, IotaIsLazy) {
  // fp::views::iota is a lazy integer range: no allocation, no materialization.
  std::size_t count = 0;
  for ([[maybe_unused]] int i : fp::views::iota(0, 5))
    ++count;
  EXPECT_EQ(count, 5u);

  auto squares = fp::views::iota(1, 6) | fp::views::map([](int x) { return x * x; });
  EXPECT_EQ(fp::to_vector(squares), (std::vector<int>{1, 4, 9, 16, 25}));
}

TEST(Views, Chunk) {
  std::vector<int> v = {1, 2, 3, 4, 5};

  std::vector<std::vector<int>> chunks;
  for (auto part : v | fp::views::chunk(2))
    chunks.emplace_back(part.begin(), part.end());
  EXPECT_EQ(chunks, (std::vector<std::vector<int>>{{1, 2}, {3, 4}, {5}}));

  auto via_into = fp::out(fp::into(v) | fp::views::chunk(3));
  std::vector<std::vector<int>> got;
  for (auto part : via_into)
    got.emplace_back(part.begin(), part.end());
  EXPECT_EQ(got, (std::vector<std::vector<int>>{{1, 2, 3}, {4, 5}}));
}

TEST(Views, Slide) {
  std::vector<int> v = {1, 2, 3, 4};
  std::vector<std::vector<int>> windows;
  for (auto w : v | fp::views::slide(3))
    windows.emplace_back(w.begin(), w.end());
  EXPECT_EQ(windows, (std::vector<std::vector<int>>{{1, 2, 3}, {2, 3, 4}}));
}

TEST(Views, Stride) {
  std::vector<int> v = {0, 1, 2, 3, 4, 5, 6};
  EXPECT_EQ(fp::to_vector(v | fp::views::stride(2)),
            (std::vector<int>{0, 2, 4, 6}));
  EXPECT_TRUE(fp::to_vector(v | fp::views::stride(0)).empty());
}

TEST(Views, TakeLast) {
  std::vector<int> v = {1, 2, 3, 4, 5};
  EXPECT_EQ(fp::to_vector(fp::views::take_last(v, 2)),
            (std::vector<int>{4, 5}));
  EXPECT_EQ(fp::to_vector(fp::views::take_last(v, 99)), v);
}

TEST(Views, ZipWith) {
  std::vector<int> a = {1, 2, 3};
  std::vector<int> b = {10, 20, 30, 40};
  auto sums = fp::out(fp::into(a) | fp::views::zip_with(b, [](int x, int y) {
                        return x + y;
                      }));
  EXPECT_EQ(fp::to_vector(sums), (std::vector<int>{11, 22, 33}));
}

TEST(Views, Zip3) {
  std::vector<int> a = {1, 2, 3};
  std::vector<std::string> b = {"x", "y"};
  std::vector<double> c = {0.5, 1.5, 2.5};

  std::vector<std::tuple<int, std::string, double>> got;
  for (auto t : fp::views::zip3(a, b, c))
    got.push_back(t);

  EXPECT_EQ(got.size(), 2u);
  EXPECT_EQ(std::get<0>(got[1]), 2);
  EXPECT_EQ(std::get<1>(got[0]), "x");
  EXPECT_DOUBLE_EQ(std::get<2>(got[1]), 1.5);
}

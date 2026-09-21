#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

TEST(Inplace, ForEachAndIndex) {
  std::vector<int> v{1, 2, 3};
  int sum = 0;
  fp::for_each(v, [&](int x) { sum += x; });
  EXPECT_EQ(sum, 6);

  fp::for_each_index(v, [&](std::size_t i, int x) { v[i] = x * 10; });
  EXPECT_EQ(v, (std::vector<int>{10, 20, 30}));
}

TEST(Inplace, TransformAndFill) {
  std::vector<int> v{1, 2, 3};
  fp::transform_inplace(v, [](int x) { return x + 1; });
  EXPECT_EQ(v, (std::vector<int>{2, 3, 4}));

  fp::fill(v, 7);
  EXPECT_EQ(v, (std::vector<int>{7, 7, 7}));
}

TEST(Inplace, ZipForEach) {
  std::vector<int> a{1, 2, 3};
  const std::vector<int> b{10, 20};

  int sum = 0;
  fp::zip_for_each(a, b, [&](int x, int y) { sum += x * y; });
  EXPECT_EQ(sum, 10 + 40);

  std::vector<int> c{1, 2, 3};
  fp::zip_transform_inplace(c, b, [](int x, int y) { return x + y; });
  EXPECT_EQ(c, (std::vector<int>{11, 22, 3}));

  int sum3 = 0;
  fp::zip3_for_each(a, b, c, [&](int x, int y, int z) { sum3 += x + y + z; });
  EXPECT_EQ(sum3, (1 + 10 + 11) + (2 + 20 + 22));
}

TEST(Inplace, MapTo) {
  const std::vector<int> src{1, 2, 3};
  std::vector<int> dst(3, 0);
  fp::map_to(src, dst, [](int x) { return x * x; });
  EXPECT_EQ(dst, (std::vector<int>{1, 4, 9}));
}

TEST(Inplace, SortReverseUnique) {
  std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};
  fp::sort_inplace(v);
  EXPECT_EQ(v, (std::vector<int>{1, 1, 2, 3, 4, 5, 6, 9}));

  fp::reverse_inplace(v);
  EXPECT_EQ(v.front(), 9);

  fp::unique_inplace(v);
  EXPECT_EQ(v, (std::vector<int>{9, 6, 5, 4, 3, 2, 1}));

  fp::remove_if_inplace(v, [](int x) { return x % 2 == 0; });
  EXPECT_EQ(v, (std::vector<int>{9, 5, 3, 1}));
}

TEST(Inplace, SortByInplace) {
  std::vector<std::pair<int, std::string>> v{{2, "b"}, {1, "a"}, {3, "c"}};
  fp::sort_by_inplace(v, [](auto const &p) { return p.first; });
  EXPECT_EQ(v.front().second, "a");
  EXPECT_EQ(v.back().second, "c");

  fp::stable_sort_by_inplace(v, [](auto const &p) { return p.first; });
  EXPECT_EQ(v.front().second, "a");
}

TEST(Inplace, WorksOnStrings) {
  std::string s = "dbca";
  fp::sort_inplace(s);
  EXPECT_EQ(s, "abcd");
  fp::reverse_inplace(s);
  EXPECT_EQ(s, "dcba");
  fp::unique_inplace(s);
  EXPECT_EQ(s, "dcba");
}

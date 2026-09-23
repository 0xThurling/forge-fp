#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <vector>

TEST(Map, LookupMapFilterMerge) {
  std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
  EXPECT_EQ(fp::lookup(m, std::string("a")).value(), 1);
  EXPECT_FALSE(fp::lookup(m, std::string("z")).has_value());

  auto doubled = fp::map_values(m, [](int v) { return v * 2; });
  EXPECT_EQ(doubled.at("b"), 4);

  auto big = fp::filter_values(m, fp::gt(1));
  EXPECT_EQ(big.size(), 1u);
  EXPECT_EQ(big.count("a"), 0u);

  auto merged = fp::merge_with(m, std::map<std::string, int>{{"b", 10}, {"c", 3}},
                               [](int x, int y) { return x + y; });
  EXPECT_EQ(merged.at("b"), 12);
  EXPECT_EQ(merged.at("c"), 3);
}

TEST(Map, WorksWithUnorderedMap) {
  std::unordered_map<std::string, int> m = {{"a", 1}, {"b", 2}};
  EXPECT_EQ(fp::lookup(m, std::string("b")).value(), 2);
  EXPECT_EQ(fp::map_values(m, [](int v) { return v + 1; }).at("a"), 2);
  EXPECT_EQ(fp::keys(m).size(), 2u);
  EXPECT_EQ(fp::values(m).size(), 2u);
}

TEST(Map, KeysValuesToMap) {
  std::map<int, std::string> m = {{1, "a"}, {2, "b"}};
  auto ks = fp::keys(m);
  auto vs = fp::values(m);
  EXPECT_TRUE(std::is_sorted(ks.begin(), ks.end()));
  EXPECT_EQ(vs.size(), 2u);

  auto built = fp::to_map(std::vector<std::pair<int, std::string>>{{1, "a"}, {2, "b"}});
  EXPECT_EQ(built.at(2), "b");

  auto u = fp::to_unordered_map(std::vector<std::pair<int, std::string>>{{1, "a"}});
  EXPECT_EQ(u.at(1), "a");
}

TEST(Grid, MapTransposeFlatten) {
  std::vector<std::vector<int>> g = {{1, 2, 3}, {4, 5, 6}};
  auto doubled = fp::map2d(g, [](int x) { return x * 2; });
  EXPECT_EQ(doubled[1][2], 12);

  auto t = fp::transpose(g);
  EXPECT_EQ(t.size(), 3u);
  EXPECT_EQ(t[0], (std::vector<int>{1, 4}));

  EXPECT_EQ(fp::flatten(g), (std::vector<int>{1, 2, 3, 4, 5, 6}));
}

TEST(Grid, ForEachIndexTabulateCartesian) {
  int count = 0;
  fp::for_each_cell(2, 3, [&](size_t, size_t) { ++count; });
  EXPECT_EQ(count, 6);

  auto squares = fp::tabulate(4, [](size_t i) { return (int)(i * i); });
  EXPECT_EQ(squares, (std::vector<int>{0, 1, 4, 9}));

  auto prod = fp::cartesian_product(std::vector<int>{1, 2}, std::vector<std::string>{"a", "b"});
  EXPECT_EQ(prod.size(), 4u);
  EXPECT_EQ(prod[0], std::make_pair(1, std::string("a")));
}

TEST(Grid, Map3d) {
  std::vector<std::vector<std::vector<int>>> g = {{{1, 2}}, {{3}}};
  auto r = fp::map3d(g, [](int x) { return x + 1; });
  EXPECT_EQ(r[0][0][1], 3);
  EXPECT_EQ(r[1][0][0], 4);
}

TEST(Grid, Map2dIndexed) {
  std::vector<std::vector<int>> g = {{1, 2}, {3, 4}};
  auto r = fp::map2d_indexed(g, [](std::size_t i, std::size_t j, int x) {
    return x + static_cast<int>(i * 10 + j);
  });
  EXPECT_EQ(r, (std::vector<std::vector<int>>{{1, 3}, {13, 15}}));
}

TEST(Grid, Column) {
  std::vector<std::vector<int>> g = {{1, 2, 3}, {4, 5, 6}};
  EXPECT_EQ(fp::column(g, 1), (std::vector<int>{2, 5}));
}

TEST(Grid, Map2dInplace) {
  std::vector<std::vector<int>> g = {{1, 2}, {3, 4}};
  fp::map2d_inplace(g, [](int x) { return x * 2; });
  EXPECT_EQ(g, (std::vector<std::vector<int>>{{2, 4}, {6, 8}}));
}

TEST(Grid, Windows2d) {
  std::vector<std::vector<int>> g = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};

  auto patches = fp::windows2d(g, 2, 2);
  ASSERT_EQ(patches.size(), 1u);
  EXPECT_EQ(patches[0], (std::vector<std::vector<int>>{{1, 2}, {4, 5}}));

  auto rows = fp::windows2d(g, 1, 3);
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[1], (std::vector<std::vector<int>>{{4, 5, 6}}));
}

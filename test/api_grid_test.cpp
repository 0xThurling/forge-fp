// Instantiates grid.hpp's public API with *only* grid.hpp included (see
// api_vec_test.cpp for why: another module's overload must not mask a
// compile error in this one).
#include <fp/grid.hpp>

#include <gtest/gtest.h>
#include <vector>

TEST(ApiGrid, MapTransposeFlatten) {
  const std::vector<std::vector<int>> g{{1, 2}, {3, 4}};
  EXPECT_EQ(fp::map2d(g, [](int x) { return x * 2; }),
            (std::vector<std::vector<int>>{{2, 4}, {6, 8}}));
  EXPECT_EQ(fp::map2d_indexed(g, [](std::size_t i, std::size_t j, int x) {
              return x + static_cast<int>(i * 10 + j);
            }),
            (std::vector<std::vector<int>>{{1, 3}, {13, 15}}));
  EXPECT_EQ(fp::transpose(g), (std::vector<std::vector<int>>{{1, 3}, {2, 4}}));
  EXPECT_EQ(fp::flatten(g), (std::vector<int>{1, 2, 3, 4}));
  EXPECT_EQ(fp::column(g, 1), (std::vector<int>{2, 4}));
  EXPECT_EQ(fp::flatten(fp::windows2d(g, 1, 1)).size(), 4u);
  const std::vector<std::vector<std::vector<int>>> cube{{{1, 2}}, {{3, 4}}};
  EXPECT_EQ(
      fp::flatten(fp::flatten(fp::map3d(cube, [](int x) { return x + 1; }))),
      (std::vector<int>{2, 3, 4, 5}));
}

TEST(ApiGrid, TabulateCartesianCells) {
  EXPECT_EQ(fp::tabulate(3, [](std::size_t i) { return static_cast<int>(i * 2); }),
            (std::vector<int>{0, 2, 4}));
  EXPECT_EQ(fp::cartesian_product(std::vector<int>{1, 2}, std::vector<int>{3, 4}),
            (std::vector<std::pair<int, int>>{{1, 3}, {1, 4}, {2, 3}, {2, 4}}));
  int cells = 0;
  fp::for_each_cell(2, 3, [&](std::size_t, std::size_t) { ++cells; });
  EXPECT_EQ(cells, 6);
}

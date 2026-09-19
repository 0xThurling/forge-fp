// Regression tests for bugs fixed while building out the library. Each test
// documents a failure mode that existed at some point; keep them green.

#include <fp/all.hpp>
#include <fp/simd.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>

TEST(Regression, ValidationMergeAccumulatesBothSides) {
  auto merged = fp::merge(fp::invalid<int>("left"), fp::invalid<int>("right"));
  ASSERT_FALSE(merged.is_ok());
  EXPECT_EQ(merged.error(), (std::vector<std::string>{"left", "right"}));
}

TEST(Regression, ArenaReturnsAlignedBlocksUnderGrowth) {
  fp::Arena arena(64);
  for (int i = 0; i < 64; ++i) {
    auto *p = arena.alloc<long double>(); // alignment 16, forces block growth
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(long double), 0u);
    *p = i;
  }
}

TEST(Regression, VecSortByActuallySorts) {
  std::vector<int> v = {5, 1, 4, 2, 3};
  EXPECT_EQ(fp::sort_by(v, [](int x) { return x; }), (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST(Regression, RangesSpanSplitsAtPredicateBoundary) {
  std::vector<int> v = {1, 2, 5, 3};
  auto [lo, hi] = fp::span(v, fp::lt(3));
  EXPECT_EQ(fp::to_vector(lo), (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::to_vector(hi), (std::vector<int>{5, 3}));
}

TEST(Regression, ValidationTraverseCollectsAllErrors) {
  auto f = [](int x) { return x > 0 ? fp::valid(x) : fp::invalid<int>("neg"); };
  auto r = fp::traverse(std::vector<int>{-1, 2, -3}, f);
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error().size(), 2u);
}

TEST(Regression, SplitWithEmptyDelimiterTerminates) {
  EXPECT_EQ(fp::str::split(std::string("abc"), std::string()), (std::vector<std::string>{"abc"}));
  EXPECT_EQ(fp::str::split_view(std::string_view("abc"), std::string_view()),
            (std::vector<std::string_view>{"abc"}));
}

TEST(Regression, SplitDropsTrailingEmptyConsistently) {
  EXPECT_EQ(fp::str::split(std::string("a,b,"), ','), (std::vector<std::string>{"a", "b"}));
  auto sv = fp::str::split_view(std::string_view("a,b,"), ',');
  ASSERT_EQ(sv.size(), 2u);
  EXPECT_EQ(sv[0], "a");
  EXPECT_EQ(sv[1], "b");
}

TEST(Regression, SimdClampUsesBothBounds) {
  std::vector<float> v = {-10.0f, 0.0f, 5.0f, 100.0f};
  fp::clamp_inplace(v, 0.0f, 1.0f);
  EXPECT_FLOAT_EQ(v[0], 0.0f);
  EXPECT_FLOAT_EQ(v[1], 0.0f);
  EXPECT_FLOAT_EQ(v[2], 1.0f);
  EXPECT_FLOAT_EQ(v[3], 1.0f);
}

TEST(Regression, ParserNegativeLookaheadNameDoesNotCollide) {
  auto p = fp::not_followed(fp::digit) >> fp::letter;
  EXPECT_EQ(fp::run(p, "a").value(), 'a');
  EXPECT_FALSE(fp::run(p, "5").is_ok());
}

TEST(Regression, UnorderedMapHelpersCompileAndWork) {
  auto m = fp::to_unordered_map(std::vector<std::pair<std::string, int>>{{"a", 1}});
  EXPECT_EQ(fp::lookup(m, std::string("a")).value(), 1);
  EXPECT_EQ(fp::map_values(m, [](int v) { return v + 1; }).at("a"), 2);
}

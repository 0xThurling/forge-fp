#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <optional>
#include <string>

using namespace fp;

TEST(Maybe, MapAndThen) {
  std::optional<int> o = 21;
  auto r = fp::map(o, [](int x) { return x * 2; });
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
  EXPECT_FALSE(fp::map(std::optional<int>{}, [](int x) { return x * 2; }).has_value());

  auto c = fp::and_then(o, [](int x) { return std::optional<int>(x + 1); });
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(*c, 22);
  EXPECT_FALSE(fp::and_then(std::optional<int>{}, [](int x) { return std::optional<int>(x); }).has_value());
}

TEST(Maybe, OrElseValueAndLazy) {
  EXPECT_EQ(fp::or_else(std::optional<int>{}, 9), 9);
  EXPECT_EQ(fp::or_else(std::optional<int>{3}, 9), 3);
  EXPECT_EQ(fp::or_else(std::optional<int>{}, [] { return 9; }), 9);
  EXPECT_EQ(fp::value_or_lazy(std::optional<int>{3}, [] { return 9; }), 3);
}

TEST(Maybe, FilterFlattenApply) {
  auto o = std::optional<int>{4};
  EXPECT_FALSE(fp::filter(o, fp::gt(10)).has_value());
  EXPECT_TRUE(fp::filter(o, fp::gt(0)).has_value());

  auto nested = std::optional<std::optional<int>>{std::optional<int>{5}};
  ASSERT_TRUE(fp::flatten(nested).has_value());
  EXPECT_EQ(*fp::flatten(nested), 5);
  EXPECT_FALSE(fp::flatten(std::optional<std::optional<int>>{}).has_value());

  std::optional<std::function<int(int)>> f = [](int x) { return x + 1; };
  auto applied = fp::apply(f, o);
  ASSERT_TRUE(applied.has_value());
  EXPECT_EQ(*applied, 5);
  EXPECT_FALSE(fp::apply(std::optional<std::function<int(int)>>{}, o).has_value());
}

TEST(Maybe, CollectAndChain) {
  std::vector<std::optional<int>> ok = {1, 2, 3};
  auto c = fp::collect(ok);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(*c, (std::vector<int>{1, 2, 3}));
  EXPECT_FALSE(fp::collect(std::vector<std::optional<int>>{1, std::nullopt}).has_value());

  std::optional<int> o = 40;
  auto chained = o >>= [](int x) { return std::optional<int>(x + 2); };
  ASSERT_TRUE(chained.has_value());
  EXPECT_EQ(*chained, 42);
}

TEST(Maybe, Pipe) {
  auto r = fp::out(fp::into(std::optional<int>{21}) | [](int x) { return x * 2; });
  EXPECT_EQ(r.value(), 42);
}

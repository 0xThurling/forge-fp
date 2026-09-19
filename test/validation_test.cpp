#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(Validation, ValidInvalid) {
  auto v = fp::valid(3);
  ASSERT_TRUE(v.is_ok());
  EXPECT_EQ(v.value(), 3);

  auto i = fp::invalid<int>("bad");
  EXPECT_FALSE(i.is_ok());
  EXPECT_EQ(i.error(), (std::vector<std::string>{"bad"}));

  auto i2 = fp::invalid<int>(std::vector<std::string>{"a", "b"});
  EXPECT_EQ(i2.error().size(), 2u);
}

TEST(Validation, ValidateAllAccumulatesEveryError) {
  std::vector<fp::Validation<int>> vs = {fp::valid(1), fp::invalid<int>("a"),
                                         fp::valid(3), fp::invalid<int>("b")};
  auto r = fp::validate_all(vs);
  EXPECT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), (std::vector<std::string>{"a", "b"}));

  auto ok = fp::validate_all(std::vector<fp::Validation<int>>{fp::valid(1), fp::valid(2)});
  ASSERT_TRUE(ok.is_ok());
  EXPECT_EQ(ok.value(), (std::vector<int>{1, 2}));
}

TEST(Validation, Combine2AndCombine) {
  auto mk = [](int a, int b) { return a + b; };
  EXPECT_EQ(fp::combine2(fp::valid(1), fp::valid(2), mk).value(), 3);

  auto bad = fp::combine2(fp::invalid<int>("x"), fp::invalid<int>("y"), mk);
  EXPECT_FALSE(bad.is_ok());
  EXPECT_EQ(bad.error(), (std::vector<std::string>{"x", "y"}));

  auto c = fp::combine([](int a, int b, int d) { return a + b + d; },
                       fp::valid(1), fp::invalid<int>("e"), fp::valid(3));
  EXPECT_FALSE(c.is_ok());
  EXPECT_EQ(c.error(), (std::vector<std::string>{"e"}));
}

TEST(Validation, EnsureCheck) {
  EXPECT_TRUE(fp::ensure(true, "m", 1).is_ok());
  EXPECT_FALSE(fp::ensure(false, "m", 1).is_ok());
  EXPECT_TRUE(fp::check(fp::gt(0), "positive", 1).is_ok());
  EXPECT_EQ(fp::check(fp::gt(0), "positive", -1).error().front(), "positive");
}

TEST(Validation, Traverse) {
  auto f = [](int x) { return x > 0 ? fp::valid(x * 2) : fp::invalid<int>("neg"); };
  auto ok = fp::traverse(std::vector<int>{1, 2, 3}, f);
  ASSERT_TRUE(ok.is_ok());
  EXPECT_EQ(ok.value(), (std::vector<int>{2, 4, 6}));

  auto bad = fp::traverse(std::vector<int>{1, -2, -3}, f);
  EXPECT_FALSE(bad.is_ok());
  EXPECT_EQ(bad.error().size(), 2u);
}

TEST(Validation, MergeKeepsBothSides) {
  auto both_ok = fp::merge(fp::valid(1), fp::valid(2));
  ASSERT_TRUE(both_ok.is_ok());
  EXPECT_EQ(both_ok.value(), 1);

  auto both_err = fp::merge(fp::invalid<int>("a"), fp::invalid<int>("b"));
  EXPECT_FALSE(both_err.is_ok());
  EXPECT_EQ(both_err.error(), (std::vector<std::string>{"a", "b"}));

  auto mixed = fp::merge(fp::invalid<int>("a"), fp::valid(2));
  EXPECT_FALSE(mixed.is_ok());
  EXPECT_EQ(mixed.error(), (std::vector<std::string>{"a"}));

  auto mixed2 = fp::merge(fp::valid(2), fp::invalid<int>("b"));
  EXPECT_FALSE(mixed2.is_ok());
  EXPECT_EQ(mixed2.error(), (std::vector<std::string>{"b"}));
}

TEST(Validation, ToResultTakesFirstMessage) {
  auto ok = fp::to_result(fp::valid(3));
  EXPECT_EQ(ok.value(), 3);
  auto bad = fp::to_result(fp::invalid<int>(std::vector<std::string>{"first", "second"}));
  EXPECT_FALSE(bad.is_ok());
  EXPECT_EQ(bad.error(), "first");
}

TEST(Validation, ValidateNoneAndAny) {
  auto none = fp::validate_none(std::vector<int>{1, 2, 3}, fp::lt(0), "must be >= 0");
  EXPECT_TRUE(none.is_ok());

  auto bad = fp::validate_none(std::vector<int>{1, -2, -3}, fp::lt(0), "must be >= 0");
  EXPECT_FALSE(bad.is_ok());
  EXPECT_EQ(bad.error().size(), 2u);

  auto any = fp::validate_any(std::vector<int>{1, 2, 3}, fp::gt(2), "need one > 2");
  EXPECT_TRUE(any.is_ok());
  EXPECT_FALSE(fp::validate_any(std::vector<int>{1, 2}, fp::gt(2), "need one > 2").is_ok());
}

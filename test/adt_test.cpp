#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <variant>

namespace {
struct Circle {
  double radius;
};
struct Rect {
  double w, h;
};
using Shape = std::variant<Circle, Rect>;

double area(Shape const &s) {
  return fp::match(s, fp::case_<Circle>([](auto c) { return c.radius; }),
                   fp::case_<Rect>([](auto r) { return r.w * r.h; }));
}

std::string kind(int x) {
  return fp::cond(x, fp::when(fp::gt(0), [](auto) { return "pos"; }),
                  fp::when(fp::lt(0), [](auto) { return "neg"; }),
                  fp::otherwise([](auto) { return "zero"; }));
}
} // namespace

TEST(Adt, VariantMatch) {
  EXPECT_DOUBLE_EQ(area(Shape{Rect{3.0, 4.0}}), 12.0);
  EXPECT_DOUBLE_EQ(area(Shape{Circle{2.0}}), 2.0);
}

TEST(Adt, CondGuards) {
  EXPECT_EQ(kind(5), "pos");
  EXPECT_EQ(kind(-1), "neg");
  EXPECT_EQ(kind(0), "zero");
}

TEST(Adt, Overload) {
  auto v = fp::overload{[](int x) { return x + 1; },
                        [](std::string const &s) { return (int)s.size(); }};
  EXPECT_EQ(v(1), 2);
  EXPECT_EQ(v(std::string("abc")), 3);
}

TEST(Adt, OptionalMatch) {
  auto r = fp::match(std::optional<int>{4}, [](int x) { return x; }, [] { return -1; });
  EXPECT_EQ(r, 4);
  auto n = fp::match(std::optional<int>{}, [](int x) { return x; }, [] { return -1; });
  EXPECT_EQ(n, -1);
}

TEST(Adt, EitherMatch) {
  auto ok = fp::match(fp::ok(3), [](int x) { return x; },
                      [](std::string const &) { return -1; });
  EXPECT_EQ(ok, 3);
  auto err = fp::match(fp::err<int>("x"), [](int x) { return x; },
                       [](std::string const &) { return -1; });
  EXPECT_EQ(err, -1);
}

TEST(Adt, ValidationMatch) {
  auto v = fp::match(fp::invalid<int>("a"), [](int x) { return x; },
                     [](std::vector<std::string> const &e) { return (int)e.size(); });
  EXPECT_EQ(v, 1);
}

TEST(Adt, ResultVoidMatch) {
  auto n = fp::match(fp::ok<void>(), [] { return 1; },
                     [](std::string const &) { return 0; });
  EXPECT_EQ(n, 1);
}

TEST(Adt, ValueOr) {
  EXPECT_EQ(fp::value_or(std::optional<int>{}, 9), 9);
  EXPECT_EQ(fp::value_or(std::optional<int>{3}, 9), 3);
  EXPECT_EQ(fp::value_or(fp::err<int>("x"), 9), 9);
  EXPECT_EQ(fp::value_or(fp::ok(3), 9), 3);
}

TEST(Adt, Unpack) {
  auto add = fp::unpack([](int a, int b) { return a + b; });
  EXPECT_EQ(add(std::pair{2, 3}), 5);
}

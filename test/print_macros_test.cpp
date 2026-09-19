#include <fp/all.hpp>
#include <fp/macros.hpp>

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

TEST(Print, ResultAndEither) {
  std::ostringstream os;
  os << fp::ok(3) << " " << fp::err<int>("boom");
  EXPECT_EQ(os.str(), "ok(3) err(boom)");
}

TEST(Print, ResultVoid) {
  std::ostringstream os;
  os << fp::ok<void>() << " " << fp::err<void>("nope");
  EXPECT_EQ(os.str(), "ok() err(nope)");
}

TEST(Print, Validation) {
  std::ostringstream os;
  os << fp::valid(1) << " "
     << fp::invalid<int>(std::vector<std::string>{"a", "b"});
  EXPECT_EQ(os.str(), "ok(1) err(a; b)");
}

// --- portable statement forms (all compilers) -----------------------------

namespace {
fp::Result<int> parse_add(std::string const &a, std::string const &b) {
  FP_TRY_VALUE(x, fp::str::to_int(a));
  FP_TRY_VALUE(y, fp::str::to_int(b));
  return fp::ok(x + y);
}

fp::Result<int> try_void_ok() {
  FP_TRY_VOID(fp::ok<void>());
  return fp::ok(7);
}

fp::Result<int> try_void_fails() {
  FP_TRY_VOID(fp::err<void>("nope"));
  return fp::ok(7);
}

fp::Outcome<int> outcome_value(std::string const &s) {
  FP_TRY_VALUE(x, fp::str::to_int(s));
  return fp::Outcome<int>::ok(x * 2);
}
} // namespace

TEST(Macros, TryValueIsPortable) {
  auto good = parse_add("40", "2");
  ASSERT_TRUE(good.is_ok());
  EXPECT_EQ(good.value(), 42);

  auto bad = parse_add("nope", "2");
  ASSERT_FALSE(bad.is_ok());
  EXPECT_EQ(bad.error(), "not a number");

  auto o = outcome_value("21");
  ASSERT_TRUE(o.is_ok());
  EXPECT_EQ(o.value(), 42);
  EXPECT_FALSE(outcome_value("x").is_ok());
}

TEST(Macros, TryVoidIsPortable) {
  EXPECT_EQ(try_void_ok().value(), 7);

  auto r = try_void_fails();
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "nope");
}

// --- expression form (GCC/Clang) ------------------------------------------

#if !defined(_MSC_VER)
namespace {
fp::Result<int> parse_add_expr(std::string const &s) {
  int a = FP_TRY(fp::str::to_int(s));
  int b = FP_TRY(fp::str::to_int("2"));
  return fp::ok(a + b);
}

fp::Outcome<int> outcome_expr(std::string const &s) {
  int x = FP_TRY(fp::str::to_int(s));
  return fp::Outcome<int>::ok(x);
}

fp::Result<int> try_void_expr() {
  FP_TRY(fp::ok<void>());
  return fp::ok(7);
}
} // namespace

TEST(Macros, TryExpressionForm) {
  EXPECT_EQ(parse_add_expr("40").value(), 42);
  auto r = parse_add_expr("nope");
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "not a number");

  auto o = outcome_expr("5");
  ASSERT_TRUE(o.is_ok());
  EXPECT_EQ(o.value(), 5);

  EXPECT_EQ(try_void_expr().value(), 7);
}
#endif

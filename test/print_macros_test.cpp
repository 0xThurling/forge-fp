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

namespace {
fp::Result<int> parse_add(std::string const &s) {
  int a = FP_TRY(fp::str::to_int(s));
  int b = FP_TRY(fp::str::to_int("2"));
  return fp::ok(a + b);
}
} // namespace

TEST(Macros, TryPropagatesErrors) {
  EXPECT_EQ(parse_add("40").value(), 42);
  auto r = parse_add("nope");
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "not a number");
}

namespace {
fp::Result<int> try_void_propagation() {
  FP_TRY(fp::ok<void>());
  return fp::ok(7);
}
} // namespace

TEST(Macros, TryWithVoid) {
  EXPECT_EQ(try_void_propagation().value(), 7);
}

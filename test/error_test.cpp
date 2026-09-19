#include <fp/all.hpp>
#include <fp/macros.hpp>

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <system_error>

TEST(Error, ConstructAndCompare) {
  auto e = fp::error("boom");
  EXPECT_EQ(e.message, "boom");
  EXPECT_FALSE(e.code);
  EXPECT_TRUE(e.causes.empty());
  EXPECT_NE(e.where.line(), 0u);

  EXPECT_EQ(fp::error("boom"), fp::error("boom"));
  EXPECT_NE(fp::error("boom"), fp::error("bang"));
  EXPECT_EQ(fp::error("c", fp::errc::not_found).code, fp::errc::not_found);
}

TEST(Outcome, ErrorConvertsImplicitly) {
  auto f = [](bool good) -> fp::Outcome<int> {
    if (!good)
      return fp::error("bad");
    return fp::Outcome<int>::ok(42);
  };
  ASSERT_TRUE(f(true).is_ok());
  EXPECT_EQ(f(true).value(), 42);
  ASSERT_FALSE(f(false).is_ok());
  EXPECT_EQ(f(false).error().message, "bad");
}

TEST(Outcome, WithContextBuildsChain) {
  auto inner =
      fp::Outcome<int>::err(fp::error("no such file", fp::errc::not_found));
  auto o = fp::with_context(inner, "reading config");
  ASSERT_FALSE(o.is_ok());
  EXPECT_EQ(o.error().message, "reading config");
  EXPECT_EQ(o.error().code, fp::errc::not_found);
  ASSERT_EQ(o.error().causes.size(), 1u);
  EXPECT_EQ(o.error().causes[0].message, "no such file");
  EXPECT_EQ(fp::root_cause(o.error()).message, "no such file");
  EXPECT_EQ(fp::to_string(o.error()), "reading config: no such file");

  auto deeper = fp::with_context(o, "startup");
  EXPECT_EQ(fp::to_string(deeper.error()),
            "startup: reading config: no such file");
  EXPECT_EQ(fp::root_cause(deeper.error()).message, "no such file");
}

TEST(Outcome, WithContextLeavesSuccessAlone) {
  auto o = fp::with_context(fp::Outcome<int>::ok(7), "ignored");
  ASSERT_TRUE(o.is_ok());
  EXPECT_EQ(o.value(), 7);
}

TEST(Outcome, BridgesToResult) {
  EXPECT_EQ(fp::to_result(fp::Outcome<int>::ok(1)).value(), 1);

  auto r = fp::to_result(
      fp::with_context(fp::Outcome<int>::err(fp::error("inner")), "outer"));
  ASSERT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "outer: inner");

  auto o = fp::from_result(fp::err<int>("disk"), fp::errc::timeout);
  ASSERT_FALSE(o.is_ok());
  EXPECT_EQ(o.error().message, "disk");
  EXPECT_EQ(o.error().code, fp::errc::timeout);
  EXPECT_TRUE(fp::from_result(fp::ok(3)).is_ok());
}

TEST(Outcome, VoidWorks) {
  fp::Outcome<void> good = fp::Outcome<void>::ok();
  EXPECT_TRUE(good.is_ok());

  fp::Outcome<void> bad = fp::error("nope");
  EXPECT_FALSE(bad.is_ok());
  EXPECT_EQ(fp::to_result(bad).error(), "nope");

  fp::Outcome<void> from = fp::from_result(fp::err<void>("x"));
  EXPECT_FALSE(from.is_ok());
}

TEST(Outcome, Print) {
  std::ostringstream os;
  os << fp::error("boom") << " " << fp::Outcome<int>::ok(3) << " "
     << fp::Outcome<int>::err(fp::error("x"));
  EXPECT_EQ(os.str(), "boom ok(3) err(x)");
}

TEST(Outcome, TryPropagates) {
  auto add = [](std::string const &a, std::string const &b) -> fp::Outcome<int> {
    FP_TRY_VALUE(x, fp::str::to_int(a));
    FP_TRY_VALUE(y, fp::str::to_int(b));
    return fp::Outcome<int>::ok(x + y);
  };

  auto good = add("40", "2");
  ASSERT_TRUE(good.is_ok());
  EXPECT_EQ(good.value(), 42);

  auto bad = add("nope", "2");
  ASSERT_FALSE(bad.is_ok());
  EXPECT_EQ(bad.error().message, "not a number");
}

TEST(Outcome, CombinatorsWork) {
  auto o = fp::map(fp::Outcome<int>::ok(21), [](int x) { return x * 2; });
  EXPECT_EQ(o.value(), 42);

  auto e = fp::map(fp::Outcome<int>::err(fp::error("e")),
                   [](int x) { return x * 2; });
  EXPECT_FALSE(e.is_ok());
  int seen = 0;
  fp::tap_err(e, [&](fp::Error const &) { ++seen; });
  EXPECT_EQ(seen, 1);

  auto recovered = fp::or_else(e, [](fp::Error const &) { return 0; });
  EXPECT_EQ(recovered, 0);
}

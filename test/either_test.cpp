#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <string>

TEST(Either, ConstructAndInspect) {
  using E = fp::Either<std::string, int>;
  E ok = E::ok(42);
  E err = E::err(std::string("boom"));
  EXPECT_TRUE(ok.is_ok());
  EXPECT_EQ(ok.value(), 42);
  EXPECT_FALSE(err.is_ok());
  EXPECT_EQ(err.error(), "boom");
  EXPECT_EQ(E::value_type{}, 0);
}

TEST(Either, MapAndThen) {
  using E = fp::Either<std::string, int>;
  EXPECT_EQ(fp::map(E::ok(21), [](int x) { return x * 2; }).value(), 42);
  EXPECT_FALSE(fp::map(E::err(std::string("e")), [](int x) { return x * 2; }).is_ok());
  EXPECT_EQ(fp::and_then(E::ok(41), [](int x) { return E::ok(x + 1); }).value(), 42);
}

TEST(Either, OrElseMapErrorBimapSwap) {
  using E = fp::Either<std::string, int>;
  EXPECT_EQ(fp::or_else(E::err(std::string("x")), [](std::string const &) { return 0; }), 0);
  EXPECT_EQ(fp::map_error(E::err(std::string("x")), [](std::string e) { return e + "!"; }).error(), "x!");
  auto b = fp::bimap(E::err(std::string("x")), [](std::string e) { return (int)e.size(); }, [](int x) { return x; });
  EXPECT_FALSE(b.is_ok());
  EXPECT_EQ(b.error(), 1);
  EXPECT_FALSE(fp::swap(E::ok(1)).is_ok());
  EXPECT_EQ(fp::swap(E::ok(1)).error(), 1);
}

TEST(Either, BimapAppliesBothSides) {
  using E = fp::Either<std::string, int>;
  // Regression: the success side used to return the raw value, silently
  // ignoring `on_ok` (the identity lambda in the older test hid it).
  auto ok = fp::bimap(E::ok(20), [](std::string e) { return (int)e.size(); },
                      [](int x) { return x * 2; });
  ASSERT_TRUE(ok.is_ok());
  EXPECT_EQ(ok.value(), 40);

  auto err = fp::bimap(E::err(std::string("abc")),
                       [](std::string e) { return (int)e.size(); },
                       [](int x) { return x * 2; });
  ASSERT_FALSE(err.is_ok());
  EXPECT_EQ(err.error(), 3);
}

TEST(Either, FlattenToOptionalRightsLefts) {
  using E = fp::Either<std::string, int>;
  using Nested = fp::Either<std::string, E>;
  auto flat = fp::flatten(Nested::ok(E::ok(7)));
  EXPECT_EQ(flat.value(), 7);
  EXPECT_EQ(fp::to_optional(E::ok(3)), std::optional<int>(3));
  std::vector<E> es = {E::ok(1), E::err(std::string("e")), E::ok(2)};
  EXPECT_EQ(fp::rights(es), (std::vector<int>{1, 2}));
  EXPECT_EQ(fp::lefts(es), (std::vector<std::string>{"e"}));
}

TEST(Either, OkOrExpectOperatorEqBool) {
  using E = fp::Either<std::string, int>;
  EXPECT_EQ(fp::ok_or(std::optional<int>{}, std::string("missing")).error(), "missing");
  EXPECT_EQ(fp::expect(E::ok(5), "msg"), 5);
  EXPECT_THROW(fp::expect(E::err(std::string("nope")), "msg"), std::runtime_error);

  EXPECT_TRUE(fp::ok(3) == fp::ok(3));
  EXPECT_TRUE(fp::ok(3) != fp::ok(4));
  EXPECT_TRUE(fp::ok(3) != fp::err<int>("x"));
  EXPECT_TRUE(fp::err<int>("x") == fp::err<int>("x"));
  if (!fp::ok(1))
    FAIL();
  if (fp::err<int>("x"))
    FAIL();

  fp::Result<int> r = fp::ok(21);
  EXPECT_EQ((r >>= [](int x) { return fp::ok(x * 2); }).value(), 42);
}

TEST(Either, Void) {
  using V = fp::Either<std::string, void>;
  EXPECT_TRUE(V::ok().is_ok());
  EXPECT_FALSE(V::err("no").is_ok());
  EXPECT_EQ(V::err("no").error(), "no");
  EXPECT_TRUE(fp::ok<void>() == fp::ok<void>());
}

TEST(Result, Fail) {
  fp::Result<int> r = fp::fail("bad");
  EXPECT_FALSE(r.is_ok());
  EXPECT_EQ(r.error(), "bad");
  fp::Validation<int> v = fp::fail("bad");
  EXPECT_FALSE(v.is_ok());
  EXPECT_EQ(v.error(), (std::vector<std::string>{"bad"}));
}

TEST(Result, SequenceTraverseTranspose) {
  std::vector<fp::Result<int>> rs = {fp::ok(1), fp::ok(2)};
  EXPECT_EQ(fp::sequence(rs).value(), (std::vector<int>{1, 2}));
  std::vector<fp::Result<int>> bad = {fp::ok(1), fp::err<int>("e")};
  EXPECT_FALSE(fp::sequence(bad).is_ok());

  auto tr = fp::traverse(std::vector<std::string>{"1", "2"}, fp::str::to_int);
  ASSERT_TRUE(tr.is_ok());
  EXPECT_EQ(tr.value(), (std::vector<int>{1, 2}));

  EXPECT_EQ(fp::transpose(fp::ok(std::optional<int>{5})).value().value(), 5);
}

TEST(Result, TryCombineContextCollectAll) {
  auto r = fp::try_([] { return std::stoi("123"); });
  EXPECT_EQ(r.value(), 123);
  auto bad = fp::try_([&]() -> int { throw std::runtime_error("x"); });
  EXPECT_FALSE(bad.is_ok());

  EXPECT_EQ(fp::combine2(fp::ok(1), fp::ok(2)).value(), (std::pair{1, 2}));
  EXPECT_EQ(fp::context(fp::err<int>("e"), "ctx: ").error(), "ctx: e");
  auto col = fp::collect_all(std::vector<fp::Result<int>>{fp::ok(1), fp::err<int>("a")});
  EXPECT_FALSE(col.is_ok());
  EXPECT_NE(col.error().find("a"), std::string::npos);

  EXPECT_EQ(fp::unwrap(fp::ok(7)), 7);
  EXPECT_THROW(fp::unwrap(fp::err<int>("u")), std::runtime_error);
  EXPECT_EQ(fp::from_optional(std::optional<int>{}, "none").error(), "none");
}

TEST(Either, TapOkErr) {
  int ok_seen = 0, err_seen = 0;
  fp::tap_ok(fp::ok(3), [&](int) { ++ok_seen; });
  fp::tap_err(fp::err<int>("e"), [&](std::string const &) { ++err_seen; });
  EXPECT_EQ(ok_seen, 1);
  EXPECT_EQ(err_seen, 1);
  fp::tap_ok(fp::err<int>("e"), [&](int) { ++ok_seen; });
  fp::tap_err(fp::ok(3), [&](std::string const &) { ++err_seen; });
  EXPECT_EQ(ok_seen, 1);
  EXPECT_EQ(err_seen, 1);
}

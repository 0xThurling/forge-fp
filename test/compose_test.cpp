#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

TEST(Compose, ComposeAndPipe) {
  auto f = fp::compose([](int x) { return x + 1; }, [](int x) { return x * 2; });
  EXPECT_EQ(f(3), 7);

  auto g = fp::pipe([](int x) { return x + 1; }, [](int x) { return x * 2; },
                    [](int x) { return x - 3; });
  EXPECT_EQ(g(3), 5);
}

TEST(Compose, IntoOutPipeline) {
  auto r = fp::out(fp::into(3) | [](int x) { return x * 2; } |
                   [](int x) { return std::to_string(x); });
  EXPECT_EQ(r, "6");

  EXPECT_EQ(fp::pipeline(3, [](int x) { return x + 1; },
                         [](int x) { return x * 10; }),
            40);
}

TEST(Compose, Tap) {
  int seen = 0;
  auto r = fp::out(fp::into(5) | fp::tap([&](int x) { seen = x; }) |
                   [](int x) { return x + 1; });
  EXPECT_EQ(r, 6);
  EXPECT_EQ(seen, 5);
}

TEST(Compose, Curry) {
  auto add = [](int a, int b) { return a + b; };
  EXPECT_EQ(fp::curry(add)(1)(2), 3);
  EXPECT_EQ(fp::curry(add)(1, 2), 3);

  auto add3 = [](int a, int b, int c) { return a + b + c; };
  EXPECT_EQ(fp::curry(add3)(1)(2)(3), 6);
  EXPECT_EQ(fp::curry(add3)(1, 2)(3), 6);
}

TEST(Compose, Uncurry) {
  auto curried = [](int a) {
    return [a](int b) { return a + b; };
  };
  EXPECT_EQ(fp::uncurry(curried)(1, 2), 3);
}

TEST(Compose, OpsCombinators) {
  EXPECT_EQ(fp::plus(1)(2), 3);
  EXPECT_EQ(fp::plus(1, 2), 3);
  EXPECT_EQ(fp::minus(5)(2), 3);
  EXPECT_EQ(fp::times(3)(4), 12);
  EXPECT_EQ(fp::divide(8.0)(2.0), 4.0);
  EXPECT_TRUE(fp::gt(0)(5));
  EXPECT_FALSE(fp::gt(5)(5));
  EXPECT_TRUE(fp::lt(5)(3));
  EXPECT_TRUE(fp::eq(3)(3));
  EXPECT_TRUE(fp::ge(3)(3));
  EXPECT_TRUE(fp::le(3)(2));
  EXPECT_TRUE(fp::ne(1)(2));
  EXPECT_EQ(fp::and_(true)(true), true);
  EXPECT_EQ(fp::or_(false)(true), true);
  EXPECT_EQ(fp::not_(false), true);
  EXPECT_EQ(fp::negate(3), -3);
  EXPECT_EQ(fp::increment(3), 4);
  EXPECT_EQ(fp::decrement(3), 2);
}

TEST(Compose, Memoize) {
  auto calls = std::make_shared<int>(0);
  auto slow = [calls](int x) {
    ++*calls;
    return x * 2;
  };
  auto m = fp::memoize<int>(slow);
  EXPECT_EQ(m(2), 4);
  EXPECT_EQ(m(2), 4);
  EXPECT_EQ(m(3), 6);
  EXPECT_EQ(*calls, 2);
}

TEST(Compose, Memoize2AndN) {
  auto calls = std::make_shared<int>(0);
  auto m2 = fp::memoize2<int, int>([calls](int a, int b) {
    ++*calls;
    return a * 10 + b;
  });
  EXPECT_EQ(m2(1, 2), 12);
  EXPECT_EQ(m2(1, 2), 12);
  EXPECT_EQ(m2(2, 1), 21);
  EXPECT_EQ(*calls, 2);

  auto calls3 = std::make_shared<int>(0);
  auto m3 = fp::memoizeN<int, int, std::string>([calls3](int a, int b, std::string c) {
    ++*calls3;
    return std::to_string(a + b) + c;
  });
  EXPECT_EQ(m3(1, 2, "x"), "3x");
  EXPECT_EQ(m3(1, 2, "x"), "3x");
  EXPECT_EQ(m3(1, 2, "y"), "3y");
  EXPECT_EQ(*calls3, 2);
}

TEST(Compose, MemoizeStringValues) {
  int calls = 0;
  auto m = fp::memoize<int>([&calls](int x) {
    ++calls;
    return std::string(static_cast<std::size_t>(x), 'x');
  });
  EXPECT_EQ(m(3), "xxx");
  EXPECT_EQ(m(3), "xxx");
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(m(4), "xxxx");
  EXPECT_EQ(calls, 2);
}

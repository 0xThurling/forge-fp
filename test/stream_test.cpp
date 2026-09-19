#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
fp::Stream<int> from(std::vector<int> const &v) {
  auto i = std::make_shared<std::size_t>(0);
  return fp::Stream<int>([&v, i]() -> std::optional<int> {
    if (*i >= v.size())
      return std::nullopt;
    return v[(*i)++];
  });
}
} // namespace

TEST(Stream, MapFilterCollect) {
  std::vector<int> v = {1, 2, 3, 4, 5};
  EXPECT_EQ(from(v).map([](int x) { return x * 2; }).collect(),
            (std::vector<int>{2, 4, 6, 8, 10}));
  EXPECT_EQ(from(v).filter([](int x) { return x % 2 == 1; }).to_vector(),
            (std::vector<int>{1, 3, 5}));
}

TEST(Stream, Subscribe) {
  std::vector<int> v = {1, 2, 3};
  int total = 0;
  from(v).subscribe([&](int x) { total += x; });
  EXPECT_EQ(total, 6);
}

TEST(Stream, TakeAndTakeWhile) {
  std::vector<int> v = {1, 2, 3, 4, 5};
  EXPECT_EQ(from(v).take(2).collect(), (std::vector<int>{1, 2}));
  EXPECT_EQ(from(v).take_while([](int x) { return x < 4; }).collect(),
            (std::vector<int>{1, 2, 3}));
  EXPECT_TRUE(from(v).take(0).collect().empty());
}

TEST(Stream, ScanAndFold) {
  std::vector<int> v = {1, 2, 3};
  EXPECT_EQ(from(v).scan(0, std::plus<>{}).collect(), (std::vector<int>{1, 3, 6}));
  EXPECT_EQ(from(v).fold_left(0, std::plus<>{}), 6);
}

TEST(Stream, Concat) {
  std::vector<int> a = {1, 2}, b = {3, 4};
  EXPECT_EQ(from(a).concat(from(b)).collect(), (std::vector<int>{1, 2, 3, 4}));
}

TEST(Stream, FromChannel) {
  fp::Channel<int> ch;
  ch.send(1);
  ch.send(2);
  ch.close();
  fp::Stream<int> s(ch);
  EXPECT_EQ(s.collect(), (std::vector<int>{1, 2}));
}

TEST(Stream, PipeMapsItems) {
  std::vector<int> v = {1, 2, 3};
  auto mapped = fp::out(fp::into(from(v)) | [](int x) { return x + 10; });
  EXPECT_EQ(mapped.collect(), (std::vector<int>{11, 12, 13}));
}

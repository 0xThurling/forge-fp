#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <string>
#include <vector>

TEST(Serialize, TextRoundTrip) {
  std::vector<double> v = {0.1 + 0.2, -1.5, 3.0};
  const auto text = fp::to_text(v, 17);

  auto back = fp::from_text<double>(text);
  ASSERT_TRUE(back.is_ok());
  EXPECT_EQ(back.value(), v);

  auto ints = fp::from_text<int>("1 2 3");
  ASSERT_TRUE(ints.is_ok());
  EXPECT_EQ(ints.value(), (std::vector<int>{1, 2, 3}));

  EXPECT_FALSE(fp::from_text<double>("1 x 3").is_ok());
}

TEST(Serialize, BytesRoundTrip) {
  std::vector<float> v = {1.5f, -2.25f, 0.0f};
  const auto bytes = fp::to_bytes(v);
  EXPECT_EQ(bytes.size(), v.size() * sizeof(float));

  auto back = fp::from_bytes<float>(bytes);
  ASSERT_TRUE(back.is_ok());
  EXPECT_EQ(back.value(), v);

  auto truncated = std::vector<std::byte>(bytes.begin(), bytes.end() - 1);
  EXPECT_FALSE(fp::from_bytes<float>(truncated).is_ok());
}

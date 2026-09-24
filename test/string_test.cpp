#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace fp::str;

TEST(String, CaseTrim) {
  EXPECT_EQ(to_lower("HeLLo"), "hello");
  EXPECT_EQ(to_upper("HeLLo"), "HELLO");
  EXPECT_EQ(trim("  x  "), "x");
  EXPECT_EQ(trim_leading("xxabc", 'x'), "abc");
  EXPECT_EQ(trim_trailing("abcxx", 'x'), "abc");
  EXPECT_TRUE(trim("").empty());
}

TEST(String, SplitJoin) {
  EXPECT_EQ(split("a,b,c", ','), (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(split("a", ','), (std::vector<std::string>{"a"}));
  EXPECT_EQ(split("a--b", std::string("--")), (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(split(std::string("a,b,"), ','), (std::vector<std::string>{"a", "b"}));

  auto sv = split_view(std::string_view("x,y,z"), ',');
  ASSERT_EQ(sv.size(), 3u);
  EXPECT_EQ(sv[2], "z");

  EXPECT_EQ(join(std::vector<std::string>{"a", "b", "c"}, "-"), "a-b-c");
  EXPECT_EQ(join(std::vector<int>{1, 2, 3}, "+"), "1+2+3");
}

TEST(String, StripReplaceRepeat) {
  EXPECT_EQ(strip_prefix("foobar", "foo"), "bar");
  EXPECT_EQ(strip_prefix("foobar", "zzz"), "foobar");
  EXPECT_EQ(strip_suffix("foobar", "bar"), "foo");
  EXPECT_EQ(replace_all("a-b-a", "a", "x"), "x-b-x");
  EXPECT_EQ(repeat("ab", 3), "ababab");
  EXPECT_EQ(repeat("ab", 0), "");
}

TEST(String, LinesChunkReverse) {
  EXPECT_EQ(lines("a\nb\nc"), (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(chunk("abcdef", 2), (std::vector<std::string>{"ab", "cd", "ef"}));
  EXPECT_EQ(chunk("abcde", 2), (std::vector<std::string>{"ab", "cd", "e"}));
  EXPECT_EQ(reverse("abc"), "cba");
}

TEST(String, PadTruncateCapitalizeTitle) {
  EXPECT_EQ(pad_left("7", 3, '0'), "007");
  EXPECT_EQ(pad_right("7", 3), "7  ");
  EXPECT_EQ(truncate("hello world", 5, "..."), "he...");
  EXPECT_EQ(truncate("hi", 5, "..."), "hi");
  EXPECT_EQ(capitalize("hello"), "Hello");
  EXPECT_EQ(title("hello world"), "Hello World");
}

TEST(String, ParseNumbers) {
  EXPECT_EQ(to_int(" 42 ").value(), 42);
  EXPECT_EQ(to_int("-7").value(), -7);
  EXPECT_FALSE(to_int("nope").is_ok());
  EXPECT_FALSE(to_int("").is_ok());
  EXPECT_DOUBLE_EQ(to_double("3.5").value(), 3.5);
  EXPECT_FALSE(to_double("x").is_ok());
}

TEST(String, Affixes) {
  EXPECT_TRUE(starts_with("foobar", "foo"));
  EXPECT_FALSE(starts_with("foobar", "bar"));
  EXPECT_TRUE(ends_with("foobar", "bar"));
  EXPECT_FALSE(ends_with("foobar", "foo"));
}

TEST(String, PrecisionToString) {
  const double v = 0.1 + 0.2;
  EXPECT_DOUBLE_EQ(std::stod(to_string(v, 17)), v);
  EXPECT_EQ(to_string(42), "42");
}

TEST(String, SplitAny) {
  EXPECT_EQ(split_any("a, b;;c\t d", ",;\t "),
            (std::vector<std::string>{"a", "b", "c", "d"}));
  EXPECT_TRUE(split_any("  ,,  ", ", ").empty());
}

TEST(String, ParseNumberList) {
  auto xs = parse_numbers<double>("1, 2.5  3");
  ASSERT_TRUE(xs.is_ok());
  EXPECT_EQ(xs.value(), (std::vector<double>{1.0, 2.5, 3.0}));

  auto is = parse_numbers<int>("1 2 3");
  ASSERT_TRUE(is.is_ok());
  EXPECT_EQ(is.value(), (std::vector<int>{1, 2, 3}));

  auto bad = parse_numbers<double>("1 x 3");
  EXPECT_FALSE(bad.is_ok());
  EXPECT_NE(bad.error().find('x'), std::string::npos);
}

TEST(String, SplitCharMatchesStringDelimiter) {
  const std::string inputs[] = {"", "a", "a,", ",a", "a,,b", ",", "a,b,c", ","};
  for (std::string const &s : inputs) {
    EXPECT_EQ(split(s, ','), split(s, std::string(","))) << "input: '" << s << "'";
  }
}

TEST(String, ParseStrictness) {
  EXPECT_FALSE(to_int("42x").is_ok());   // no trailing garbage
  EXPECT_FALSE(to_int("4 2").is_ok());
  EXPECT_FALSE(to_int("0x10").is_ok());
  EXPECT_EQ(to_int("+7").value(), 7);    // from_chars needs the plus handled
  EXPECT_EQ(to_int("  -0  ").value(), 0);
  EXPECT_FALSE(to_double("1.5x").is_ok());
  EXPECT_DOUBLE_EQ(to_double("+2.5").value(), 2.5);
  EXPECT_DOUBLE_EQ(to_double("1e3").value(), 1000.0);
  EXPECT_FALSE(to_int("99999999999999999999").is_ok()); // out of range
}

TEST(String, CaseMappingLeavesNonAsciiAlone) {
  std::string s = "aA";
  s += static_cast<char>(0xC3);
  s += static_cast<char>(0xA9); // "é" in UTF-8
  EXPECT_EQ(to_upper(s), std::string("AA") + "\xC3\xA9");
  EXPECT_EQ(to_lower(s), std::string("aa") + "\xC3\xA9");
}

#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cctype>
#include <string>
#include <variant>
#include <vector>

using namespace fp;

namespace {
Parser<int> number() {
  return lexeme(map(some(digit), [](std::vector<char> cs) {
    return std::stoi(std::string(cs.begin(), cs.end()));
  }));
}
} // namespace

TEST(Parse, Primitives) {
  EXPECT_EQ(run(char_('a'), "abc").value(), 'a');
  EXPECT_EQ(run(string_("hello"), "hello world").value(), "hello");
  EXPECT_EQ(run(digit, "5").value(), '5');
  EXPECT_FALSE(run(digit, "x").is_ok());
  EXPECT_EQ(run(satisfy([](char c) { return c == 'z'; }), "z").value(), 'z');
  EXPECT_EQ(run(one_of('a', 'b'), "b").value(), 'b');
  EXPECT_EQ(run(none_of('a', 'b'), "c").value(), 'c');
}

TEST(Parse, RepetitionAndChoice) {
  EXPECT_EQ(run(some(digit), "123").value().size(), 3u);
  EXPECT_EQ(run(many(char_('a')), "aab").value().size(), 2u);
  EXPECT_TRUE(run(many(char_('a')), "b").is_ok());
  EXPECT_EQ(run(many1(digit), "12").value().size(), 2u);
  EXPECT_FALSE(run(many1(digit), "x").is_ok());
  EXPECT_EQ(run(choice(keyword("yes"), keyword("no")), "no").value(), "no");
  EXPECT_EQ(run(optional(digit), "x").value(), std::nullopt);
  EXPECT_EQ(run(optional(digit), "7").value().value(), '7');
  EXPECT_EQ(run(sep_by(number(), symbol(',')), "1, 2, 3").value(),
            (std::vector<int>{1, 2, 3}));
}

TEST(Parse, Sequencing) {
  auto kv = seq(lexeme(map(some(letter), [](std::vector<char> cs) {
                 return std::string(cs.begin(), cs.end());
               })),
               preceded(symbol('='), number()));
  EXPECT_EQ(run(kv, "size = 42").value(), std::make_pair(std::string("size"), 42));

  auto br = between(symbol('['), symbol(']'), number());
  EXPECT_EQ(run(br, "[ 7 ]").value(), 7);

  EXPECT_EQ(run(preceded(symbol('$'), number()), "$5").value(), 5);
  EXPECT_EQ(run(terminated(number(), symbol('!')), "5!").value(), 5);
  EXPECT_EQ(run(char_('a') >> char_('b'), "ab").value(), 'b');
  EXPECT_EQ(run(char_('a') << char_('b'), "ab").value(), 'a');
  EXPECT_EQ(run(char_('a') | char_('b'), "b").value(), 'b');
}

TEST(Parse, SepByAndOperators) {
  EXPECT_EQ(run(number() % symbol(','), "1,2,3").value(), (std::vector<int>{1, 2, 3}));
  auto letters = lexeme(map(some(letter), [](std::vector<char> cs) {
    return std::string(cs.begin(), cs.end());
  }));
  EXPECT_EQ(run(*letters, "abc").value().size(), 1u);

  auto bound = lexeme(digit) >>= [](char c) { return succeed(c - '0'); };
  EXPECT_EQ(run(bound, "9").value(), 9);
}

TEST(Parse, EofPeekNotLabel) {
  EXPECT_TRUE(run(char_('a') << eof, "a").is_ok());
  EXPECT_FALSE(run(char_('a') << eof, "ab").is_ok());
  EXPECT_EQ(run(peek(char_('a')), "abc").value(), 'a');
  EXPECT_EQ(run(peek(char_('a')) >> char_('a'), "abc").value(), 'a');
  EXPECT_TRUE(run(not_followed(digit) >> char_('a'), "a").is_ok());
  EXPECT_FALSE(run(not_followed(digit), "5").is_ok());

  auto lr = run(label(digit, "a digit"), "x");
  ASSERT_FALSE(lr.is_ok());
  EXPECT_NE(lr.error().find("a digit"), std::string::npos);

  auto cr = run(context(char_('a'), "header"), "x");
  ASSERT_FALSE(cr.is_ok());
  EXPECT_NE(cr.error().find("header"), std::string::npos);
}

TEST(Parse, ErrorPositions) {
  auto r = run(lexeme(digit), "ab");
  ASSERT_FALSE(r.is_ok());
  EXPECT_NE(r.error().find("line 1, col 1"), std::string::npos);

  auto r2 = run(char_('a') >> char_('\n') >> char_('b'), "a\nx");
  ASSERT_FALSE(r2.is_ok());
  EXPECT_NE(r2.error().find("line 2, col 1"), std::string::npos);
}

TEST(Parse, Chainl1) {
  auto plus_op = map(symbol('+'), [](char) {
    return std::function<int(int, int)>([](int a, int b) { return a + b; });
  });
  EXPECT_EQ(run(chainl1(number(), plus_op), "1 + 2 + 3").value(), 6);
}

TEST(Parse, RecursiveJson) {
  struct JsonNull {};
  struct Json {
    using Array = std::vector<Json>;
    std::variant<JsonNull, double, std::string, Array> v;
  };

  Parser<std::string> jstr = lexeme(map(
      char_('"') >> *none_of('"') << char_('"'),
      [](std::vector<char> cs) { return std::string(cs.begin(), cs.end()); }));
  Parser<double> jnum = map(number(), [](int x) { return (double)x; });

  Parser<Json> value;
  auto jarr = map(symbol('[') >> ref(value) % symbol(',') << symbol(']'),
                  [](Json::Array xs) { return Json{xs}; });
  value = preceded(whitespace(),
                   map(jnum, [](double d) { return Json{d}; }) |
                       map(jstr, [](std::string s) { return Json{s}; }) | jarr);

  auto r = run(value, R"([1, 2, [3, 4]])");
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(std::get<Json::Array>(r.value().v).size(), 3u);
}

TEST(Parse, ScanWhileScansTokens) {
  auto is_digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
  auto token = fp::scan_while1(is_digit);
  auto r = token("1234abc", 0);
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(r.value().first, "1234");
  EXPECT_EQ(r.value().second, "abc");

  // take_while may match nothing; take_while1 must not
  auto opt = fp::scan_while(is_digit);
  auto empty = opt("abc", 0);
  ASSERT_TRUE(empty.is_ok());
  EXPECT_TRUE(empty.value().first.empty());
  EXPECT_FALSE(token("abc", 0).is_ok());

  // a full token list, without the vector<char> that many() would build
  auto number = fp::map(token, [](std::string_view ds) {
    unsigned v = 0;
    for (char c : ds)
      v = v * 10 + static_cast<unsigned>(c - '0');
    return v;
  });
  auto list = fp::sep_by(number, fp::char_(','));
  auto parsed = list("12,7,900", 0);
  ASSERT_TRUE(parsed.is_ok());
  EXPECT_EQ(parsed.value().first, (std::vector<unsigned>{12, 7, 900}));
  EXPECT_TRUE(parsed.value().second.empty());
}

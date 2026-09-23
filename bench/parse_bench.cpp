// Parser combinators vs a hand-written parser for the same grammar.
//
// Grammar: a comma-separated list of unsigned integers, e.g. "12,7,900,...".
// This measures what fp::Parser's type erasure (one std::function per
// combinator, one indirect call per position) costs compared with direct code.
#include <fp/parse.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "bench.hpp"

namespace {

std::string make_input(std::size_t values) {
  std::string s;
  s.reserve(values * 5);
  for (std::size_t i = 0; i < values; ++i) {
    if (i)
      s += ',';
    s += std::to_string((i * 7919) % 100'000);
  }
  return s;
}

// Hand-written: skip nothing, parse digits, expect ',' or end.
bool hand_parse(std::string_view s, std::vector<unsigned> &out) {
  out.clear();
  std::size_t i = 0;
  while (i < s.size()) {
    if (!std::isdigit(static_cast<unsigned char>(s[i])))
      return false;
    unsigned v = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
      v = v * 10 + static_cast<unsigned>(s[i++] - '0');
    out.push_back(v);
    if (i < s.size()) {
      if (s[i] != ',')
        return false;
      ++i;
    }
  }
  return true;
}

fp::Parser<unsigned> number() {
  using namespace fp;
  // scan_while1 yields a string_view: no vector<char> per token.
  return map(scan_while1([](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }),
             [](std::string_view ds) {
               unsigned v = 0;
               for (char c : ds)
                 v = v * 10 + static_cast<unsigned>(c - '0');
               return v;
             });
}

fp::Parser<std::vector<unsigned>> fp_list() {
  using namespace fp;
  return sep_by(number(), char_(','));
}

} // namespace

int main() {
  const std::string input = make_input(2000);
  std::vector<unsigned> hand_out;
  std::vector<unsigned> fp_out;

  bench::measure("parse 2k ints: hand-written", [&] {
    if (!hand_parse(input, hand_out))
      std::abort();
    bench::keep(hand_out.data());
  });

  const auto parser = fp_list();
  bench::measure("parse 2k ints: fp::Parser", [&] {
    auto r = parser(input);
    if (!r.is_ok())
      std::abort();
    fp_out = r.value().first;
    bench::keep(fp_out.data());
  });

  if (hand_out != fp_out)
    std::printf("MISMATCH: results differ\n");
  else
    std::printf("both parsed %zu values identically\n", fp_out.size());
}

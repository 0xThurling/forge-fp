// Benchmark serialization against hand-written to_chars/from_chars.
#include <fp/serialize.hpp>

#include <charconv>
#include <cstdio>
#include <string>
#include <vector>

#include "bench.hpp"

int main() {
  std::vector<double> values(10'000);
  for (std::size_t i = 0; i < values.size(); ++i)
    values[i] = 0.1 * static_cast<double>(i % 977) - 48.0;

  const std::string text = fp::to_text(values);
  std::string sink_str;
  std::size_t sink_n = 0;

  bench::measure("to_text 10k doubles: fp", [&] {
    sink_str = fp::to_text(values);
    bench::keep(&sink_str);
  });
  bench::measure("to_text 10k doubles: hand to_chars", [&] {
    std::string out;
    out.reserve(values.size() * 20);
    char buf[32];
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i)
        out += ' ';
      auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), values[i]);
      (void)ec;
      out.append(buf, p);
    }
    sink_str = std::move(out);
    bench::keep(&sink_str);
  });

  bench::measure("from_text 10k doubles: fp", [&] {
    auto r = fp::from_text<double>(text);
    sink_n = r.is_ok() ? r.value().size() : 0;
    bench::keep(&sink_n);
  });
  bench::measure("from_text 10k doubles: hand from_chars", [&] {
    std::vector<double> out;
    out.reserve(values.size());
    const char *p = text.data();
    const char *end = p + text.size();
    while (p < end) {
      while (p < end && *p == ' ')
        ++p;
      if (p >= end)
        break;
      double v = 0.0;
      auto [next, ec] = std::from_chars(p, end, v);
      if (ec != std::errc{})
        break;
      out.push_back(v);
      p = next;
    }
    sink_n = out.size();
    bench::keep(&sink_n);
  });

  bench::measure("to_bytes 10k doubles: fp", [&] {
    auto b = fp::to_bytes(values);
    sink_n = b.size();
    bench::keep(&sink_n);
  });
  bench::measure("from_bytes 10k doubles: fp", [&] {
    static const auto bytes = fp::to_bytes(values);
    auto r = fp::from_bytes<double>(bytes);
    sink_n = r.is_ok() ? r.value().size() : 0;
    bench::keep(&sink_n);
  });
}

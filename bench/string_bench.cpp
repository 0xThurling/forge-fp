// Benchmark the string helpers against hand-written equivalents.
#include <fp/string.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "bench.hpp"

namespace {
std::string csv(std::size_t fields) {
  std::string s;
  s.reserve(fields * 8);
  for (std::size_t i = 0; i < fields; ++i) {
    if (i)
      s += ',';
    s += std::to_string(i * 7919 % 100000);
  }
  return s;
}

std::string lines_text(std::size_t n) {
  std::string s;
  s.reserve(n * 12);
  for (std::size_t i = 0; i < n; ++i)
    s += "line " + std::to_string(i) + "\n";
  return s;
}

std::vector<std::string> parts(std::size_t n) {
  std::vector<std::string> v;
  v.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
    v.push_back("field" + std::to_string(i));
  return v;
}

std::vector<int> numbers(std::size_t n) {
  std::vector<int> v;
  v.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
    v.push_back(static_cast<int>(i * 7919 % 100000));
  return v;
}
} // namespace

int main() {
  const std::string data = csv(100'000);      // ~600 KB, 100k fields
  const std::string text = lines_text(50'000); // 50k lines
  const auto words = parts(100'000);
  const auto nums = numbers(1'000);

  std::size_t sink_size = 0;
  std::string sink_str;

  // --- split: char delimiter ---
  bench::measure("split(char) 100k fields: fp", [&] {
    auto v = fp::str::split(data, ',');
    sink_size = v.size();
    bench::keep(&sink_size);
  });
  bench::measure("split(char) 100k fields: hand", [&] {
    std::vector<std::string> out;
    out.reserve(data.size() / 8 + 1);
    std::size_t start = 0, pos;
    while ((pos = data.find(',', start)) != std::string::npos) {
      out.push_back(data.substr(start, pos - start));
      start = pos + 1;
    }
    if (start < data.size())
      out.push_back(data.substr(start));
    sink_size = out.size();
    bench::keep(&sink_size);
  });
  bench::measure("split(char) 100k fields: views", [&] {
    auto v = fp::str::split_view(data, ',');
    sink_size = v.size();
    bench::keep(&sink_size);
  });

  // --- lines ---
  bench::measure("lines() 50k: fp", [&] {
    auto v = fp::str::lines(text);
    sink_size = v.size();
    bench::keep(&sink_size);
  });
  bench::measure("lines() 50k: hand", [&] {
    std::vector<std::string> out;
    std::size_t start = 0, pos;
    while ((pos = text.find('\n', start)) != std::string::npos) {
      out.push_back(text.substr(start, pos - start));
      start = pos + 1;
    }
    if (start < text.size())
      out.push_back(text.substr(start));
    sink_size = out.size();
    bench::keep(&sink_size);
  });

  // --- join ---
  bench::measure("join(vector<string>) 100k: fp", [&] {
    sink_str = fp::str::join(words, ",");
    bench::keep(&sink_str);
  });
  bench::measure("join(vector<string>) 100k: hand", [&] {
    std::size_t total = 0;
    for (auto const &w : words)
      total += w.size() + 1;
    std::string out;
    out.reserve(total);
    for (std::size_t i = 0; i < words.size(); ++i) {
      if (i)
        out += ',';
      out += words[i];
    }
    sink_str = std::move(out);
    bench::keep(&sink_str);
  });
  bench::measure("join(range<int>) 1k: fp", [&] {
    sink_str = fp::str::join(nums, ",");
    bench::keep(&sink_str);
  });

  // --- parsing numbers ---
  bench::measure("to_int: fp", [&] {
    auto r = fp::str::to_int("1234567");
    sink_size = r.is_ok() ? static_cast<std::size_t>(r.value()) : 0;
    bench::keep(&sink_size);
  });
  bench::measure("to_int: from_chars hand", [&] {
    int v = 0;
    auto [p, ec] = std::from_chars("1234567", "1234567" + 7, v);
    (void)p;
    sink_size = ec == std::errc{} ? static_cast<std::size_t>(v) : 0;
    bench::keep(&sink_size);
  });
  bench::measure("to_int invalid: fp", [&] {
    auto r = fp::str::to_int("abc");
    sink_size = r.is_ok() ? 1u : 0u;
    bench::keep(&sink_size);
  });
  bench::measure("to_double: fp", [&] {
    auto r = fp::str::to_double("3.14159265358979");
    sink_size = r.is_ok() ? 1u : 0u;
    bench::keep(&sink_size);
  });
  bench::measure("to_double invalid: fp", [&] {
    auto r = fp::str::to_double("abc");
    sink_size = r.is_ok() ? 1u : 0u;
    bench::keep(&sink_size);
  });

  // --- replace_all / trim / case ---
  const std::string template_str = "the quick brown fox jumps over the lazy dog, "
                                   "the quick brown fox jumps over the lazy dog";
  bench::measure("replace_all (4 hits): fp", [&] {
    sink_str = fp::str::replace_all(template_str, "the", "a");
    bench::keep(&sink_str);
  });
  bench::measure("replace_all (4 hits): hand", [&] {
    std::string out;
    out.reserve(template_str.size());
    std::size_t start = 0, pos;
    while ((pos = template_str.find("the", start)) != std::string::npos) {
      out.append(template_str, start, pos - start);
      out += 'a';
      start = pos + 3;
    }
    out.append(template_str, start, std::string::npos);
    sink_str = std::move(out);
    bench::keep(&sink_str);
  });
  bench::measure("trim: fp", [&] {
    sink_str = fp::str::trim("   hello world   ");
    bench::keep(&sink_str);
  });
  bench::measure("to_lower 600KB: fp", [&] {
    sink_str = fp::str::to_lower(data);
    bench::keep(&sink_str);
  });
  bench::measure("to_string(double): fp", [&] {
    sink_str = fp::str::to_string(3.1415926535897931, 17);
    bench::keep(&sink_str);
  });
}

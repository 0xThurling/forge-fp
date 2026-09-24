// Benchmark the eager combinators against hand-written loops, and the
// vec/ranges/sort_by_cached variants against each other.
#include <fp/ranges.hpp>
#include <fp/vec.hpp>
#include <fp/views.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "bench.hpp"

int main() {
  constexpr std::size_t n = 1 << 20;
  std::vector<int> v(n);
  for (std::size_t i = 0; i < n; ++i)
    v[i] = static_cast<int>((i * 2654435761u) % 1000);

  std::vector<std::string> words;
  words.reserve(200'000);
  for (std::size_t i = 0; i < 200'000; ++i)
    words.push_back("word" + std::to_string((i * 7919) % 50'000));

  std::size_t sink = 0;

  bench::measure("map 1M: fp::map", [&] {
    auto r = fp::map(v, [](int x) { return x * 2 + 1; });
    sink = r[0];
    bench::keep(&sink);
  });
  bench::measure("map 1M: hand loop", [&] {
    std::vector<int> out;
    out.reserve(v.size());
    for (int x : v)
      out.push_back(x * 2 + 1);
    sink = out[0];
    bench::keep(&sink);
  });
  bench::measure("filter 1M: fp::filter", [&] {
    auto r = fp::filter(v, [](int x) { return x % 3 == 0; });
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("flat_map 1M: fp::flat_map", [&] {
    auto r = fp::flat_map(v, [](int x) { return std::vector<int>{x, x + 1}; });
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("scan 1M: fp::scan", [&] {
    auto r = fp::scan(v, 0, [](int a, int b) { return a + b; });
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("enumerate 1M: fp::enumerate", [&] {
    auto r = fp::enumerate(v);
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("zip 1M: fp::zip", [&] {
    auto r = fp::zip(v, v);
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("chunk 1M/64: fp::chunk", [&] {
    auto r = fp::chunk(v, 64);
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("windows 1M/16: fp::windows", [&] {
    auto r = fp::windows(v, 16);
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("unique 1M: fp::unique", [&] {
    auto r = fp::unique(v);
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("sort 1M: fp::sort", [&] {
    auto r = fp::sort(v);
    sink = r.size();
    bench::keep(&sink);
  });

  // --- group_by ---
  bench::measure("group_by 1M (1k keys): fp", [&] {
    auto r = fp::group_by(v, [](int x) { return x % 1000; });
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("group_by 1M (1k keys): hand", [&] {
    std::unordered_map<int, std::vector<int>> r;
    for (int x : v)
      r[x % 1000].push_back(x);
    sink = r.size();
    bench::keep(&sink);
  });

  // --- sort_by: cheap vs expensive key ---
  bench::measure("sort_by 200k strings (len): fp", [&] {
    auto r = fp::sort_by(words, [](std::string const &s) { return s.size(); });
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("sort_by_cached 200k strings (len): fp", [&] {
    auto r =
        fp::sort_by_cached(words, [](std::string const &s) { return s.size(); });
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("sort_by 200k strings (copy): fp", [&] {
    auto r = fp::sort_by(words, [](std::string const &s) { return s; });
    sink = r.size();
    bench::keep(&sink);
  });
  bench::measure("sort_by_cached 200k strings (copy): fp", [&] {
    auto r = fp::sort_by_cached(words, [](std::string const &s) { return s; });
    sink = r.size();
    bench::keep(&sink);
  });

  // --- lazy views vs eager, 3-stage pipeline ---
  bench::measure("3-stage map/filter/map: eager", [&] {
    auto a = fp::map(v, [](int x) { return x * 2; });
    auto b = fp::filter(a, [](int x) { return x % 3 == 0; });
    auto c = fp::map(b, [](int x) { return x + 1; });
    sink = c.size();
    bench::keep(&sink);
  });
  bench::measure("3-stage map/filter/map: lazy views", [&] {
    auto c = fp::to_vector(v | fp::views::map([](int x) { return x * 2; }) |
                           fp::views::filter([](int x) { return x % 3 == 0; }) |
                           fp::views::map([](int x) { return x + 1; }));
    sink = c.size();
    bench::keep(&sink);
  });
  bench::measure("3-stage map/filter/map: hand loop", [&] {
    std::vector<int> c;
    c.reserve(v.size());
    for (int x : v) {
      const int y = x * 2;
      if (y % 3 == 0)
        c.push_back(y + 1);
    }
    sink = c.size();
    bench::keep(&sink);
  });
}

// Benchmark the io helpers against hand-written stream code.
#include <fp/input.hpp>
#include <fp/io.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "bench.hpp"

int main() {
  const std::string path = "/tmp/opencode/io_bench_data.bin";
  const std::string text_path = "/tmp/opencode/io_bench_lines.txt";
  const std::string payload(8 * 1024 * 1024, 'x');

  std::string text;
  for (int i = 0; i < 100'000; ++i)
    text += "line " + std::to_string(i) + " with some payload\n";
  if (!fp::write_file(path, payload).is_ok() ||
      !fp::write_file(text_path, text).is_ok()) {
    std::printf("setup failed\n");
    return 1;
  }

  std::size_t sink = 0;
  std::string sink_str;

  bench::measure("read_file 8MB: fp", [&] {
    auto r = fp::read_file(path);
    sink = r.is_ok() ? r.value().size() : 0;
    bench::keep(&sink);
  });
  bench::measure("read_file 8MB: istreambuf_iterator", [&] {
    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    sink = content.size();
    bench::keep(&sink);
  });
  bench::measure("read_bytes 8MB: fp", [&] {
    auto r = fp::read_bytes(path);
    sink = r.is_ok() ? r.value().size() : 0;
    bench::keep(&sink);
  });
  bench::measure("read_lines 100k: fp", [&] {
    auto r = fp::read_lines(text_path);
    sink = r.is_ok() ? r.value().size() : 0;
    bench::keep(&sink);
  });
  bench::measure("read_lines 100k: getline loop", [&] {
    std::ifstream in(text_path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
      lines.push_back(line);
    sink = lines.size();
    bench::keep(&sink);
  });
  bench::measure("write_file 8MB: fp", [&] {
    auto r = fp::write_file(path, payload);
    sink = r.is_ok() ? payload.size() : 0;
    bench::keep(&sink);
  });
  bench::measure("read_all(ifstream) 8MB: fp", [&] {
    std::ifstream in(path, std::ios::binary);
    auto r = fp::read_all(in);
    sink = r.is_ok() ? r.value().size() : 0;
    bench::keep(&sink);
  });
}

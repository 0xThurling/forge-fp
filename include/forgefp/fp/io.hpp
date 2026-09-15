#pragma once

#include "fp/result.hpp"
#include <fstream>
#include <iterator>
#include <string_view>
#include <type_traits>

namespace fp {
inline Result<std::string> read_file(std::string const &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return err<std::string>("cannot open " + path);

  std::string content((std::istream_iterator<char>(in)),
                      std::istream_iterator<char>());

  if (in.bad())
    return err<std::string>("read failed: " + path);
  return ok(std::move(content));
}

inline Result<std::vector<std::string>> read_lines(std::string const &path) {
  std::ifstream in(path);
  if (!in)
    return err<std::vector<std::string>>("cannot open " + path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line))
    lines.push_back(std::move(line));
  return ok(std::move(lines));
}

inline Result<void> write_file(std::string const &path,
                               std::string_view content) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    return err<void>("cannot open " + path);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out)
    return err<void>("write failed " + path);
  return ok<void>();
}

template <class F>
auto lift_io(F f) {
    return [f = std::move(f)] (auto &&... args) -> Result<std::invoke_result_t<F, decltype(args)...>> {
        using R = std::invoke_result_t<F, decltype(args)...>;
        r
    }
}
} // namespace fp

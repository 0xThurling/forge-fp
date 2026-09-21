#pragma once

#include "forgefp/fp/result.hpp"
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace fp {
inline Result<std::string> read_file(std::string const &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return err<std::string>("cannot open " + path);

  // istreambuf_iterator reads raw bytes; istream_iterator<char> would skip
  // whitespace (including newlines).
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());

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

template <class F> auto lift_io(F f) {
  return [f = std::move(f)](auto &&...args)
             -> Result<std::invoke_result_t<F, decltype(args)...>> {
    using R = std::invoke_result_t<F, decltype(args)...>;
    try {
      return ok(f(std::forward<decltype(args)>(args)...));
    } catch (std::exception const &e) {
      return err<R>(e.what());
    } catch (...) {
      return err<R>("unknown IO error");
    }
  };
}

inline void interact(std::function<std::string(std::string)> f) {
  std::string in((std::istream_iterator<char>(std::cin)),
                 std::istream_iterator<char>());

  std::cout << f(std::move(in));
}

// True when the file exists and can be opened for reading.
inline bool exists(std::string const &path) {
  std::ifstream in(path, std::ios::binary);
  return static_cast<bool>(in);
}

inline Result<std::vector<std::byte>> read_bytes(std::string const &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return err<std::vector<std::byte>>("cannot open " + path);

  const std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  if (in.bad())
    return err<std::vector<std::byte>>("read failed: " + path);

  std::vector<std::byte> out(content.size());
  if (!content.empty())
    std::memcpy(out.data(), content.data(), content.size());
  return ok(std::move(out));
}

inline Result<void> write_bytes(std::string const &path,
                                std::span<std::byte const> data) {
  std::ofstream out(path, std::ios::binary);
  if (!out)
    return err<void>("cannot open " + path);

  out.write(reinterpret_cast<char const *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  if (!out)
    return err<void>("write failed " + path);
  return ok<void>();
}

inline Result<void> write_lines(std::string const &path,
                                std::vector<std::string> const &lines) {
  std::string content;
  for (auto const &line : lines) {
    content += line;
    content += '\n';
  }
  return write_file(path, content);
}

// Like `mkdir -p`; idempotent.
inline Result<void> ensure_directory(std::string const &path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec)
    return err<void>("cannot create directory " + path + ": " + ec.message());
  return ok<void>();
}
} // namespace fp

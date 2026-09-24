#pragma once

#include "result.hpp"
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
[[nodiscard]] inline Result<std::string> read_file(std::string_view path) {
  std::ifstream in{std::filesystem::path(path), std::ios::binary};
  if (!in)
    return err<std::string>("cannot open " + std::string(path));

  std::string content;
  // Seekable input (the common case): size the buffer, then one bulk read.
  // Reading through istreambuf_iterator instead costs a virtual call per byte.
  if (in.seekg(0, std::ios::end)) {
    const auto size = in.tellg();
    if (size > 0) {
      content.resize(static_cast<std::size_t>(size));
      in.seekg(0, std::ios::beg);
      in.read(content.data(), static_cast<std::streamsize>(content.size()));
      content.resize(static_cast<std::size_t>(in.gcount()));
    }
  } else {
    // Not seekable (a pipe, /proc, ...): clear the failed seek and stream it.
    in.clear();
    content.assign(std::istreambuf_iterator<char>(in),
                   std::istreambuf_iterator<char>());
  }

  if (in.bad())
    return err<std::string>("read failed: " + std::string(path));
  return ok(std::move(content));
}

[[nodiscard]] inline Result<std::vector<std::string>> read_lines(std::string_view path) {
  std::ifstream in{std::filesystem::path(path)};
  if (!in)
    return err<std::vector<std::string>>("cannot open " + std::string(path));
  std::vector<std::string> lines;
  lines.reserve(64); // heuristic; grows by doubling
  std::string line;
  while (std::getline(in, line))
    lines.push_back(std::move(line));
  return ok(std::move(lines));
}

[[nodiscard]] inline Result<void> write_file(std::string_view path,
                               std::string_view content) {
  std::ofstream out{std::filesystem::path(path), std::ios::binary};
  if (!out)
    return err<void>("cannot open " + std::string(path));
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!out)
    return err<void>("write failed " + std::string(path));
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
inline bool exists(std::string_view path) {
  std::ifstream in{std::filesystem::path(path), std::ios::binary};
  return static_cast<bool>(in);
}

[[nodiscard]] inline Result<std::vector<std::byte>> read_bytes(std::string_view path) {
  std::ifstream in{std::filesystem::path(path), std::ios::binary};
  if (!in)
    return err<std::vector<std::byte>>("cannot open " + std::string(path));

  // Same bulk path as read_file: size the buffer and read once. The old
  // version streamed through istreambuf_iterator into a std::string and then
  // memcpy'd into the vector (two buffers, one virtual call per byte).
  std::vector<std::byte> out;
  if (in.seekg(0, std::ios::end)) {
    const auto size = in.tellg();
    if (size > 0) {
      out.resize(static_cast<std::size_t>(size));
      in.seekg(0, std::ios::beg);
      in.read(reinterpret_cast<char *>(out.data()),
              static_cast<std::streamsize>(out.size()));
      out.resize(static_cast<std::size_t>(in.gcount()));
    }
  } else {
    in.clear();
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    out.resize(content.size());
    if (!content.empty())
      std::memcpy(out.data(), content.data(), content.size());
  }
  if (in.bad())
    return err<std::vector<std::byte>>("read failed: " + std::string(path));
  return ok(std::move(out));
}

[[nodiscard]] inline Result<void> write_bytes(std::string_view path,
                                std::span<std::byte const> data) {
  std::ofstream out{std::filesystem::path(path), std::ios::binary};
  if (!out)
    return err<void>("cannot open " + std::string(path));

  out.write(reinterpret_cast<char const *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  if (!out)
    return err<void>("write failed " + std::string(path));
  return ok<void>();
}

[[nodiscard]] inline Result<void> write_lines(std::string_view path,
                                std::vector<std::string> const &lines) {
  std::string content;
  for (auto const &line : lines) {
    content += line;
    content += '\n';
  }
  return write_file(path, content);
}

// Like `mkdir -p`; idempotent.
[[nodiscard]] inline Result<void> ensure_directory(std::string_view path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec)
    return err<void>("cannot create directory " + std::string(path) + ": " +
                     ec.message());
  return ok<void>();
}
} // namespace fp

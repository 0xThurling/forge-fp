#pragma once
#include "forgefp/fp/result.hpp"
#include <concepts>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fp {

// Generic round-trip for ranges of arithmetic values. fp owns the encoding;
// callers own the schema (which vector is which parameter).

template <std::ranges::range R>
  requires std::is_arithmetic_v<std::ranges::range_value_t<R>>
std::string to_text(R const &r, int precision = 17) {
  std::ostringstream os;
  os << std::setprecision(precision);
  bool first = true;
  for (auto const &x : r) {
    if (!first)
      os << ' ';
    os << x;
    first = false;
  }
  return os.str();
}

template <class T>
  requires std::is_arithmetic_v<T>
Result<std::vector<T>> from_text(std::string_view text) {
  std::istringstream is{std::string(text)};
  std::vector<T> out;
  T value{};
  while (is >> value)
    out.push_back(value);
  if (!is.eof())
    return err<std::vector<T>>("from_text: invalid number");
  return ok(std::move(out));
}

template <std::ranges::contiguous_range R>
  requires std::is_trivially_copyable_v<std::ranges::range_value_t<R>>
std::vector<std::byte> to_bytes(R const &r) {
  using T = std::ranges::range_value_t<R>;
  const auto *p = reinterpret_cast<std::byte const *>(std::ranges::data(r));
  return {p, p + std::ranges::size(r) * sizeof(T)};
}

template <class T>
  requires std::is_trivially_copyable_v<T>
Result<std::vector<T>> from_bytes(std::span<std::byte const> data) {
  if (data.size() % sizeof(T) != 0)
    return err<std::vector<T>>(
        "from_bytes: size is not a multiple of the element size");

  std::vector<T> out(data.size() / sizeof(T));
  if (!out.empty())
    std::memcpy(out.data(), data.data(), data.size());
  return ok(std::move(out));
}

} // namespace fp

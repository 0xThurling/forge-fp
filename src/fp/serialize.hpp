#pragma once
#include "result.hpp"
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fp {

// Generic round-trip for ranges of arithmetic values. fp owns the encoding;
// callers own the schema (which vector is which parameter).

// Text formatting uses to_chars (a plain ostringstream cost ~250ns per double).
// `precision` is significant digits, matching ostream's setprecision.
template <std::ranges::range R>
  requires std::is_arithmetic_v<std::ranges::range_value_t<R>>
std::string to_text(R const &r, int precision = 17) {
  using T = std::ranges::range_value_t<R>;
  std::string out;
  if constexpr (std::ranges::sized_range<R>)
    out.reserve(std::ranges::size(r) * (sizeof(T) > 4 ? 24 : 12));
  char buf[64];
  bool first = true;
  for (auto const &x : r) {
    if (!first)
      out += ' ';
    if constexpr (std::is_same_v<T, char> || std::is_same_v<T, signed char> ||
                  std::is_same_v<T, unsigned char>) {
      out += static_cast<char>(x); // character semantics, like ostream
    } else if constexpr (std::is_floating_point_v<T>) {
      const auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), x,
                                         std::chars_format::general, precision);
      if (ec == std::errc{})
        out.append(buf, p);
    } else {
      const auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), x);
      if (ec == std::errc{})
        out.append(buf, p);
    }
    first = false;
  }
  return out;
}

// Whitespace-separated numbers, parsed with from_chars: no stream copy, no
// locale, no exceptions. The whole text must be numbers (the old istringstream
// version behaved the same).
template <class T>
  requires std::is_arithmetic_v<T>
[[nodiscard]] Result<std::vector<T>> from_text(std::string_view text) {
  std::vector<T> out;
  out.reserve(text.size() / 8 + 1);
  const char *p = text.data();
  const char *end = p + text.size();
  constexpr std::string_view ws = " \t\n\v\f\r";
  for (;;) {
    while (p < end && ws.find(*p) != std::string_view::npos)
      ++p;
    if (p == end)
      break;
    if (*p == '+')
      ++p; // from_chars does not accept a leading plus
    T value{};
    const auto [next, ec] = std::from_chars(p, end, value);
    if (ec == std::errc::result_out_of_range)
      return err<std::vector<T>>("from_text: value out of range");
    if (ec != std::errc{} || next == p)
      return err<std::vector<T>>("from_text: invalid number");
    out.push_back(value);
    p = next;
  }
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
[[nodiscard]] Result<std::vector<T>> from_bytes(std::span<std::byte const> data) {
  if (data.size() % sizeof(T) != 0)
    return err<std::vector<T>>(
        "from_bytes: size is not a multiple of the element size");

  std::vector<T> out(data.size() / sizeof(T));
  if (!out.empty())
    std::memcpy(out.data(), data.data(), data.size());
  return ok(std::move(out));
}

} // namespace fp

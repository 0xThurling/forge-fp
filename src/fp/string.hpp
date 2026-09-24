#pragma once
#include "result.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp::str {
// ASCII case mapping. The C locale (the default, and the only one this library
// uses) maps ASCII and leaves every other byte alone — a range check is
// exactly equivalent to std::tolower and skips the per-character locale call.
inline std::string to_lower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

inline std::string to_upper(std::string s) {
  for (char &c : s)
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
  return s;
}

inline std::string trim_trailing(std::string s, char c = ' ') {
  while (!s.empty() && s.back() == c)
    s.pop_back();
  return s;
}

inline std::string trim_leading(std::string s, char c = ' ') {
  s.erase(0, s.find_first_not_of(c));
  return s;
}

inline std::string trim(std::string s) {
  return trim_trailing(trim_leading(std::move(s)));
}

// Splits on a single character. Uses the same find/substr loop as the string
// overload: a stringstream + getline costs ~7x more and allocates a buffer.
inline std::vector<std::string> split(std::string const &s, char delim) {
  std::vector<std::string> out;
  out.reserve(s.size() / 8 + 1); // heuristic: fields average >= 8 chars
  size_t start = 0, pos;
  while ((pos = s.find(delim, start)) != std::string::npos) {
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  if (start < s.size())
    out.push_back(s.substr(start));
  return out;
}

inline std::vector<std::string> split(std::string const &s,
                                      std::string const &delim) {
  std::vector<std::string> out;
  if (delim.empty()) {
    out.push_back(s);
    return out;
  }
  out.reserve(s.size() / 8 + 1); // heuristic: fields average >= 8 chars
  size_t start = 0, pos;
  while ((pos = s.find(delim, start)) != std::string::npos) {
    out.push_back(s.substr(start, pos - start));
    start = pos + delim.size();
  }
  if (start < s.size())
    out.push_back(s.substr(start));
  return out;
}

// Non-owning split: the pieces are views into `s`. A trailing delimiter is
// dropped, matching the owning `split`.
inline std::vector<std::string_view> split_view(std::string_view s, char delim) {
  std::vector<std::string_view> out;
  if (s.empty())
    return out;
  out.reserve(s.size() / 8 + 1);
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == delim) {
      out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < s.size())
    out.push_back(s.substr(start));
  return out;
}

inline std::vector<std::string_view> split_view(std::string_view s,
                                                std::string_view delim) {
  std::vector<std::string_view> out;
  if (delim.empty()) {
    out.push_back(s);
    return out;
  }
  size_t start = 0, pos;
  while ((pos = s.find(delim, start)) != std::string_view::npos) {
    out.push_back(s.substr(start, pos - start));
    start = pos + delim.size();
  }
  if (start < s.size())
    out.push_back(s.substr(start));
  return out;
}

inline std::string join(std::vector<std::string> const &parts,
                        std::string const &sep) {
  std::size_t total = parts.empty() ? 0 : sep.size() * (parts.size() - 1);
  for (auto const &p : parts)
    total += p.size();
  std::string out;
  out.reserve(total);
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) {
      out += sep;
    }
    out += parts[i];
  }
  return out;
}

inline std::string strip_prefix(std::string s, std::string const &prefix) {
  if (s.rfind(prefix, 0) == 0)
    s.erase(0, prefix.size());
  return s;
}

inline std::string strip_suffix(std::string s, std::string const &suffix) {
  if (s.size() >= suffix.size() &&
      s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
    s.erase(s.size() - suffix.size());
  return s;
}

// Replaces every occurrence in one pass. The old version called
// std::string::replace per hit, shifting the tail each time (O(n*k)); this
// rebuilds into a reserved buffer (O(n)).
inline std::string replace_all(std::string s, std::string const &from,
                               std::string const &to) {
  if (from.empty())
    return s;
  std::string out;
  out.reserve(s.size());
  size_t start = 0, pos;
  while ((pos = s.find(from, start)) != std::string::npos) {
    out.append(s, start, pos - start);
    out += to;
    start = pos + from.size();
  }
  out.append(s, start, std::string::npos);
  return out;
}

inline std::vector<std::string> lines(std::string const &s) {
  return split(s, '\n');
}

inline std::string repeat(std::string const &s, size_t n) {
  std::string out;
  out.reserve(s.size() * n);
  for (size_t i = 0; i < n; ++i)
    out += s;
  return out;
}

inline std::string pad_left(std::string s, size_t width, char c = ' ') {
  if (s.size() < width)
    s.insert(0, width - s.size(), c);
  return s;
}

inline std::string pad_right(std::string s, size_t width, char c = ' ') {
  if (s.size() < width)
    s.append(width - s.size(), c);
  return s;
}

namespace detail {
// Whitespace trim without copying (the parsers below need a view).
inline std::string_view trim_view(std::string_view s) {
  constexpr std::string_view ws = " \t\n\r\f\v";
  const std::size_t first = s.find_first_not_of(ws);
  if (first == std::string_view::npos)
    return {};
  const std::size_t last = s.find_last_not_of(ws);
  return s.substr(first, last - first + 1);
}

// from_chars does not accept a leading '+' (stoi/stod do), so skip one.
inline std::string_view skip_plus(std::string_view s) {
  if (!s.empty() && s.front() == '+')
    s.remove_prefix(1);
  return s;
}
} // namespace detail

// The whole trimmed string must be a number: trailing garbage is an error,
// unlike std::stoi. Parsing uses from_chars, so invalid input costs a few
// nanoseconds instead of the ~1.1us an exception costs.
[[nodiscard]] inline Result<int> to_int(std::string_view s) {
  const std::string_view t = detail::skip_plus(detail::trim_view(s));
  if (t.empty())
    return err<int>("not a number");
  int value = 0;
  const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), value);
  if (ec == std::errc::result_out_of_range)
    return err<int>("out of range");
  if (ec != std::errc{} || ptr != t.data() + t.size())
    return err<int>("not a number");
  return ok(value);
}

[[nodiscard]] inline Result<double> to_double(std::string_view s) {
  const std::string_view t = detail::skip_plus(detail::trim_view(s));
  if (t.empty())
    return err<double>("not a number");
  double value = 0.0;
  const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), value);
  if (ec == std::errc::result_out_of_range)
    return err<double>("out of range");
  if (ec != std::errc{} || ptr != t.data() + t.size())
    return err<double>("not a number");
  return ok(value);
}

inline std::string reverse(std::string s) {
  std::reverse(s.begin(), s.end());
  return s;
}

inline std::string truncate(std::string s, size_t max,
                            std::string tail = "...") {
  if (s.size() <= max)
    return s;
  if (max <= tail.size())
    return tail.substr(0, max);
  return s.substr(0, max - tail.size()) + tail;
}

inline std::string capitalize(std::string s) {
  if (!s.empty())
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

inline std::string title(std::string s) {
  bool word_start = true;
  for (char &c : s) {
    unsigned char u = static_cast<unsigned char>(c);
    if (std::isspace(u)) {
      word_start = true;
    } else if (word_start) {
      c = static_cast<char>(std::toupper(u));
      word_start = false;
    }
  }
  return s;
}

namespace detail {
// Appends one element without the iostream machinery (an ostringstream costs
// ~25ns per element just to format an int). Strings append directly; numbers
// go through to_chars; streamable-only user types keep working via the
// ostringstream fallback.
template <class T> void append_element(std::string &out, T const &x) {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<U, std::string> ||
                std::is_same_v<U, std::string_view> ||
                std::is_same_v<U, char> || std::is_same_v<U, char const *> ||
                (std::is_array_v<U> &&
                 std::is_same_v<std::remove_extent_t<U>, char>)) {
    out += x;
  } else if constexpr (std::is_arithmetic_v<U>) {
    char buf[64];
    if constexpr (std::is_floating_point_v<U>) {
      // `general` with precision 6 matches ostream's default formatting.
      const auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), x,
                                         std::chars_format::general, 6);
      if (ec == std::errc{})
        out.append(buf, p);
    } else {
      const auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), x);
      if (ec == std::errc{})
        out.append(buf, p);
    }
  } else if constexpr (requires(std::ostream &os) { os << x; }) {
    std::ostringstream os;
    os << x;
    out += os.str();
  } else {
    out += std::to_string(x);
  }
}
} // namespace detail

template <std::ranges::range R>
std::string join(R const &parts, std::string const &sep) {
  std::string out;
  bool first = true;
  for (auto const &p : parts) {
    if (!first)
      out += sep;
    detail::append_element(out, p);
    first = false;
  }
  return out;
}

inline std::vector<std::string> chunk(std::string const &s, size_t n) {
  std::vector<std::string> out;
  if (n == 0)
    return out;
  out.reserve(s.size() / n + 1);
  for (size_t i = 0; i < s.size(); i += n)
    out.emplace_back(s.substr(i, n));
  return out;
}

inline bool starts_with(std::string const &s, std::string const &prefix) {
  return s.rfind(prefix, 0) == 0;
}

inline bool ends_with(std::string const &s, std::string const &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Precision-controlled formatting: to_string(v, 17) round-trips a double.
// `precision` is significant digits, matching ostream's setprecision.
inline std::string to_string(double value, int precision = 17) {
  char buf[64];
  const int p = precision < 1 ? 1 : precision;
  const auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), value,
                                       std::chars_format::general, p);
  if (ec != std::errc{})
    return {};
  return std::string(buf, end);
}

inline std::string to_string(int value) { return std::to_string(value); }

// Splits on any character in `delims`; runs of delimiters collapse and empty
// fields are dropped.
inline std::vector<std::string> split_any(std::string const &s,
                                          std::string const &delims) {
  std::vector<std::string> out;
  out.reserve(s.size() / 8 + 1);
  std::string current;
  for (char c : s) {
    if (delims.find(c) != std::string::npos) {
      if (!current.empty())
        out.push_back(std::move(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty())
    out.push_back(std::move(current));
  return out;
}

namespace detail {

template <class T> Result<T> parse_number(std::string const &s) {
  if constexpr (std::is_integral_v<T>)
    return fp::map(to_int(s), [](int v) { return static_cast<T>(v); });
  else
    return fp::map(to_double(s), [](double v) { return static_cast<T>(v); });
}

} // namespace detail

// Parses whitespace/comma separated numbers; reports the offending token.
template <class T> Result<std::vector<T>> parse_numbers(std::string const &s) {
  std::vector<T> out;
  for (auto const &token : split_any(s, " \t\n\r,")) {
    auto parsed = detail::parse_number<T>(token);
    if (!parsed.is_ok())
      return err<std::vector<T>>("not a number: '" + token + "'");
    out.push_back(parsed.value());
  }
  return ok(std::move(out));
}
} // namespace fp::str

#pragma once
#include "fp/stream.hpp"
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

namespace fp {

// Read one line from `in` (default stdin). Blocks until a newline or EOF.
// Errors: "end of input" at EOF, "input error" on stream failure.
inline Result<std::string> read_line(std::istream &in = std::cin) {
  std::string line;
  if (std::getline(in, line))
    return ok(std::move(line));
  if (in.eof())
    return err<std::string>("end of input");
  return err<std::string>("input error");
}

// Read all of `in` into one string.
inline Result<std::string> read_all(std::istream &in = std::cin) {
  std::string out((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  if (in.bad())
    return err<std::string>("input error");
  return ok(std::move(out));
}

// Lazily stream lines from `in` until EOF. `in` must outlive the returned
// Stream (stdin does — it's a global).
inline Stream<std::string> read_lines(std::istream &in = std::cin) {
  return Stream<std::string>([&in]() -> std::optional<std::string> {
    std::string line;
    if (std::getline(in, line))
      return line;
    return std::nullopt;
  });
}

// Read one character from `in`. Note: a terminal line-buffers by default, so
// this blocks until a newline unless the terminal is in raw mode (see
// `raw_mode`/`read_key` below).
inline Result<char> read_char(std::istream &in = std::cin) {
  auto c = in.get();
  if (c == std::char_traits<char>::eof())
    return in.eof() ? err<char>("end of input") : err<char>("input error");
  return ok(static_cast<char>(c));
}

// Lazily stream characters from `in` until EOF.
inline Stream<char> read_chars(std::istream &in = std::cin) {
  return Stream<char>([&in]() -> std::optional<char> {
    auto c = in.get();
    if (c == std::char_traits<char>::eof())
      return std::nullopt;
    return static_cast<char>(c);
  });
}

// Producer: read lines from `in` and send them into a channel. Blocks until
// EOF or until the channel is closed (which makes `send` throw).
inline void feed_lines(Channel<std::string> &ch, std::istream &in = std::cin) {
  std::string line;
  while (std::getline(in, line))
    ch.send(std::move(line));
}

#if defined(__unix__) || defined(__APPLE__)

// Toggle terminal raw mode (no echo, no line buffering) for single-keypress
// reading. Pair the calls: raw_mode(true) ... raw_mode(false). The original
// terminal state is saved on first enable and restored on disable.
inline Result<void> raw_mode(bool on) {
  static termios original;
  static bool saved = false;
  if (on) {
    if (tcgetattr(STDIN_FILENO, &original) != 0)
      return err<void>("cannot get terminal attributes");
    saved = true;
    termios raw = original;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
      return err<void>("cannot set raw mode");
  } else if (saved) {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &original) != 0)
      return err<void>("cannot restore terminal");
  }
  return ok<void>();
}

// Read a single keypress in raw mode (POSIX). Returns immediately — no newline
// and no echo. Call raw_mode(true) first.
inline Result<char> read_key() {
  char c = 0;
  ssize_t n = ::read(STDIN_FILENO, &c, 1);
  if (n <= 0)
    return err<char>("end of input");
  return ok(c);
}
#endif

} // namespace fp

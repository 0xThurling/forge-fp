# Input — `input.hpp`

Reading from an input stream — lines, whole input, or single characters — as
`Result`, `Stream`, or a `Channel` producer. The default source is `std::cin`,
but every function takes an `std::istream&` so you can read from a file, a
`std::istringstream`, or any other stream.

```cpp
#include <fp/input.hpp>
```

**Why this exists:** stream extraction returns the stream, and failure hides in
flags (`eof()`, `fail()`) that are easy to ignore. Input here returns `Result`
(the value or an error), `Stream` (lazy transformable), or pushes into a
`Channel` (decoupled producer) — the same three shapes the rest of the library
uses, so input composes with the error channel instead of interrupting it.

## The functions

| Function | Result |
|---|---|
| `read_line(in = std::cin)` | `Result<std::string>` — one line (blocks) |
| `read_all(in = std::cin)` | `Result<std::string>` — everything |
| `read_lines(in = std::cin)` | `Stream<std::string>` — lazy line stream |
| `read_char(in = std::cin)` | `Result<char>` — one character |
| `read_chars(in = std::cin)` | `Stream<char>` — lazy character stream |
| `feed_lines(ch, in = std::cin)` | push lines into a `Channel<std::string>` |
| `raw_mode(on)` *(POSIX)* | `Result<void>` — toggle raw terminal mode |
| `read_key()` *(POSIX)* | `Result<char>` — a single keypress, no Enter |

## Reading lines

```cpp
#include <fp/input.hpp>
#include <iostream>

int main() {
    std::cout << "name: ";
    auto name = fp::read_line();          // Result<std::string>
    if (!name.is_ok())
        return 1;                          // "end of input" at EOF
    std::cout << "hello, " << name.value() << "\n";
}
```

`read_line` returns a `Result` so EOF / stream failure is a value you handle,
not an unchecked stream flag. `read_all` reads to EOF in one call:

```cpp
auto source = fp::read_all();            // Result<std::string>
if (source.is_ok())
    std::cout << "read " << source.value().size() << " bytes\n";
```

## Streaming lines lazily

`read_lines` returns a `Stream<std::string>`, so you can `map`/`filter`/
`subscribe` as the lines arrive — no need to read the whole input first:

```cpp
// sum every line that parses as a number
long long total = 0;
fp::read_lines().subscribe([&](std::string const &line) {
    if (auto r = fp::str::to_int(line); r.is_ok())
        total += r.value();
});
```

The stream is **pull-based**: each line is read when the subscriber asks for
it. That makes it suitable for pipes and large files:

```cpp
fp::read_lines(in)
    .filter([](std::string const &line) { return !line.empty(); })
    .take(10)
    .subscribe([](std::string const &line) { std::cout << line << "\n"; });
```

## Single characters

`read_char` reads one character; `read_chars` streams them:

```cpp
auto c = fp::read_char();                        // Result<char>

// count vowels in a stream
int vowels = 0;
fp::read_chars().subscribe([&](char c) {
    if (std::string("aeiou").find(c) != std::string::npos)
        ++vowels;
});
```

## Producer/consumer with `Channel`

`feed_lines` is the producer side: it reads lines and pushes them into a
`Channel<std::string>`. Run it in a thread while a consumer drains the channel:

```cpp
fp::Channel<std::string> ch(64);                     // bounded -> backpressure
std::thread producer([&] { fp::feed_lines(ch); });   // stdin -> channel

for (;;) {
    auto line = ch.try_recv();
    if (!line)
        break;                                       // channel closed + drained
    process(*line);
}
producer.join();
```

The bounded channel is the point: a fast producer cannot outrun a slow consumer
without bound. For the lock-free single-producer/single-consumer variant, see
[`RingBuffer`](concurrency.md#ringbuffert--lock-free-spsc).

## Single keys (TUI / game style)

`read_char` reads one character, but a terminal *line-buffers* by default — you
still need Enter. For a true "press a key, no Enter, no echo", POSIX systems
provide `raw_mode` + `read_key`:

```cpp
#if defined(__unix__) || defined(__APPLE__)
#include <fp/scope.hpp>            // fp::defer
int main() {
    fp::raw_mode(true);
    auto restore = fp::defer([] { fp::raw_mode(false); });   // always restore

    while (true) {
        auto key = fp::read_key();
        if (!key.is_ok() || key.value() == 'q')
            break;
        std::cout << "pressed " << key.value() << "\n";
    }
}
#endif
```

`raw_mode(true)` disables echo and line buffering; `raw_mode(false)` restores
the terminal. Pairing it with [`fp::defer`](scope.md) guarantees the restore
even on an early exit or exception. These are POSIX-only (the
`<termios.h>`/`<unistd.h>` path is guarded), so a portable program should use
`read_char` and reserve `read_key` for POSIX targets.

## Gotchas

- **The stream must outlive the returned `Stream`.** `read_lines(in)`/`read_chars(in)`
  capture `&in`; with the default `std::cin` that's fine (it's a global), but
  don't pass a temporary stream.
- **`feed_lines` blocks** until EOF or the channel is closed (closing makes
  `send` throw). Run it in its own thread for a live producer.
- **Errors are strings**, consistent with the rest of the library: `"end of
  input"` at EOF, `"input error"` on stream failure.
- **A `Stream` is one-pass.** It pulls from the underlying stream; iterating it
  twice does not rewind. Collect with `fp::to_vector` if you need to re-read.
- **`raw_mode` is global terminal state.** Restore it on every path (use
  `fp::defer`), or a crash leaves the user's terminal in raw mode.
- **Don't mix line and char reads** on the same stream casually: `read_line`
  consumes the newline, `read_char` does not.

# Input — `input.hpp`

Reading from an input stream — lines or single characters — as `Result`,
`Stream`, or a `Channel` producer. The default source is `std::cin`, but every
function takes an `std::istream&` so you can read from a file, a
`std::istringstream`, or any other stream.

```cpp
#include <fp/input.hpp>
```

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
    if (!name.is_ok()) return 1;          // "end of input" at EOF
    std::cout << "hello, " << name.value() << "\n";
}
```

`read_line` returns a `Result` so EOF / stream failure is a value you handle,
not an unchecked stream flag.

## Streaming lines lazily

`read_lines` returns a `Stream<std::string>`, so you can `map`/`filter`/
`subscribe` as the lines arrive — no need to read the whole input first:

```cpp
auto total = 0;
fp::read_lines()
    .map([](std::string const& s) { return fp::str::to_int(s); })   // not optional — use a lambda
    .subscribe([](auto) { /* ... */ });
```

Better: parse and skip the bad lines with a plain subscribe loop, or compose
with `filter_map` after collecting. The point is the same `map`/`filter`
vocabulary you already know, applied to input.

```cpp
// sum every line that parses as a number
long long total = 0;
fp::read_lines().subscribe([&](std::string const& line) {
    if (auto r = fp::str::to_int(line); r.is_ok())
        total += r.value();
});
```

## Producer/consumer

`feed_lines` is the producer side: it reads lines and pushes them into a
`Channel<std::string>`. Run it in a thread while a consumer drains the channel:

```cpp
fp::Channel<std::string> ch;
std::thread producer([&] { fp::feed_lines(ch); });   // reads stdin -> channel
// consume on the main thread, or another worker
```

## Single keys (TUI / game style)

`read_char` reads one character, but a terminal *line-buffers* by default — you
still need Enter. For a true "press a key, no Enter, no echo", POSIX systems
provide `raw_mode` + `read_key`:

```cpp
#if defined(__unix__) || defined(__APPLE__)
int main() {
    fp::raw_mode(true);
    while (true) {
        auto key = fp::read_key();
        if (!key.is_ok() || key.value() == 'q') break;
        std::cout << "pressed " << key.value() << "\n";
    }
    fp::raw_mode(false);
}
#endif
```

`raw_mode(true)` disables echo and line buffering; `raw_mode(false)` restores
the terminal. These are POSIX-only (the `<termios.h>`/`<unistd.h>` path is
guarded), so a portable program should use `read_char` and reserve `read_key`
for POSIX targets.

## Notes

- **The stream must outlive the returned `Stream`.** `read_lines(in)`/`read_chars(in)`
  capture `&in`; with the default `std::cin` that's fine (it's a global), but
  don't pass a temporary stream.
- **`feed_lines` blocks** until EOF or the channel is closed (closing makes
  `send` throw). Run it in its own thread for a live producer.
- **Errors are strings**, consistent with the rest of the library: `"end of
  input"` at EOF, `"input error"` on stream failure.

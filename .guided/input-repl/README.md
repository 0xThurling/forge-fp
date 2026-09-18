# Project 8 — Input: Line Processor → REPL (CodeCrafters-style)

Build a tool that reads **input** and does something with it — first as a Unix
filter that processes stdin line by line, then as an interactive REPL that reads
single keys. Each stage adds a feature and ends with a **Verify** check.

**Modules:** `input.hpp`, `stream.hpp`, `concurrent.hpp`, `string.hpp`, `result.hpp`, `adt.hpp`.
**Compile:** `g++ -std=c++20 -pthread -I src -o app app.cpp`

To test non-interactively, pipe input in:

```bash
printf "1\n2\n3\n" | ./app
```

---

## Stage 1 — Read a line and echo it

Goal: read one line with `read_line` (a `Result`), and print it.

```cpp
#include <fp/input.hpp>
#include <iostream>

int main() {
    std::cout << "type something: ";
    auto line = fp::read_line();          // Result<std::string>
    if (!line.is_ok()) return 1;          // "end of input" at EOF
    std::cout << "you said: " << line.value() << "\n";
}
```

**Verify:** `echo "hi" | ./app` prints `you said: hi`.

**Concept — error-as-value, again.** EOF / stream failure is a `Result`, so you
handle "no more input" explicitly instead of checking a stream flag.

## Stage 2 — Read everything and count words

Goal: read all of stdin, then count lines and words.

```cpp
#include <fp/input.hpp>
#include <fp/string.hpp>
#include <iostream>

int main() {
    auto text = fp::read_all();           // Result<std::string>
    if (!text.is_ok()) return 1;

    auto lines = fp::str::lines(text.value());
    auto words = fp::str::split(text.value(), ' ');
    std::cout << lines.size() << " lines, " << words.size() << " words\n";
}
```

**Verify:** `printf "a b\nc d\n" | ./app` prints `2 lines, 4 words`.

**Concept — compose.** `read_all` + `str::lines`/`split` turns raw input into
collections you can `map`/`filter`/`size` like anything else.

## Stage 3 — Stream lines lazily and sum the numbers

Goal: don't read everything at once — `read_lines` is a lazy `Stream`.

```cpp
#include <fp/input.hpp>
#include <fp/string.hpp>
#include <iostream>

int main() {
    long long total = 0;
    fp::read_lines().subscribe([&](std::string const& line) {
        if (auto r = fp::str::to_int(line); r.is_ok())
            total += r.value();
    });
    std::cout << "sum = " << total << "\n";
}
```

**Verify:** `printf "1\n2\nfoo\n3\n" | ./app` prints `sum = 6`.

**Concept — a stream of lines.** `read_lines` returns a `Stream<std::string>`;
`subscribe` runs the pipeline, pulling lines until EOF. Nothing is materialized
up front.

## Stage 4 — `map` and `filter` the stream

Goal: transform the lines before summing — only keep the even numbers.

```cpp
#include <fp/all.hpp>

int main() {
    long long total = 0;
    // `|` maps over the stream: `to_int` is lifted into every line
    auto parsed = fp::out(fp::into(fp::read_lines()) | fp::str::to_int);  // Stream<Result<int>>
    parsed.subscribe([&](fp::Result<int> r) {
        if (r.is_ok() && r.value() % 2 == 0) total += r.value();
    });
    std::cout << "even sum = " << total << "\n";
}
```

**Verify:** `printf "1\n2\n3\n4\n" | ./app` prints `even sum = 6`.

**Concept — same vocabulary as collections, via `|`.** `into(stream) | f` maps
`f` over the items (the type-directed pipe — see
[composition](../../.docs/composition.md)), so the stream is transformed with
the same operator you'd use on a `Result` or `optional`.

**Concept — same vocabulary as collections.** `Stream::map`/`filter` mirror
`vec.hpp`'s — a sequence that arrives over time is transformed with the same
combinators.

## Stage 5 — Producer/consumer with a `Channel`

Goal: read lines and push them into a channel; consume from a worker.

```cpp
#include <fp/all.hpp>
#include <thread>

int main() {
    fp::Channel<std::string> ch;
    std::thread producer([&] { fp::feed_lines(ch); });   // stdin -> channel

    long long total = 0;
    for (auto line = ch.try_recv(); line; line = ch.try_recv()) {
        if (auto r = fp::str::to_int(*line); r.is_ok()) total += r.value();
    }
    producer.join();
    std::cout << "sum = " << total << "\n";
}
```

**Verify:** `printf "1\n2\n3\n" | ./app` prints `sum = 6`.

**Concept — separate the producer from the consumer.** `feed_lines` is the
producer side; the main thread drains the channel. The channel is the explicit
boundary between "produce input" and "consume input".

## Stage 6 — Character-level input

Goal: read input one character at a time.

```cpp
#include <fp/input.hpp>
#include <iostream>

int main() {
    auto c = fp::read_char();
    if (c.is_ok()) std::cout << "first char: " << c.value() << "\n";
}
```

**Verify:** `echo "hello" | ./app` prints `first char: h`.

**Concept — a char is just a stream element.** `read_char` is `read_line` for
one character; `read_chars` streams them.

## Stage 7 — Raw-mode key reading (POSIX)

Goal: a true "press a key, no Enter" reader — for TUI/game input.

```cpp
#if defined(__unix__) || defined(__APPLE__)
#include <fp/input.hpp>
#include <iostream>

int main() {
    fp::raw_mode(true);
    std::cout << "press keys (q to quit):\n";
    for (;;) {
        auto key = fp::read_key();
        if (!key.is_ok() || key.value() == 'q') break;
        std::cout << "pressed '" << key.value() << "'\n";
    }
    fp::raw_mode(false);
}
#endif
```

**Verify:** running it, pressing `a`, `b`, `q` echoes each key immediately (no
Enter), and `q` exits.

**Concept — the terminal is line-buffered.** `raw_mode` disables echo and
buffering so `read_key` returns per keypress. It's POSIX-only (guarded), so keep
it behind `#if defined(__unix__)`.

## Stage 8 — An interactive REPL

Goal: tie it together — read a command, dispatch on it, respond.

```cpp
#include <fp/all.hpp>
#include <iostream>

int main() {
    int value = 0;
    std::cout << "commands: +N  -N  show  quit\n";
    for (;;) {
        std::cout << "> ";
        auto line = fp::read_line();
        if (!line.is_ok()) break;

        auto cmd = line.value();
        if (cmd == "quit") break;
        else if (cmd == "show") std::cout << "value = " << value << "\n";
        else if (fp::str::starts_with(cmd, "+"))
            value += std::stoi(cmd.substr(1));
        else if (fp::str::starts_with(cmd, "-"))
            value -= std::stoi(cmd.substr(1));
        else std::cout << "?\n";
    }
}
```

**Verify:** feed it `+5`, `+3`, `show`, `quit` → `value = 8`.

**Concept — the REPL loop is a fold over input.** Read → interpret → update
state → repeat. The state is a value; each command is a pure-ish transition.

---

## 🏆 Extensions

1. **A word-frequency histogram** — `read_lines` + `split` + `group_by` to count
   word occurrences, print sorted by count.
2. **A calculator REPL** — parse each line as an expression (`+ 1 2`, `* 3 4`)
   and evaluate with `match` on the operator.
3. **A number-guessing game** — raw-mode keys to "guess higher/lower", with the
   answer chosen once and state threaded through the loop.
4. **A full pipeline as a value** — compose the filter/sum stages into a
   `fp::pipe` and apply it to `read_lines`.
5. **Two streams merged** — read lines and chars, combine them (e.g. count lines
   *and* characters).

The goal: feel how input is just another source of values — lines, chars, or
keys — that flows through the same `Result`/`Stream`/`Channel` combinators as
everything else in the library.

# File I/O — `io.hpp`

File access with a `Result` entry point, so failures (missing file,
permissions, disk full) flow through the library's error channel instead of
throwing.

```cpp
#include <fp/io.hpp>
```

| Function | Result |
|---|---|
| `read_file(path)` | `Result<std::string>` — whole file, binary mode |
| `read_lines(path)` | `Result<std::vector<std::string>>` — text lines |
| `write_file(path, content)` | `Result<void>` |
| `write_lines(path, lines)` | `Result<void>` — one `\n` after each line |
| `read_bytes(path)` | `Result<std::vector<std::byte>>` |
| `write_bytes(path, span)` | `Result<void>` |
| `exists(path)` | `bool` — can it be opened for reading? |
| `ensure_directory(path)` | `Result<void>` — like `mkdir -p`, idempotent |

```cpp
#include <fp/all.hpp>
#include <iostream>

int main() {
    auto text = fp::read_file("input.txt");
    if (!text.is_ok()) {
        std::cerr << text.error() << "\n";   // "cannot open input.txt"
        return 1;
    }

    auto saved = fp::write_file("output.txt", text.value());
    return saved.is_ok() ? 0 : 1;
}
```

**Why `Result`-returning I/O:** the stdlib's `fstream` reports failure through
its `fail()`/`bad()` flags, which are easy to forget to check. Wrapping the
open/read/write in a `Result` makes "did the file work?" part of the return
type — you can't ignore it, and the failure composes with the rest of the
library. The design rule is "IO errors are `Result` strings, never exceptions"
(the same error model as everything else).

## Reading

```cpp
// Whole file as one string.
auto config = fp::read_file("app.conf");
std::string text = config.value();

// Line-oriented.
auto lines = fp::read_lines("data.txt");
for (auto const &line : lines.value())
  process(line);

// Binary.
auto raw = fp::read_bytes("model.bin");
auto params = fp::from_bytes<float>(raw.value());   // <fp/serialize.hpp>
```

`read_file` reads in binary mode and preserves every byte (including `\r`);
`read_lines` uses `std::getline`, so it splits on `\n` and drops the newline.
Pick by whether line structure matters.

`read_file` and `read_bytes` size the file and read it in one call when the
input is seekable; pipes and `/proc` entries fall back to streaming through the
buffer. That matters: reading 8 MB through `istreambuf_iterator` costs ~30ms
against ~1.3ms for the bulk path (**~23x**). `read_lines` is at parity with a
hand-written `getline` loop. Every path parameter in this module is a
`std::string_view`, so literals and substrings need no temporary.

## Writing

```cpp
fp::write_file("out.txt", "hello\n");

fp::write_lines("notes.txt", {"epoch 10", "loss 0.42"});
// file contents: "epoch 10\nloss 0.42\n"
```

`write_file` takes a `std::string_view`, so a `std::string`, a literal, or a
substring works without a copy.

## Bytes and checkpoints

Text is the default; binary is for checkpoints and caches:

```cpp
std::vector<float> params = /* ... */;
fp::write_bytes("params.bin", fp::to_bytes(params));

auto raw = fp::read_bytes("params.bin");
auto loaded = fp::from_bytes<float>(raw.value());
if (!loaded.is_ok())
  return fp::fail("checkpoint is truncated: " + loaded.error());
```

`read_bytes`/`write_bytes` do no interpretation — they are the raw layer that
`serialize.hpp` sits on. `from_bytes` rejects sizes that are not a multiple of
the element size, which catches the common truncation bug.

## Directories and pre-checks

```cpp
fp::ensure_directory("checkpoints/run-01");     // idempotent, creates parents

if (!fp::exists("server.conf"))
  return fp::fail("missing config");
auto config = fp::read_file("server.conf");
```

`ensure_directory` uses `std::filesystem::create_directories`, so every missing
parent is created and an existing directory is a success. `exists` answers "can
this be opened for reading?" — a cheap pre-check that keeps the common case out
of the error path (note it is a snapshot: the file can disappear between the
check and the read, so the read still returns `Result`).

## Composing with the ADT machinery

Because the results are `Result`, they compose with everything in
[ADTs](adts.md):

```cpp
// read -> parse -> transform, in one pipeline
auto total = fp::read_lines("numbers.txt")
    >>= [](std::vector<std::string> const &lines) {
            return fp::traverse(lines, fp::str::to_int);   // Result<vector<int>>
        }
    >>= [](std::vector<int> const &ns) {
            return fp::ok(fp::fold_left(ns, 0, fp::plus));
        };

if (total.is_ok())
    std::cout << "sum = " << total.value() << "\n";
```

`read_lines(path) | and_then(parse_csv)` is the whole "load a file and process
it" pipeline in one expression. A CSV-ish loader reads naturally:

```cpp
auto dataset = fp::read_lines("data.csv")
    >>= [](std::vector<std::string> const &lines) {
          return fp::traverse(lines, [](std::string const &line) {
            return fp::str::parse_numbers<double>(line);   // one row per line
          });
        };
// Result<vector<vector<double>>>
```

## Error messages

Errors are `std::string` messages, consistent with `Result`:

| Failure | Message |
|---|---|
| open for read/write fails | `"cannot open <path>"` |
| read fails mid-stream | `"read failed: <path>"` |
| write fails mid-stream | `"write failed <path>"` |
| directory creation fails | `"cannot create directory <path>: <system message>"` |

If callers need to branch on *what* failed, use `fp::Outcome<T>` and attach
codes (`fp::errc::not_found`, `fp::errc::invalid`) at your boundary — see
[Structured errors](adts.md#outcomet--structured-errors-with-codes-and-context).

## Gotchas

- **`read_file` is binary; `read_lines` is text.** A file with `\r\n` keeps the
  `\r` in `read_lines` output (trim it with `fp::str::trim` if needed).
- **No automatic buffering layer.** Each call opens and closes the file; for
  many small reads, read once and split in memory.
- **`exists` is a snapshot**, not a lock — always handle the read failure too.
- **Binary is host-order.** `read_bytes`/`write_bytes` round-trip on one
  machine; cross-platform files should be text or byte-normalized (see
  [serialization](serialization.md)).
- **Nothing creates parent directories implicitly** for `write_file`; call
  `ensure_directory` first if the path is nested.
- **`write_lines` appends a trailing newline** to every line, including the
  last; `read_lines` drops it, so the round-trip is stable.

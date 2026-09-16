# File I/O — `io.hpp`

Three functions that give file access a `Result` entry point, so failures
(missing file, permissions, disk full) flow through the library's error channel
instead of throwing.

```cpp
#include <fp/io.hpp>
```

| Function | Result |
|---|---|
| `read_file(path)` | `Result<std::string>` |
| `read_lines(path)` | `Result<std::vector<std::string>>` |
| `write_file(path, content)` | `Result<void>` |

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

## Composing with the ADT machinery

Because the results are `Result`, they compose with everything in
[ADTs](adts.md):

```cpp
// read -> parse -> transform, in one pipeline
auto total = fp::read_lines("numbers.txt")
    >>= [](std::vector<std::string> const& lines) {
            return fp::traverse(lines, fp::str::to_int);   // Result<vector<int>>
        }
    >>= [](std::vector<int> const& ns) {
            return fp::ok(fp::fold_left(ns, 0, fp::plus));
        };

if (total.is_ok())
    std::cout << "sum = " << total.value() << "\n";
```

## Notes

- `read_file`/`write_file` open in binary mode; `read_lines` reads text lines
  (no trailing newline handling beyond `std::getline`).
- `write_file` takes a `std::string_view` content and returns `Result<void>`
  (see `Either<E, void>` in [ADTs](adts.md)).
- Errors are `std::string` messages (`"cannot open <path>"`, `"read failed:
  <path>"`, `"write failed <path>"`) — never exceptions.

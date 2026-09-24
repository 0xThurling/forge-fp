# Serialization — `serialize.hpp`

Generic round-trip for ranges of numbers: text when you want to read it, bytes
when you want it compact.

```cpp
#include <fp/serialize.hpp>
```

**Why this exists:** `utils/serialization` in most projects is a pile of
per-type loops (`for (double x : w) out << x << ' ';`). The encoding of *a
range of numbers* is generic — only the schema (which vector is which
parameter) is domain-specific. `serialize.hpp` owns the encoding; your
checkpoint format owns the schema.

## The API

```cpp
template <std::ranges::range R>
std::string to_text(R const &r, int precision = 17);

template <class T>
fp::Result<std::vector<T>> from_text(std::string_view text);

template <std::ranges::contiguous_range R>
std::vector<std::byte> to_bytes(R const &r);

template <class T>
fp::Result<std::vector<T>> from_bytes(std::span<std::byte const> data);
```

| Function | Format | Use for |
|---|---|---|
| `to_text` | space-separated decimal | config files, logs, debugging, model cards |
| `from_text` | parses whitespace-separated numbers | reading those files back |
| `to_bytes` | raw little-endian bytes (host order) | checkpoints, caches, IPC |
| `from_bytes` | exact inverse | loading them back |

Text formatting uses `std::to_chars`/`from_chars`: `to_text` is ~4x faster and
`from_text` ~9x faster than the previous stream-based versions, and neither
depends on the locale or copies the input.

## Worked examples

### 1. Text round-trip

```cpp
std::vector<double> w = {0.1 + 0.2, -1.5, 3.0};

const std::string text = fp::to_text(w, 17);   // "0.30000000000000004 -1.5 3"
auto back = fp::from_text<double>(text);
// back.value() == w, bit for bit
```

### 2. A parameter block

```cpp
std::string block;
block += "w " + fp::to_text(model.weights(), 17) + "\n";
block += "b " + fp::to_text(model.biases(), 17) + "\n";
fp::write_file("model.txt", block);
```

Loading is `fp::str::split` per line plus `from_text` per field; the schema
(which line is `w`) stays in your code.

### 3. Compact binary checkpoint

```cpp
std::vector<float> params = /* ... */;

auto bytes = fp::to_bytes(params);
fp::write_bytes("params.bin", bytes);

auto raw = fp::read_bytes("params.bin");
auto loaded = fp::from_bytes<float>(raw.value());
// loaded.value() == params
```

### 4. Detecting corruption

```cpp
auto bytes = fp::read_bytes(path).value();
auto params = fp::from_bytes<float>(bytes);
if (!params.is_ok())
  return fp::fail("checkpoint is truncated: " + params.error());
```

`from_bytes` rejects any size that is not a multiple of `sizeof(T)`.

## Theory: text vs binary, and precision

- **Text is inspectable and diffable.** It costs space and parse time, and it
  is subject to the stream's locale (`std::ostringstream` uses the global
  locale; use the default "C" locale for data files).
- **Binary is exact and compact.** It is `sizeof(T)` per element with no
  parsing, but it is opaque and depends on the host's byte order.
- **Precision 17** is `std::numeric_limits<double>::max_digits10`: the smallest
  number of decimal digits that round-trips every `double` exactly. Fewer
  digits are fine for display, wrong for storage.
- **`float`** needs only 9 digits (`max_digits10`), but `to_text` defaults to
  17 for `double`; pass the precision explicitly when you know the type.

## Gotchas

- **Bytes are host-order.** `to_bytes`/`from_bytes` write the in-memory
  representation, so a file written on a big-endian machine will not load on a
  little-endian one. For portable formats, write text or normalize the byte
  order yourself.
- **`to_bytes` requires trivially copyable elements.** A `std::vector<std::string>`
  does not qualify (it would serialize pointers, not text).
- **No schema, no versioning.** `serialize.hpp` does not know that the first
  vector is `w`; keep a version field and the parameter names in your
  checkpoint format, and validate shapes on load.
- **`from_text` reports the first bad token generically** (`"invalid number"`).
  If you need the offending token, split with `fp::str::parse_numbers`, which
  reports it.
- **Text round-trips `nan`/`inf`** as `nan`/`inf` only if the stream parses
  them back (`istringstream` does); don't rely on it for portable files.

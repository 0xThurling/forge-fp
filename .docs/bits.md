# Bits — `bits.hpp`

`bits.hpp` works directly on the bits of a value: read and write single bits,
cut out and splice in bit ranges, count and rotate, view a bit range of any
object as a first-class *section*, pack and unpack bit streams in either bit
order, and render values as binary or hex. Everything is `constexpr` where it
can be, and the whole-value operations are defined on the *object
representation*, so they work on integers, floats and plain structs alike —
not just `unsigned`.

```cpp
#include <fp/bits.hpp>
```

**Why this exists:** flags, packed wire formats, hardware registers, bitmap
allocators and quantized weights all live below the type system. The usual
answer is hand-rolled shifts, magic masks and `reinterpret_cast`. Here the same
operations have names, ranges are inclusive and explicit, and the same helpers
apply to any trivially copyable type.

**The one guarantee to remember:** nothing here is undefined behavior, whatever
the inputs. Out-of-range arguments are `assert`ed, so debug builds catch the
bug; release builds stay *defined* — reads give 0, writes leave the value
unchanged, a `bit_span` is clamped to its storage, and stream reads return an
error.

## Values

### Single bits

Bit 0 is the least significant bit. The functions take any integral type except
`bool`, and return the same type.

```cpp
std::uint8_t flags = 0b0000'1000;
fp::bit(flags, 3);            // true
fp::set_bit(flags, 0);        // 0b0000'1001
fp::clear_bit(flags, 3);      // 0b0000'0001
fp::toggle_bit(flags, 1);     // 0b0000'0011
fp::set_bit(flags, 7, false); // explicit clear
```

### Bit ranges

Ranges are inclusive: `[lo, hi]`.

```cpp
std::uint16_t rgb = 0;
rgb = fp::insert_bits(rgb, 11, 15, std::uint16_t{0x1F}); // red   = 31
rgb = fp::insert_bits(rgb,  5, 10, std::uint16_t{0x2A}); // green = 42
rgb = fp::insert_bits(rgb,  0,  4, std::uint16_t{0x15}); // blue  = 21

fp::extract_bits(rgb, 5, 10);       // 42 — the green channel
fp::bit_mask<std::uint16_t>(5, 10); // 0b0111'1110'0000
fp::low_mask<std::uint8_t>(3);      // 0b111
```

| Function | Result |
|---|---|
| `low_mask<T>(n)` | bits `[0, n)` set; `n >= digits` gives all ones |
| `bit_mask<T>(lo, hi)` | bits `[lo, hi]` set |
| `extract_bits(x, lo, hi)` | the value of `[lo, hi]`, shifted down to bit 0 |
| `insert_bits(x, lo, hi, value)` | `x` with `[lo, hi]` replaced; bits of `value` above the range are dropped |

`insert_bits` widens the value to `T` *before* shifting, so mixed widths are
safe: `insert_bits(uint64, 60, 63, uint8_t{0xF})` works.

### Counting and rearranging

```cpp
fp::popcount(std::uint32_t{0xF0F0});      // 8
fp::count_zeros(std::uint8_t{0x0F});      // 4
fp::leading_zeros(std::uint8_t{1});       // 7
fp::trailing_zeros(std::uint8_t{8});      // 3
fp::bit_width(std::uint32_t{255});        // 8 (bits needed to represent x)
fp::has_single_bit(std::uint32_t{1024});  // true

fp::rotl(std::uint8_t{0x81}, 1);          // 0x03
fp::rotr(std::uint8_t{0x81}, -1);         // 0x03 — negative rotates the other way
fp::byteswap(std::uint32_t{0x11223344});  // 0x44332211
fp::bit_reverse(std::uint32_t{0x12345678}); // 0x1E6A2C48 — bits, not bytes
fp::bit_reverse(std::uint8_t{0b1011}, 4); // 0b1101 — low 4 bits only
fp::sign_bit(-0.0);                       // true
```

`rotl`/`rotr` accept any rotation count (including negative and larger than the
type). `byteswap` reverses *byte order*; `bit_reverse` reverses *bit order* —
the two are different and often confused. `bit_reverse(x, width)` reverses only
the low `width` bits (the rest become zero), which is what you want for a field
read from an MSB-first source.

### Byte order

Byte reversal is its own inverse, so `to_*` and `from_*` are the same
operation; the names document intent at the call site.

```cpp
std::uint32_t wire = fp::to_big_endian(0x11223344u); // network order
std::uint32_t host = fp::from_big_endian(wire);
fp::to_little_endian(1.5f);                          // any trivially copyable value
```

### Whole-value bitwise operators

The usual operators, lifted from integers to any trivially copyable value of
the same size:

```cpp
struct Mask { std::uint16_t a, b; };
Mask m = fp::bit_and(Mask{0x00FF, 0xFF00}, Mask{0x0F0F, 0x0F0F});
// m == {0x000F, 0x0F00}

fp::bit_not(std::uint8_t{0x0F});  // 0xF0
fp::bit_or(a, b);                 // byte-wise on the representation
fp::bit_xor(a, b);
```

Sizes with a matching unsigned integer type (1/2/4/8 bytes) take a
single-instruction path; other sizes fall back to a byte loop.

`bool` is rejected at compile time: its object representation is not guaranteed
to stay 0/1 under bitwise ops (`bit_not(true)` would produce an invalid `bool`).
Structs *containing* `bool` have the same caveat — these ops are for numeric
representations and bitmasks, not booleans.

## Bytes

```cpp
float f = 1.0f;

auto bytes = fp::bytes_of(f);          // std::array<std::byte, 4>, a copy
fp::as_bytes(f)[3] |= std::byte{0x80}; // mutable view: flip the sign bit
// f == -1.0f

std::uint64_t wide = 0x1122334455667788;
fp::word_at<std::uint32_t>(wide, 4);   // 4 bytes at offset 4 (native order)
fp::word_at<std::uint32_t>(wide, 99);  // 0 — past the end reads as zero
```

| Function | Result |
|---|---|
| `bytes_of(x)` | `std::array<std::byte, sizeof(T)>` — a constexpr copy |
| `as_bytes(x)` | `std::span<std::byte const>`; the non-const overload is writable |
| `word_at<Word>(x, offset = 0)` | `sizeof(Word)` bytes read as an integer (native order; past the end reads zero) |

`as_bytes` is a *view*: writing through it edits the object in place. Use
`bytes_of` when you need a `constexpr` copy.

## Sections — `bit_span`

A section is a non-owning view of a range of bits: of an object's
representation, or of raw bytes. `bit_span` is the mutable view,
`const_bit_span` the read-only one; a mutable view converts to a const one,
never the other way round.

```cpp
std::uint32_t x = 0xDEADBEEF;

fp::bit_span whole(x);          // all 32 bits
fp::bit_span low(x, 0, 15);     // bits [0, 15]
low.read();                     // 0xBEEF
low.write(0x1234);              // x == 0xDEAD1234
```

Sections can also be built over raw byte ranges — any span or container of
1-byte elements works, so `std::byte`, `unsigned char` and `char` buffers are
all accepted:

```cpp
std::vector<unsigned char> buffer = ...;
fp::bit_span b{std::span<unsigned char>(buffer)}; // 8 bits per byte
fp::bit_span bytes{std::span(buffer), 0, 63};     // first 64 bits
fp::const_bit_span c{buffer};                     // read-only view
fp::bit_span empty;                               // default: empty view
```

Views are cheap to copy, and every section reports its shape:

```cpp
s.size();        // bits
s.size_bytes();  // bytes needed to cover those bits
s.offset();      // bit offset into the storage
s.storage();     // the whole backing byte range (not just this section)
s.empty();
```

### Reading and writing

`read`/`write` move at most 64 bits per call; `read_at`/`write_at` do the same
at an offset inside the section. Both are defined for any input (they clamp).

```cpp
s.read();                     // uint64_t
s.write(0xAB);                // low bits of the value
s.read_at(8, 16);             // 16 bits starting at bit 8
s.write_at(8, 0xBEEF, 16);
```

### Indexing and iteration

```cpp
s[3] = true;                  // assignable proxy (mutable spans)
bool b = s[3];                // const spans return bool directly
s.set_bit(3, false);
s.toggle_bit(0);

for (bool b : s) { /* ... */ }  // LSB first
for (auto b : s) b = true;      // write through the proxy
```

`bit_span` models `std::ranges::input_range` and `sized_range`, so range
algorithms work too:

```cpp
std::ranges::count_if(s, [](bool b) { return b; });
std::ranges::distance(s);
```

### Slicing, copying and comparing

```cpp
auto nib = whole.section(4, 7);   // a smaller view over the same bits
auto [lo, hi] = whole.split(16);  // cut in two at bit 16
hi.copy_from(low);                // copy a same-width section
low == hi;                        // bit-wise equality (sizes must match)
low.equal(hi);                    // same thing, named
```

`copy_from` has **memmove semantics**: overlapping sections are handled, so
in-place shifts work:

```cpp
s.section(0, n - 2).copy_from(s.section(1, n - 1)); // shift the low n bits left
```

### Bulk operations

```cpp
s.fill(true);        // set every bit in the section
s.flip();            // invert every bit
s.popcount();        // number of set bits
s.any(); s.all(); s.none();
```

### Search

Bitmap scanning is word-at-a-time (about 20x a byte loop):

```cpp
s.find_first_set();        // optional<size_t>, nullopt if none
s.find_first_clear();
s.find_next_set(from);     // first set bit at or after `from`
s.find_next_clear(from);
s.find_last_set();         // highest set bit

while (auto i = ready.find_next_set(from)) {
  // ... use *i ...
  from = *i + 1;
}
```

Indices are relative to the section, and `all()` on an empty section is
vacuously true (like `std::bitset`).

### Compile-time fields

For fixed layouts the range becomes part of the type, so the masks are
constants and the codegen matches hand-written shifts:

```cpp
std::uint16_t rgb = 0;
fp::bit_field<11, 15>(rgb) = 31;       // read and write like a value
fp::bit_field<5, 10>(rgb) = 42;
unsigned red = fp::bit_field<11, 15>(rgb);

using green = decltype(fp::bit_field<5, 10>(rgb));
green::lo; green::hi; green::width;    // 5, 10, 6
```

`bit_split<Widths...>(x)` cuts a value into consecutive fields (from bit 0 up)
for destructuring:

```cpp
auto [blue, green, red] = fp::bit_split<5, 6, 5>(rgb);
blue = 21; green = 42; red = 31;
```

Zero widths and widths that do not fit the value are rejected at compile time.
`bit_field` on a `const` object still reads (it just cannot be assigned to).

## Packed bit streams

`BitWriter`/`BitReader` pack values at arbitrary bit offsets into a byte
buffer; `MsbWriter`/`MsbReader` are the MSB-first variants.

### Bit order

The order is part of the type, so it costs nothing:

```cpp
fp::BitWriter writer;      // LSB-first: the low bit of a field goes out first
writer.write(0b101, 3);    // zlib/DEFLATE-style values
writer.write(0xABCD, 16);

fp::MsbWriter msb;         // MSB-first: the high bit of a field goes out first
msb.write(0b101, 3);       // H.264/JPEG/FLAC-style codes
```

Some formats mix both in one stream (DEFLATE writes header fields LSB-first but
Huffman codes MSB-first). The stream's type fixes how *bytes* fill, and a
per-call template argument overrides the *field* order:

```cpp
fp::BitWriter w;
w.write(0b101, 3);                             // LSB-first value
w.write<fp::BitOrder::MsbFirst>(0xABCD, 16);   // MSB-first code, same byte stream
w.write(0b11, 2);

fp::BitReader r{w.data()};
r.read(3);
r.read<fp::BitOrder::MsbFirst>(16);
r.read(2);
```

### Alignment

```cpp
fp::BitWriter w;
w.write(0b101, 3);
w.align_to_byte();        // pad with zero bits to the next byte
w.align_to_byte(true);    // ... or with ones

fp::BitReader r{w.data()};
r.read(3);
r.align_to_byte();        // skip to the next byte boundary, or error
```

### Streams and sections

The stream and section APIs hand off to each other:

```cpp
std::uint16_t src = 0xBEEF;
fp::BitWriter w;
w.write(fp::bit_span(src));          // append every bit of a section
w.data();                            // std::span<std::byte const>
auto owned = w.release();            // move the bytes out (writer is emptied)

std::uint16_t dst = 0;
fp::BitReader r{w.data()};
r.read_into(fp::bit_span(dst));      // fill a section, advance

fp::BitReader r2{w.data()};
auto window = r2.peek_bits(8);       // a view of the next 8 bits, no consume
window.value().read();
r2.skip(8);                          // ... then consume them
```

### Reading

```cpp
fp::BitReader r{w.data()};
r.read(3);          // Result<uint64_t>
r.read_bit();       // Result<bool>
r.peek(16);         // like read, without consuming
r.skip(5);          // Result<void>
r.remaining();      // bits left
r.at_end();
r.position();       // bits consumed from the start of the view
r.reset();
```

Reads past the end return an error rather than reading garbage.

### Bounded readers

A reader built from a section stops at that section's end — useful for parsing
a field out of a larger structure:

```cpp
std::uint32_t header = 0xDEADBEEF;
fp::BitReader r{fp::bit_span(header, 8, 23)};
r.remaining();      // 16
r.read(16);         // 0xADBE
r.read(1);          // error: past the section
```

## Text

```cpp
fp::to_binary(std::uint8_t{0xA5});        // "10100101"
fp::to_binary(std::uint8_t{0xA5}, true);  // "1010 0101"
fp::to_binary(section);                   // a section's bits, MSB first
section.to_binary(true);                  // same

fp::to_hex(std::span<const std::byte>(buf));      // "deadbeef"
fp::to_hex(std::span<const std::byte>(buf), true); // "de ad be ef"
fp::to_hex(fp::as_bytes(1.5f));                   // any value's representation
fp::to_hex(std::string_view("AB"));               // "4142"
fp::str::to_upper(fp::to_hex(buf));               // "DEADBEEF"
```

`to_binary` always prints the full width of the type (or the whole section),
most significant bit first — signed values print their two's-complement
representation. `to_hex` is lowercase with two digits per byte; it takes any
1-byte range and no value overload, so `to_hex(x)` for a value must be spelled
`to_hex(as_bytes(x))` — explicit, and impossible to confuse with a numeric
hex conversion.

## Cost

Measured with `bench/bits_bench.cpp` (4096-element loops, GCC 16, `-O2`):

| operation | time | vs hand-written |
|---|---|---|
| `bit_field` read / write | 0.40 / 0.65 ns | same as hand shifts |
| `bit_span` read / write, 16 bits | 0.43 / 0.62 ns | same as `extract_bits` / `insert_bits` |
| `find_next_set` over 32 k bits | 0.76 µs | 23x a byte loop |
| `bit_reverse` u64 | 1.2 ns | ~1.6x a SWAR loop |
| `BitWriter` 64-bit writes | 10.4 ns/value | same as a byte-wise hand loop; 8x the old bit-by-bit code |
| `BitReader` 64-bit reads | 1.1 ns/value | 45x the old bit-by-bit code |
| `BitReader` single bits | 0.77 ns/bit | |
| `popcount` over 32 k bits | 1.5 µs | ~1.4x a byte loop |
| `to_hex` | ~1.0 ns/byte | |
| `to_binary` over 32 k bits | 15 µs | ~5x a per-bit loop |
| iteration over 32 k bits | 18 µs | convenience; use `popcount`/search for hot scans |

`bit_span` read/write and `bit_field` are at parity with hand-written shifts:
the section fast path uses one fixed-size load/store when the range fits a
machine word.

## Limits and notes

- **Debug asserts, defined release.** Out-of-range bit indices and ranges are
  `assert`ed; `NDEBUG` builds get the defined fallbacks described above rather
  than undefined behavior. The compile-time API (`bit_field`, `bit_split`) is
  checked at compile time and never hits those paths.
- **Bit numbering is memory order.** `bit_span` bit 0 is the low bit of the
  first byte. For an integer on a little-endian machine that is the value's bit
  0; on big-endian it is not. `bit_field`/`extract_bits` number bits in *value*
  order, so the two agree on little-endian.
- **Offsets include padding.** A section over a struct sees the raw
  representation, padding bytes included.
- **Signed fields do not sign-extend.** `bit_field<0, 3>(int8_t{-1})` is 15,
  not -1 — the field is read as an unsigned bit pattern.
- **`read`/`write` are 64-bit operations.** Wider sections use `section`,
  `read_at`/`write_at`, `fill`, `copy_from` or per-bit access.
- **`to_hex` takes bytes, not values.** Use `as_bytes(x)` for a value's
  representation; `str::to_hex`-style numeric formatting is a different thing.
- **Thread safety.** Two threads writing different bits in the same byte race
  (read-modify-write); synchronize or partition by byte.
- **`volatile` / memory-mapped registers are not supported** — a `bit_span`
  needs a non-volatile object.
- **Sections over objects are runtime-only.** They go through `as_bytes`
  (`reinterpret_cast`); sections over `std::span<std::byte>` are `constexpr`.
- **`BitWriter::data()`** is a view into the writer's buffer: it is invalidated
  by later writes or `reserve()`.
- **`bit_span::storage()`** is the *whole* backing range; use `section` if you
  want a smaller view.

## Cookbook

**Pack and unpack a pixel format.**

```cpp
std::uint16_t rgb565 = 0;
fp::bit_field<11, 15>(rgb565) = 31;
fp::bit_field<5, 10>(rgb565) = 42;
fp::bit_field<0, 4>(rgb565) = 21;

auto [r, g, b] = fp::bit_split<5, 6, 5>(rgb565);
```

**Read 4-bit quantized weights.**

```cpp
std::vector<std::uint8_t> packed = ...;
fp::bit_span weights{std::span(packed)};
for (std::size_t i = 0; i < weights.size() / 4; ++i)
  use(weights.section(i * 4, i * 4 + 3).read()); // one nibble each
```

**Find the next free slot in a bitmap.**

```cpp
std::uint64_t in_use = ...;
fp::bit_span slots(in_use);
if (auto i = slots.find_first_clear())
  slots.set_bit(*i);
```

**Inspect a float.**

```cpp
float f = 1.5f;
fp::bit_span sign(f, 31, 31), exponent(f, 23, 30), mantissa(f, 0, 22);
sign.write(1);                 // f is now -1.5f
fp::to_binary(exponent);       // "01111111"
```

**Parse an MSB-first format, then an LSB-first one.**

```cpp
fp::MsbReader code{payload};
auto prefix = code.read(3);
auto suffix = code.read(5);
code.align_to_byte();
```

**Mixed orders in one stream (DEFLATE-style).**

```cpp
fp::BitWriter w;
w.write(block_type, 2);                          // LSB-first header
w.write<fp::BitOrder::MsbFirst>(huffman_code, 7); // MSB-first code
```

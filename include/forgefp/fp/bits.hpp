#pragma once
#include "result.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp {

template <bool Const> class basic_bit_span; // defined below

// Bit-level access to values.
//
// Most helpers work on the *object representation*, so the same function
// applies to integers, floats and whole structs without casting first. Bit 0
// is the least significant bit and every range is inclusive [lo, hi].
//
//   values     bit, set_bit, clear_bit, toggle_bit
//              low_mask, bit_mask, extract_bits, insert_bits
//              popcount, count_zeros, leading_zeros, trailing_zeros,
//              bit_width, has_single_bit, rotl, rotr, byteswap, sign_bit,
//              bit_reverse
//              bit_not, bit_and, bit_or, bit_xor (whole values)
//   bytes      bytes_of, as_bytes, word_at,
//              to_little_endian / to_big_endian (and from_*)
//   sections   bit_span / const_bit_span — a view of a bit range of an object
//              or of bytes: read/write, indexing, iteration, slicing,
//              search, fill/flip/popcount, copy/compare
//              bit_field<Lo,Hi> / bit_split<Widths...> — compile-time fields
//   streams    BitWriter / BitReader (LSB-first) and MsbWriter / MsbReader
//              (MSB-first), with alignment, section I/O and peeking
//   text       to_binary, to_hex
//
// Guarantees:
//   - constexpr wherever it can be: values, sections over byte ranges,
//     bit_field, bit_split and the bit writer.
//   - no undefined behavior for any input. Out-of-range arguments are
//     `assert`ed, so debug builds catch the bug; release builds stay defined
//     (reads give 0, writes leave the value unchanged, a bit_span is clamped
//     to its storage, stream reads report an error).
//   - no allocation except the strings returned by to_binary/to_hex and the
//     writer's internal buffer.

// --- object representation ---------------------------------------------------

// The bytes of a value, in native byte order.
template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr std::array<std::byte, sizeof(T)> bytes_of(T const &x) noexcept {
  return std::bit_cast<std::array<std::byte, sizeof(T)>>(x);
}

// A view of a value's bytes. The non-const overload is writable, so
// `as_bytes(x)[0] = std::byte{0}` edits `x` in place.
template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr std::span<std::byte const> as_bytes(T const &x) noexcept {
  return std::as_bytes(std::span<T const>(std::addressof(x), 1));
}

template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr std::span<std::byte> as_bytes(T &x) noexcept {
  return std::as_writable_bytes(std::span<T>(std::addressof(x), 1));
}

// Reinterpret sizeof(Word) bytes starting at byte_offset as a Word, in native
// byte order. Bytes past the end of the object read as zero.
template <class Word = std::uint64_t, class T>
  requires(std::is_trivially_copyable_v<T> && std::is_integral_v<Word> &&
           !std::is_same_v<std::remove_cv_t<Word>, bool>)
constexpr Word word_at(T const &x, std::size_t byte_offset = 0) noexcept {
  std::array<std::byte, sizeof(Word)> word{};
  if (byte_offset >= sizeof(T))
    return std::bit_cast<Word>(word); // entirely past the end: zero
  if (std::is_constant_evaluated()) {
    const auto bytes = bytes_of(x);
    for (std::size_t i = 0; i < sizeof(Word); ++i)
      if (byte_offset + i < sizeof(T))
        word[i] = bytes[byte_offset + i];
  } else if (byte_offset + sizeof(Word) <= sizeof(T)) {
    std::memcpy(word.data(), as_bytes(x).data() + byte_offset, sizeof(Word));
  } else {
    const auto bytes = as_bytes(x);
    for (std::size_t i = 0; i < sizeof(Word); ++i)
      if (byte_offset + i < sizeof(T))
        word[i] = bytes[byte_offset + i];
  }
  return std::bit_cast<Word>(word);
}

namespace detail {

template <std::size_t N> struct uint_of_size;
template <> struct uint_of_size<1> { using type = std::uint8_t; };
template <> struct uint_of_size<2> { using type = std::uint16_t; };
template <> struct uint_of_size<4> { using type = std::uint32_t; };
template <> struct uint_of_size<8> { using type = std::uint64_t; };

template <class T>
concept word_sized = requires { typename uint_of_size<sizeof(T)>::type; };

template <class T>
inline constexpr bool is_bool_v =
    std::is_same_v<std::remove_cv_t<T>, bool>;

// Bit-reversed byte, table driven (no per-bit loop at the call site).
struct bit_reverse_table {
  unsigned char map[256] = {};
  constexpr bit_reverse_table() noexcept {
    for (unsigned i = 0; i < 256; ++i) {
      unsigned char value = static_cast<unsigned char>(i);
      unsigned char reversed = 0;
      for (int b = 0; b < 8; ++b)
        reversed = static_cast<unsigned char>((reversed << 1) | ((value >> b) & 1u));
      map[i] = reversed;
    }
  }
};
inline constexpr bit_reverse_table reversed_bytes{};

constexpr unsigned char bit_reverse_byte(unsigned char b) noexcept {
  return reversed_bytes.map[b];
}

template <class T> struct is_span_like : std::false_type {};
template <class T, std::size_t N>
struct is_span_like<std::span<T, N>> : std::true_type {};

template <class T> struct is_bit_span_like : std::false_type {};
template <bool C> struct is_bit_span_like<basic_bit_span<C>> : std::true_type {};

// Byte storage: any contiguous range of 1-byte trivially copyable elements
// (std::byte, unsigned char, char, ...), whether a span or a container.
template <class T>
concept byte_range =
    std::ranges::contiguous_range<std::remove_cvref_t<T>> &&
    (sizeof(std::ranges::range_value_t<std::remove_cvref_t<T>>) == 1) &&
    std::is_trivially_copyable_v<
        std::ranges::range_value_t<std::remove_cvref_t<T>>>;

// What basic_bit_span can be built from: an object's representation. Spans and
// bit spans are excluded, or the explicit object constructor would outrank the
// copy/converting constructors (T& is an exact match, T const& is not).
template <class T, bool Const>
concept bit_span_object =
    std::is_trivially_copyable_v<std::remove_cv_t<T>> &&
    !is_span_like<std::remove_cv_t<T>>::value &&
    !is_bit_span_like<std::remove_cv_t<T>>::value &&
    (Const || !std::is_const_v<T>);

// Reverse the bytes of an unsigned word. The mask form is what compilers
// recognise as a byte swap (a byte-array reversal becomes a SIMD shuffle).
template <class W>
constexpr W byte_reverse(W v) noexcept {
  static_assert(sizeof(W) <= 8, "byte_reverse: word wider than 64 bits");
  if constexpr (sizeof(W) == 1) {
    return v;
  } else if constexpr (sizeof(W) == 2) {
    return static_cast<W>((v >> 8) | (v << 8));
  } else if constexpr (sizeof(W) == 4) {
    return static_cast<W>(((v & W{0x000000FF}) << 24) |
                          ((v & W{0x0000FF00}) << 8) |
                          ((v & W{0x00FF0000}) >> 8) |
                          ((v & W{0xFF000000}) >> 24));
  } else {
    return static_cast<W>(
        ((v & W{0x00000000000000FF}) << 56) |
        ((v & W{0x000000000000FF00}) << 40) |
        ((v & W{0x0000000000FF0000}) << 24) |
        ((v & W{0x00000000FF000000}) << 8) |
        ((v & W{0x000000FF00000000}) >> 8) |
        ((v & W{0x0000FF0000000000}) >> 24) |
        ((v & W{0x00FF000000000000}) >> 40) |
        ((v & W{0xFF00000000000000}) >> 56));
  }
}

} // namespace detail

// --- single bits -------------------------------------------------------------

// Integral types the bit helpers accept (bool is excluded: its object
// representation is not guaranteed to stay 0/1 under bitwise ops).
template <class T>
concept bit_integral =
    std::integral<T> && !std::is_same_v<std::remove_cv_t<T>, bool>;

// Read bit `i`.
template <bit_integral T>
constexpr bool bit(T x, unsigned i) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  assert(i < digits);
  if (i >= digits)
    return false; // precondition violated
  return ((static_cast<U>(x) >> i) & 1u) != 0u;
}

// Return `x` with bit `i` set to `value` (set when value is true).
template <bit_integral T>
constexpr T set_bit(T x, unsigned i, bool value = true) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  assert(i < digits);
  if (i >= digits)
    return x; // precondition violated
  const U mask = static_cast<U>(U{1} << i);
  return static_cast<T>(value ? static_cast<U>(x) | mask
                              : static_cast<U>(x) & static_cast<U>(~mask));
}

template <bit_integral T>
constexpr T clear_bit(T x, unsigned i) noexcept {
  return set_bit(x, i, false);
}

template <bit_integral T>
constexpr T toggle_bit(T x, unsigned i) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  assert(i < digits);
  if (i >= digits)
    return x; // precondition violated
  return static_cast<T>(static_cast<U>(x) ^ static_cast<U>(U{1} << i));
}

// --- bit ranges --------------------------------------------------------------

// A mask with bits [0, n) set; n >= digits gives all ones.
template <bit_integral T>
constexpr T low_mask(unsigned n) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  return n >= digits ? static_cast<T>(~U{0})
                     : static_cast<T>(static_cast<U>(U{1} << n) - 1u);
}

// A mask with bits [lo, hi] set.
template <bit_integral T>
constexpr T bit_mask(unsigned lo, unsigned hi) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  assert(lo <= hi);
  assert(hi < digits);
  if (lo > hi || hi >= digits)
    return 0; // empty or out-of-range: no bits
  return static_cast<T>(static_cast<U>(low_mask<T>(hi - lo + 1)) << lo);
}

// The value of bits [lo, hi], shifted down to bit 0.
template <bit_integral T>
constexpr T extract_bits(T x, unsigned lo, unsigned hi) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  if (lo > hi || hi >= digits)
    return 0; // empty or out-of-range
  return static_cast<T>((static_cast<U>(x) >> lo) &
                        static_cast<U>(low_mask<T>(hi - lo + 1)));
}

// Return `x` with bits [lo, hi] replaced by `value`.
template <bit_integral T, bit_integral U>
constexpr T insert_bits(T x, unsigned lo, unsigned hi, U value) noexcept {
  using V = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<V>::digits;
  if (lo > hi || hi >= digits)
    return x; // empty or out-of-range: nothing to replace
  const V mask = static_cast<V>(bit_mask<T>(lo, hi));
  // Widen to V *before* shifting: shifting in U would be UB when lo is at or
  // beyond U's width (e.g. inserting a uint8_t at bit 60 of a uint64_t).
  const V shifted = static_cast<V>(static_cast<V>(value) << lo);
  return static_cast<T>((static_cast<V>(x) & static_cast<V>(~mask)) |
                        (shifted & mask));
}

// --- counting and rearranging ------------------------------------------------

template <bit_integral T>
constexpr int popcount(T x) noexcept {
  return std::popcount(static_cast<std::make_unsigned_t<T>>(x));
}

template <bit_integral T>
constexpr int count_zeros(T x) noexcept {
  return std::numeric_limits<std::make_unsigned_t<T>>::digits - popcount(x);
}

template <bit_integral T>
constexpr int leading_zeros(T x) noexcept {
  return std::countl_zero(static_cast<std::make_unsigned_t<T>>(x));
}

template <bit_integral T>
constexpr int trailing_zeros(T x) noexcept {
  return std::countr_zero(static_cast<std::make_unsigned_t<T>>(x));
}

// Number of bits needed to represent x (0 for 0).
template <bit_integral T>
constexpr int bit_width(T x) noexcept {
  return std::bit_width(static_cast<std::make_unsigned_t<T>>(x));
}

template <bit_integral T>
constexpr bool has_single_bit(T x) noexcept {
  return std::has_single_bit(static_cast<std::make_unsigned_t<T>>(x));
}

// Rotate by `n` bits; negative n rotates the other way.
template <bit_integral T>
constexpr T rotl(T x, int n) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr int digits = std::numeric_limits<U>::digits;
  int s = n % digits;
  if (s < 0)
    s += digits;
  const U u = static_cast<U>(x);
  return static_cast<T>(s == 0 ? u
                               : static_cast<U>((u << s) | (u >> (digits - s))));
}

template <bit_integral T>
constexpr T rotr(T x, int n) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr int digits = std::numeric_limits<U>::digits;
  int s = n % digits;
  if (s < 0)
    s += digits;
  // Rotating right by s is rotating left by digits - s. Never negate n:
  // -INT_MIN overflows.
  return rotl(x, (digits - s) % digits);
}

// Reverse the byte order of any trivially copyable value.
template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr T byteswap(T x) noexcept {
  if constexpr (detail::word_sized<T>) {
    using W = typename detail::uint_of_size<sizeof(T)>::type;
    return std::bit_cast<T>(detail::byte_reverse(std::bit_cast<W>(x)));
  } else {
    auto bytes = bytes_of(x);
    std::reverse(bytes.begin(), bytes.end());
    return std::bit_cast<T>(bytes);
  }
}

// Convert to/from a fixed byte order. Byte reversal is its own inverse, so
// both directions are the same operation; the names document intent.
template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr T to_little_endian(T x) noexcept {
  if constexpr (std::endian::native == std::endian::little)
    return x;
  else
    return byteswap(x);
}

template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr T from_little_endian(T x) noexcept {
  return to_little_endian(x);
}

template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr T to_big_endian(T x) noexcept {
  if constexpr (std::endian::native == std::endian::big)
    return x;
  else
    return byteswap(x);
}

template <class T>
  requires std::is_trivially_copyable_v<T>
constexpr T from_big_endian(T x) noexcept {
  return to_big_endian(x);
}

// Reverse the bits of an integer (RBIT semantics). Complements `byteswap`,
// which reverses byte order only.
template <bit_integral T>
constexpr T bit_reverse(T x) noexcept {
  using U = std::make_unsigned_t<T>;
  const U v = static_cast<U>(x);
  if constexpr (sizeof(T) == 8) {
    // Swap-and-mask: a handful of ALU ops, no table.
    std::uint64_t w = v;
    w = ((w >> 1) & 0x5555555555555555ull) |
        ((w & 0x5555555555555555ull) << 1);
    w = ((w >> 2) & 0x3333333333333333ull) |
        ((w & 0x3333333333333333ull) << 2);
    w = ((w >> 4) & 0x0F0F0F0F0F0F0F0Full) |
        ((w & 0x0F0F0F0F0F0F0F0Full) << 4);
    w = ((w >> 8) & 0x00FF00FF00FF00FFull) |
        ((w & 0x00FF00FF00FF00FFull) << 8);
    w = ((w >> 16) & 0x0000FFFF0000FFFFull) |
        ((w & 0x0000FFFF0000FFFFull) << 16);
    return static_cast<T>(static_cast<U>((w >> 32) | (w << 32)));
  } else if constexpr (sizeof(T) == 4) {
    std::uint32_t w = v;
    w = ((w >> 1) & 0x55555555u) | ((w & 0x55555555u) << 1);
    w = ((w >> 2) & 0x33333333u) | ((w & 0x33333333u) << 2);
    w = ((w >> 4) & 0x0F0F0F0Fu) | ((w & 0x0F0F0F0Fu) << 4);
    w = ((w >> 8) & 0x00FF00FFu) | ((w & 0x00FF00FFu) << 8);
    return static_cast<T>(static_cast<U>((w >> 16) | (w << 16)));
  } else {
    // Any other width: reverse the bytes, then the bits within each byte.
    const auto bytes = bytes_of(v);
    std::array<std::byte, sizeof(T)> out{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      const auto byte = std::to_integer<unsigned char>(bytes[i]);
      out[sizeof(T) - 1 - i] =
          static_cast<std::byte>(detail::bit_reverse_byte(byte));
    }
    return static_cast<T>(std::bit_cast<U>(out));
  }
}

// Reverse the low `width` bits (the rest become zero).
template <bit_integral T>
constexpr T bit_reverse(T x, unsigned width) noexcept {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  if (width == 0)
    return 0;
  const U full = static_cast<U>(bit_reverse(x));
  if (width >= digits)
    return static_cast<T>(full);
  return static_cast<T>(full >> (digits - width));
}

template <std::floating_point T>
constexpr bool sign_bit(T x) noexcept {
  return std::signbit(x);
}

template <bit_integral T>
constexpr bool sign_bit(T x) noexcept {
  return x < 0;
}

// --- whole-value bitwise ops -------------------------------------------------

// The bitwise operators, lifted from integers to any trivially copyable value
// (floats, structs, arrays) by applying them to the object representation.
// Sizes with a matching unsigned integer type (1/2/4/8 bytes) take the
// single-instruction path; other sizes fall back to a byte loop.
//
// bool is excluded: its object representation is not guaranteed to stay 0/1
// under bitwise ops. Structs *containing* bool have the same caveat.

namespace detail {

template <class T, class U, class Op>
constexpr T bit_zip(T a, U b, Op op) noexcept {
  if constexpr (word_sized<T>) {
    using W = typename uint_of_size<sizeof(T)>::type;
    return std::bit_cast<T>(
        static_cast<W>(op(std::bit_cast<W>(a), std::bit_cast<W>(b))));
  } else {
    auto x = bytes_of(a);
    const auto y = bytes_of(b);
    for (std::size_t i = 0; i < sizeof(T); ++i)
      x[i] = op(x[i], y[i]);
    return std::bit_cast<T>(x);
  }
}

} // namespace detail

template <class T>
  requires(std::is_trivially_copyable_v<T> && !detail::is_bool_v<T>)
constexpr T bit_not(T x) noexcept {
  if constexpr (detail::word_sized<T>) {
    using W = typename detail::uint_of_size<sizeof(T)>::type;
    return std::bit_cast<T>(static_cast<W>(~std::bit_cast<W>(x)));
  } else {
    auto bytes = bytes_of(x);
    for (auto &b : bytes)
      b = ~b;
    return std::bit_cast<T>(bytes);
  }
}

template <class T, class U>
  requires(std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<U> &&
           sizeof(T) == sizeof(U) && !detail::is_bool_v<T> &&
           !detail::is_bool_v<U>)
constexpr T bit_and(T a, U b) noexcept {
  return detail::bit_zip(a, b, [](auto x, auto y) { return x & y; });
}

template <class T, class U>
  requires(std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<U> &&
           sizeof(T) == sizeof(U) && !detail::is_bool_v<T> &&
           !detail::is_bool_v<U>)
constexpr T bit_or(T a, U b) noexcept {
  return detail::bit_zip(a, b, [](auto x, auto y) { return x | y; });
}

template <class T, class U>
  requires(std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<U> &&
           sizeof(T) == sizeof(U) && !detail::is_bool_v<T> &&
           !detail::is_bool_v<U>)
constexpr T bit_xor(T a, U b) noexcept {
  return detail::bit_zip(a, b, [](auto x, auto y) { return x ^ y; });
}

// --- text --------------------------------------------------------------------

namespace detail {
struct binary_table {
  char chars[64] = {};
  constexpr binary_table() noexcept {
    for (int v = 0; v < 16; ++v)
      for (int b = 0; b < 4; ++b)
        chars[v * 4 + b] = ((v >> (3 - b)) & 1) != 0 ? '1' : '0';
  }
};
inline constexpr binary_table binary_digits{};
} // namespace detail

// The bits of x, most significant first. `group` adds a space every 4 bits.
template <bit_integral T>
std::string to_binary(T x, bool group = false) {
  using U = std::make_unsigned_t<T>;
  constexpr unsigned digits = std::numeric_limits<U>::digits;
  constexpr unsigned nibbles = (digits + 3) / 4;
  const U u = static_cast<U>(x);
  // Filled with spaces: the unrolled nibble stores below overwrite the digits,
  // and with `group` the spaces between nibbles are exactly what remains.
  std::string out(digits + (group ? nibbles - 1 : 0), ' ');
  if constexpr (digits % 4 == 0) {
    // One 4-byte store per nibble, fully unrolled at compile time.
    auto emit = [&]<bool Group, std::size_t... I>(std::index_sequence<I...>) {
      constexpr std::size_t stride = Group ? 5 : 4;
      ((std::memcpy(out.data() + I * stride,
                    detail::binary_digits.chars +
                        ((u >> ((nibbles - 1 - I) * 4)) & 0xFu) * 4,
                    4)),
       ...);
    };
    if (group)
      emit.template operator()<true>(std::make_index_sequence<nibbles>{});
    else
      emit.template operator()<false>(std::make_index_sequence<nibbles>{});
  } else {
    std::size_t pos = 0;
    for (unsigned n = nibbles; n-- > 0;) {
      const unsigned shift = n * 4;
      const unsigned width = std::min(4u, digits - shift);
      const unsigned v =
          static_cast<unsigned>((u >> shift) & ((U{1} << width) - 1u));
      for (unsigned b = width; b-- > 0;)
        out[pos++] = ((v >> b) & 1u) != 0 ? '1' : '0';
      if (group && n > 0)
        out[pos++] = ' ';
    }
  }
  return out;
}

namespace detail {
constexpr unsigned char to_uchar(std::byte b) noexcept {
  return std::to_integer<unsigned char>(b);
}
template <class E>
constexpr unsigned char to_uchar(E e) noexcept {
  return static_cast<unsigned char>(e);
}
} // namespace detail

// Hex dump of a byte range: lowercase, two digits per byte, no separator
// unless `group` asks for a space between bytes. For the object representation
// of a value, pass `as_bytes(x)`. For uppercase, compose with fp::str::to_upper.
template <class Bytes>
  requires(!detail::is_bit_span_like<std::remove_cvref_t<Bytes>>::value &&
           detail::byte_range<Bytes>)
std::string to_hex(Bytes &&bytes, bool group = false) {
  constexpr char digits[] = "0123456789abcdef";
  const auto view = std::span(bytes);
  const std::size_t count = view.size();
  std::string out;
  if (count == 0)
    return out;
  out.resize(count * (group ? 3 : 2) - (group ? 1 : 0));
  std::size_t pos = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const unsigned byte = detail::to_uchar(view[i]);
    out[pos++] = digits[byte >> 4];
    out[pos++] = digits[byte & 0x0Fu];
    if (group && i + 1 < count)
      out[pos++] = ' ';
  }
  return out;
}

// --- bit sections ------------------------------------------------------------

// A non-owning view of a range of bits: of an object's representation, or of
// raw bytes. `bit_span(x)` covers all of x, `bit_span(x, lo, hi)` covers bits
// [lo, hi] of it (inclusive, bit 0 = least significant).
//
// Views are cheap to copy, can be sliced (`section`, `split`), iterated, and
// read/written as a whole (up to 64 bits per call) or bit by bit. Writing
// through a view edits the underlying object in place.
template <bool Const = false> class basic_bit_span {
public:
  using byte_type = std::conditional_t<Const, std::byte const, std::byte>;

  // --- construction ----------------------------------------------------------

  // All the bits of an object.
  template <class T>
    requires(detail::bit_span_object<T, Const> && !detail::byte_range<T>)
  constexpr explicit basic_bit_span(T &obj) noexcept
      : bytes_(as_bytes(obj)), offset_(0), size_(bytes_.size() * 8) {}

  // Bits [lo, hi] of an object.
  template <class T>
    requires(detail::bit_span_object<T, Const> && !detail::byte_range<T>)
  constexpr basic_bit_span(T &obj, std::size_t lo, std::size_t hi) noexcept
      : bytes_(as_bytes(obj)), offset_(lo), size_(hi - lo + 1) {
    assert(lo <= hi);
    assert(hi < bytes_.size() * 8);
    clamp_to_storage();
  }

  // All the bits of a byte range: a span or any contiguous container of 1-byte
  // elements (std::byte, unsigned char, char). Constrained to 1-byte ranges so
  // the compiler never probes std::span's range constructor for bit spans
  // themselves (which recurses through the range concepts).
  template <class Bytes>
    requires(!detail::is_bit_span_like<std::remove_cvref_t<Bytes>>::value &&
             detail::byte_range<Bytes> &&
             (Const || !std::is_const_v<std::ranges::range_value_t<
                           std::remove_cvref_t<Bytes>>>))
  constexpr explicit basic_bit_span(Bytes &&bytes) noexcept
      : bytes_(as_byte_view(std::forward<Bytes>(bytes))),
        offset_(0),
        size_(bytes_.size() * 8) {}

  // Bits [lo, hi] of a byte range.
  template <class Bytes>
    requires(!detail::is_bit_span_like<std::remove_cvref_t<Bytes>>::value &&
             detail::byte_range<Bytes> &&
             (Const || !std::is_const_v<std::ranges::range_value_t<
                           std::remove_cvref_t<Bytes>>>))
  constexpr basic_bit_span(Bytes &&bytes, std::size_t lo,
                           std::size_t hi) noexcept
      : bytes_(as_byte_view(std::forward<Bytes>(bytes))),
        offset_(lo),
        size_(hi - lo + 1) {
    assert(lo <= hi);
    assert(hi < bytes_.size() * 8);
    clamp_to_storage();
  }

  // An empty view (useful as a member and for singular iterators).
  constexpr basic_bit_span() noexcept = default;

  // Mutable views convert to const ones, never the other way around.
  template <bool OtherConst>
    requires(Const || !OtherConst)
  constexpr basic_bit_span(basic_bit_span<OtherConst> const &other) noexcept
      : bytes_(other.storage()), offset_(other.offset()), size_(other.size()) {}

  // A span of 1-byte elements viewed as bytes. Same-element spans (the common
  // std::byte case) convert directly, which keeps them constexpr; other
  // elements need std::as_bytes (a reinterpret_cast), so those are runtime-only.
  template <class Bytes>
  static constexpr std::span<byte_type> as_byte_view(Bytes &&bytes) noexcept {
    const auto view = std::span(bytes);
    using Element = typename decltype(view)::element_type;
    if constexpr (std::is_same_v<Element, std::remove_const_t<byte_type>>)
      return std::span<byte_type>(view);
    else if constexpr (Const)
      return std::as_bytes(view);
    else
      return std::as_writable_bytes(view);
  }

  // --- shape -----------------------------------------------------------------

  constexpr std::size_t size() const noexcept { return size_; }      // bits
  constexpr std::size_t size_bytes() const noexcept { return (size_ + 7) / 8; }
  constexpr std::size_t offset() const noexcept { return offset_; }
  constexpr bool empty() const noexcept { return size_ == 0; }
  // The whole backing range, not just this section's bytes.
  constexpr std::span<byte_type> storage() const noexcept { return bytes_; }

  // --- single bits -----------------------------------------------------------

  constexpr bool get_bit(std::size_t i) const noexcept {
    assert(i < size_);
    if (i >= size_)
      return false; // release safety
    const std::size_t pos = offset_ + i;
    return ((std::to_integer<unsigned>(bytes_[pos / 8]) >> (pos % 8)) & 1u) !=
           0u;
  }

  constexpr void set_bit(std::size_t i, bool value = true) const noexcept {
    assert(i < size_);
    if (i >= size_)
      return; // release safety
    const std::size_t pos = offset_ + i;
    unsigned byte = std::to_integer<unsigned>(bytes_[pos / 8]);
    const unsigned mask = 1u << (pos % 8);
    byte = value ? (byte | mask) : (byte & ~mask);
    bytes_[pos / 8] = static_cast<std::byte>(byte);
  }

  constexpr void toggle_bit(std::size_t i) const noexcept {
    assert(i < size_);
    if (i >= size_)
      return; // release safety
    const std::size_t pos = offset_ + i;
    const unsigned byte = std::to_integer<unsigned>(bytes_[pos / 8]);
    bytes_[pos / 8] = static_cast<std::byte>(byte ^ (1u << (pos % 8)));
  }

  // --- whole-section access (up to 64 bits) ----------------------------------

  // The section's bits, shifted down to bit 0.
  constexpr std::uint64_t read() const noexcept {
    return read_at(0, static_cast<unsigned>(size_));
  }

  // Replace the section's bits with the low bits of `value`.
  constexpr void write(std::uint64_t value) const noexcept {
    write_at(0, value, static_cast<unsigned>(size_));
  }

private:
  // One fixed-size load covering `need` bytes at `first`. Sets `loaded` to the
  // number of bytes read (0 = no safe fixed-size access, use the slow path).
  constexpr std::uint64_t load_word(std::size_t first, unsigned need,
                                    unsigned &loaded) const noexcept {
    loaded = 0;
    std::uint64_t word = 0;
    if (first >= bytes_.size())
      return 0; // nothing safe to read
    const std::size_t avail = bytes_.size() - first;
    if (need <= 8 && avail >= 8) {
      std::memcpy(&word, bytes_.data() + first, 8);
      loaded = 8;
    } else if (need <= 4 && avail >= 4) {
      std::uint32_t part = 0;
      std::memcpy(&part, bytes_.data() + first, 4);
      word = part;
      loaded = 4;
    } else if (need <= 2 && avail >= 2) {
      std::uint16_t part = 0;
      std::memcpy(&part, bytes_.data() + first, 2);
      word = part;
      loaded = 2;
    } else if (need <= 1) {
      std::uint8_t part = 0;
      std::memcpy(&part, bytes_.data() + first, 1);
      word = part;
      loaded = 1;
    }
    return word;
  }

  constexpr void store_word(std::size_t first, unsigned loaded,
                            std::uint64_t word) const noexcept {
    if (loaded == 8) {
      std::memcpy(bytes_.data() + first, &word, 8);
    } else if (loaded == 4) {
      const auto part = static_cast<std::uint32_t>(word);
      std::memcpy(bytes_.data() + first, &part, 4);
    } else if (loaded == 2) {
      const auto part = static_cast<std::uint16_t>(word);
      std::memcpy(bytes_.data() + first, &part, 2);
    } else {
      const auto part = static_cast<std::uint8_t>(word);
      std::memcpy(bytes_.data() + first, &part, 1);
    }
  }

public:
  // `count` bits starting at bit `bit` of this section.
  constexpr std::uint64_t read_at(std::size_t bit,
                                  unsigned count) const noexcept {
    assert(count <= 64);
    assert(bit + count <= size_);
    if (count > 64)
      count = 64; // release safety; folds away for constant counts
    const std::size_t start = offset_ + bit;
    const std::size_t first = start / 8;
    const unsigned bit_off = static_cast<unsigned>(start % 8);
    const unsigned need = (bit_off + count + 7) / 8;
    if (need <= 8 && !std::is_constant_evaluated()) {
      unsigned loaded = 0;
      const std::uint64_t word = load_word(first, need, loaded);
      if (loaded != 0)
        return (word >> bit_off) & low_mask<std::uint64_t>(count);
    }
    std::uint64_t out = 0;
    unsigned filled = 0;
    std::size_t pos = start;
    while (filled < count && pos / 8 < bytes_.size()) {
      const unsigned off = static_cast<unsigned>(pos % 8);
      const unsigned take = std::min(count - filled, 8u - off);
      const auto byte = std::to_integer<std::uint64_t>(bytes_[pos / 8]);
      out |= ((byte >> off) & ((std::uint64_t{1} << take) - 1)) << filled;
      filled += take;
      pos += take;
    }
    return out;
  }

  constexpr void write_at(std::size_t bit, std::uint64_t value,
                          unsigned count) const noexcept {
    assert(count <= 64);
    assert(bit + count <= size_);
    if (count > 64)
      count = 64; // release safety; folds away for constant counts
    if (count < 64)
      value &= (std::uint64_t{1} << count) - 1;
    const std::size_t start = offset_ + bit;
    const std::size_t first = start / 8;
    const unsigned bit_off = static_cast<unsigned>(start % 8);
    const unsigned need = (bit_off + count + 7) / 8;
    if (need <= 8 && !std::is_constant_evaluated()) {
      unsigned loaded = 0;
      std::uint64_t word = load_word(first, need, loaded);
      if (loaded != 0) {
        const std::uint64_t mask =
            low_mask<std::uint64_t>(count) << bit_off;
        word = (word & ~mask) | ((value << bit_off) & mask);
        store_word(first, loaded, word);
        return;
      }
    }
    unsigned filled = 0;
    std::size_t pos = start;
    while (filled < count && pos / 8 < bytes_.size()) {
      const unsigned off = static_cast<unsigned>(pos % 8);
      const unsigned take = std::min(count - filled, 8u - off);
      const std::uint64_t mask = (std::uint64_t{1} << take) - 1;
      const unsigned byte = std::to_integer<unsigned>(bytes_[pos / 8]);
      const unsigned merged =
          (byte & ~static_cast<unsigned>(mask << off)) |
          static_cast<unsigned>(((value >> filled) & mask) << off);
      bytes_[pos / 8] = static_cast<std::byte>(merged);
      filled += take;
      pos += take;
    }
  }

  // --- bulk ------------------------------------------------------------------

  constexpr void fill(bool value) const noexcept {
    if (size_ == 0)
      return;
    const std::size_t first = offset_ / 8;
    const std::size_t last = (offset_ + size_ - 1) / 8;
    for (std::size_t i = first; i <= last; ++i) {
      unsigned mask = 0xFFu;
      if (i == first)
        mask &= 0xFFu << (offset_ % 8);
      if (i == last) {
        const unsigned used = static_cast<unsigned>((offset_ + size_) % 8);
        if (used != 0)
          mask &= (1u << used) - 1u;
      }
      const unsigned byte = std::to_integer<unsigned>(bytes_[i]);
      bytes_[i] = static_cast<std::byte>(value ? (byte | mask)
                                               : (byte & ~mask));
    }
  }

  constexpr int popcount() const noexcept {
    int total = 0;
    std::size_t done = 0;
    while (done < size_) {
      const unsigned chunk =
          static_cast<unsigned>(std::min<std::size_t>(64, size_ - done));
      total += std::popcount(read_at(done, chunk));
      done += chunk;
    }
    return total;
  }

  // Copy another section of the same width into this one. Overlapping ranges
  // are handled (memmove semantics), so an in-place shift works:
  //   s.section(0, n - 2).copy_from(s.section(1, n - 1));
  constexpr void copy_from(basic_bit_span<true> src) const noexcept {
    assert(src.size() == size_);
    if (size_ == 0)
      return;
    // Copy in the direction that cannot clobber a chunk before it is read.
    const bool forward = !overlaps(src) || starts_before(src);
    if (forward) {
      for (std::size_t done = 0; done < size_;) {
        const unsigned chunk =
            static_cast<unsigned>(std::min<std::size_t>(64, size_ - done));
        write_at(done, src.read_at(done, chunk), chunk);
        done += chunk;
      }
    } else {
      std::size_t remaining = size_;
      while (remaining > 0) {
        const unsigned chunk =
            static_cast<unsigned>(std::min<std::size_t>(64, remaining));
        const std::size_t at = remaining - chunk;
        write_at(at, src.read_at(at, chunk), chunk);
        remaining = at;
      }
    }
  }

  constexpr bool equal(basic_bit_span<true> other) const noexcept {
    if (size() != other.size())
      return false;
    std::size_t done = 0;
    while (done < size_) {
      const unsigned chunk =
          static_cast<unsigned>(std::min<std::size_t>(64, size_ - done));
      if (read_at(done, chunk) != other.read_at(done, chunk))
        return false;
      done += chunk;
    }
    return true;
  }

  // --- predicates and search -------------------------------------------------

  constexpr bool any() const noexcept { return popcount() != 0; }
  constexpr bool none() const noexcept { return popcount() == 0; }
  constexpr bool all() const noexcept {
    return static_cast<std::size_t>(popcount()) == size_;
  }

  // Invert every bit in the section.
  constexpr void flip() const noexcept {
    if (size_ == 0)
      return;
    const std::size_t first = offset_ / 8;
    const std::size_t last = (offset_ + size_ - 1) / 8;
    for (std::size_t i = first; i <= last; ++i) {
      unsigned mask = 0xFFu;
      if (i == first)
        mask &= 0xFFu << (offset_ % 8);
      if (i == last) {
        const unsigned used = static_cast<unsigned>((offset_ + size_) % 8);
        if (used != 0)
          mask &= (1u << used) - 1u;
      }
      bytes_[i] ^= static_cast<std::byte>(mask);
    }
  }

  // Index of the first set bit at or after `from` (bitmap scanning).
  constexpr std::optional<std::size_t>
  find_next_set(std::size_t from = 0) const noexcept {
    std::size_t bit = from;
    while (bit < size_) {
      const unsigned chunk =
          static_cast<unsigned>(std::min<std::size_t>(64, size_ - bit));
      const std::uint64_t word = read_at(bit, chunk);
      if (word != 0)
        return bit + static_cast<std::size_t>(std::countr_zero(word));
      bit += chunk;
    }
    return std::nullopt;
  }

  constexpr std::optional<std::size_t>
  find_next_clear(std::size_t from = 0) const noexcept {
    std::size_t bit = from;
    while (bit < size_) {
      const unsigned chunk =
          static_cast<unsigned>(std::min<std::size_t>(64, size_ - bit));
      const std::uint64_t zeros =
          ~read_at(bit, chunk) & low_mask<std::uint64_t>(chunk);
      if (zeros != 0)
        return bit + static_cast<std::size_t>(std::countr_zero(zeros));
      bit += chunk;
    }
    return std::nullopt;
  }

  [[nodiscard]] constexpr std::optional<std::size_t> find_first_set() const noexcept {
    return find_next_set(0);
  }

  [[nodiscard]] constexpr std::optional<std::size_t> find_first_clear() const noexcept {
    return find_next_clear(0);
  }

  // Index of the highest set bit.
  [[nodiscard]] constexpr std::optional<std::size_t> find_last_set() const noexcept {
    std::size_t end = size_;
    while (end > 0) {
      const std::size_t start = end >= 64 ? end - 64 : 0;
      const unsigned chunk = static_cast<unsigned>(end - start);
      const std::uint64_t word = read_at(start, chunk);
      if (word != 0)
        return start + 63u -
               static_cast<std::size_t>(std::countl_zero(word));
      end = start;
    }
    return std::nullopt;
  }

  // --- slicing ---------------------------------------------------------------

  // Bits [lo, hi] of this section.
  constexpr basic_bit_span section(std::size_t lo,
                                   std::size_t hi) const noexcept {
    assert(lo <= hi);
    assert(hi < size_);
    return basic_bit_span(bytes_, offset_ + lo, offset_ + hi);
  }

  // This section cut in two at bit `at`: [0, at-1] and [at, size-1].
  constexpr std::pair<basic_bit_span, basic_bit_span>
  split(std::size_t at) const noexcept {
    assert(at > 0 && at < size_);
    return {section(0, at - 1), section(at, size_ - 1)};
  }

  // --- element access and iteration ------------------------------------------

  // Assignable proxy for one bit of a mutable span.
  class reference {
  public:
    constexpr reference(std::span<byte_type> bytes, std::size_t pos) noexcept
        : bytes_(bytes), pos_(pos) {}

    constexpr operator bool() const noexcept {
      if (pos_ / 8 >= bytes_.size())
        return false; // release safety
      return ((std::to_integer<unsigned>(bytes_[pos_ / 8]) >> (pos_ % 8)) &
              1u) != 0u;
    }
    constexpr reference &operator=(bool value) noexcept {
      if (pos_ / 8 >= bytes_.size())
        return *this; // release safety
      const unsigned byte = std::to_integer<unsigned>(bytes_[pos_ / 8]);
      const unsigned mask = 1u << (pos_ % 8);
      bytes_[pos_ / 8] =
          static_cast<std::byte>(value ? byte | mask : byte & ~mask);
      return *this;
    }
    constexpr reference &operator=(reference const &other) noexcept {
      return *this = static_cast<bool>(other);
    }
    constexpr void flip() noexcept {
      if (pos_ / 8 >= bytes_.size())
        return; // release safety
      const unsigned byte = std::to_integer<unsigned>(bytes_[pos_ / 8]);
      bytes_[pos_ / 8] = static_cast<std::byte>(byte ^ (1u << (pos_ % 8)));
    }

  private:
    std::span<byte_type> bytes_;
    std::size_t pos_ = 0;
  };

  // bool for a const span, an assignable proxy for a mutable one.
  using reference_type = std::conditional_t<Const, bool, reference>;

  constexpr reference_type operator[](std::size_t i) const noexcept {
    assert(i < size_);
    if constexpr (Const)
      return get_bit(i);
    else
      return reference(bytes_, offset_ + i);
  }

  class iterator {
  public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = bool;
    using difference_type = std::ptrdiff_t;

    using reference = typename basic_bit_span::reference_type;
    using pointer = void;

    constexpr iterator() noexcept = default;
    constexpr iterator(basic_bit_span span, std::size_t index) noexcept
        : span_(span), index_(index) {}

    constexpr reference operator*() const noexcept { return span_[index_]; }
    constexpr iterator &operator++() noexcept {
      ++index_;
      return *this;
    }
    constexpr iterator operator++(int) noexcept {
      iterator copy = *this;
      ++index_;
      return copy;
    }
    friend constexpr bool operator==(iterator const &a,
                                     iterator const &b) noexcept {
      return a.index_ == b.index_;
    }
    friend constexpr bool operator!=(iterator const &a,
                                     iterator const &b) noexcept {
      return !(a == b);
    }

  private:
    basic_bit_span span_;
    std::size_t index_ = 0;
  };

  constexpr iterator begin() const noexcept { return iterator(*this, 0); }
  constexpr iterator end() const noexcept { return iterator(*this, size_); }

  // --- text ------------------------------------------------------------------

  // The section's bits, most significant first; `group` adds a space every 4.
  std::string to_binary(bool group = false) const {
    std::string out;
    out.reserve(size_ + (group ? size_ / 4 : 0));
    // Emit whole nibbles from the top, 64 bits (16 nibbles) per read; only a
    // leading partial nibble needs the per-bit path.
    std::size_t top = size_;
    if (const unsigned lead = static_cast<unsigned>(size_ % 4); lead != 0) {
      const std::uint64_t word = read_at(size_ - lead, lead);
      for (unsigned b = lead; b-- > 0;)
        out.push_back(((word >> b) & 1u) != 0u ? '1' : '0');
      top -= lead;
      if (group && top > 0)
        out.push_back(' ');
    }
    while (top > 0) {
      const std::size_t batch = std::min<std::size_t>(16, top / 4);
      const std::size_t at = top - batch * 4;
      const std::uint64_t word =
          read_at(at, static_cast<unsigned>(batch * 4));
      for (unsigned i = static_cast<unsigned>(batch); i-- > 0;) {
        out.append(detail::binary_digits.chars +
                       static_cast<std::size_t>((word >> (i * 4)) & 0xFu) * 4,
                   4);
        if (group && (i > 0 || at > 0))
          out.push_back(' ');
      }
      top = at;
    }
    return out;
  }

private:
  // Keep the view inside its storage whatever the arguments were: release
  // builds clamp instead of reading or writing out of bounds.
  constexpr void clamp_to_storage() noexcept {
    const std::size_t total = bytes_.size() * 8;
    if (offset_ > total)
      offset_ = total;
    if (size_ > total - offset_)
      size_ = total - offset_;
  }

  constexpr std::byte const *first_byte() const noexcept {
    return bytes_.data() + offset_ / 8;
  }

  constexpr std::byte const *last_byte() const noexcept {
    return bytes_.data() + (offset_ + size_ - 1) / 8;
  }

  // Whether this view starts before `other` in memory. Compares bit positions,
  // not just bytes, so ranges starting inside the same byte order correctly.
  // std::less gives a total order even for pointers into unrelated objects.
  constexpr bool starts_before(basic_bit_span<true> other) const noexcept {
    std::byte const *a = first_byte();
    std::byte const *b = other.storage().data() + other.offset() / 8;
    if (std::less<std::byte const *>{}(a, b))
      return true;
    if (std::less<std::byte const *>{}(b, a))
      return false;
    return (offset_ % 8) < (other.offset() % 8);
  }

  // Whether the two views share storage and overlap. std::less gives a total
  // order even for pointers into unrelated objects.
  constexpr bool overlaps(basic_bit_span<true> other) const noexcept {
    if (size_ == 0 || other.size() == 0)
      return false;
    std::byte const *a0 = first_byte();
    std::byte const *a1 = last_byte();
    std::byte const *b0 = other.storage().data() + other.offset() / 8;
    std::byte const *b1 =
        other.storage().data() + (other.offset() + other.size() - 1) / 8;
    return !std::less<const std::byte *>{}(a1, b0) &&
           !std::less<const std::byte *>{}(b1, a0);
  }

  std::span<byte_type> bytes_;
  std::size_t offset_ = 0;
  std::size_t size_ = 0;
};

using bit_span = basic_bit_span<false>;
using const_bit_span = basic_bit_span<true>;

// Bit-wise equality (sizes must match). A free template rather than a member so
// both operands can be mutable, const, or one of each.
template <bool C1, bool C2>
constexpr bool operator==(basic_bit_span<C1> a, basic_bit_span<C2> b) noexcept {
  return a.equal(b);
}

// The bits of a section, most significant first (see basic_bit_span::to_binary).
inline std::string to_binary(const_bit_span span, bool group = false) {
  return span.to_binary(group);
}

// --- compile-time fields -----------------------------------------------------

// A named, typed section of an integral: `bit_field<11, 15>(rgb)` reads and
// writes bits [11, 15] like a value. Because the range is part of the type,
// the masks are constants and the codegen is the same as hand-written shifts.
template <class T, std::size_t Lo, std::size_t Hi> class bit_field_ref {
  static_assert(Lo <= Hi, "bit_field: empty range");

public:
  static constexpr std::size_t lo = Lo;
  static constexpr std::size_t hi = Hi;
  static constexpr std::size_t width = Hi - Lo + 1;

  constexpr explicit bit_field_ref(T &ref) noexcept : ref_(&ref) {}
  constexpr bit_field_ref(bit_field_ref const &) noexcept = default;

  constexpr T get() const noexcept { return extract_bits(*ref_, Lo, Hi); }
  constexpr void set(T value) const noexcept {
    *ref_ = insert_bits(*ref_, Lo, Hi, value);
  }
  constexpr operator T() const noexcept { return get(); }
  constexpr bit_field_ref &operator=(T value) noexcept {
    set(value);
    return *this;
  }
  constexpr bit_field_ref &operator=(bit_field_ref const &other) noexcept {
    set(other.get());
    return *this;
  }

private:
  T *ref_;
};

template <std::size_t Lo, std::size_t Hi, bit_integral T>
  requires(Hi < std::numeric_limits<std::make_unsigned_t<T>>::digits)
constexpr bit_field_ref<T, Lo, Hi> bit_field(T &x) noexcept {
  return bit_field_ref<T, Lo, Hi>(x);
}

// Split an integral into consecutive sections, given their widths from bit 0
// up: `auto [b, g, r] = bit_split<5, 6, 5>(rgb);`.
template <std::size_t I, std::size_t... W>
consteval std::size_t split_offset() noexcept {
  constexpr std::size_t widths[] = {W...};
  std::size_t sum = 0;
  for (std::size_t i = 0; i < I; ++i)
    sum += widths[i];
  return sum;
}

template <std::size_t I, std::size_t... W>
consteval std::size_t split_width() noexcept {
  constexpr std::size_t widths[] = {W...};
  return widths[I];
}

template <class T, std::size_t I, std::size_t... W>
using split_field_t =
    bit_field_ref<T, split_offset<I, W...>(),
                  split_offset<I, W...>() + split_width<I, W...>() - 1>;

template <std::size_t... Widths, bit_integral T>
  requires(sizeof...(Widths) > 0 && ((Widths > 0) && ...) &&
           (Widths + ...) <=
               std::numeric_limits<std::make_unsigned_t<T>>::digits)
constexpr auto bit_split(T &x) noexcept {
  return [&x]<std::size_t... I>(std::index_sequence<I...>) {
    return std::tuple<split_field_t<T, I, Widths...>...>{
        split_field_t<T, I, Widths...>(x)...};
  }(std::make_index_sequence<sizeof...(Widths)>{});
}

// --- packed bit streams ------------------------------------------------------

// Packing order for the bit streams. LSB-first puts the low bit of a field in
// the first bit of the stream (zlib/DEFLATE values); MSB-first puts the high
// bit first (H.264, JPEG, FLAC). Both are zero-cost: the order is part of the
// type, and a single call can override it with write<Order>(...) / read<Order>(...).
enum class BitOrder { LsbFirst, MsbFirst };

// Appends bits to a byte buffer, least significant bit first by default. Whole
// bytes are appended in bulk, so a 64-bit write costs a handful of operations.
template <BitOrder Order = BitOrder::LsbFirst> class basic_bit_writer {
public:
  constexpr void reserve(std::size_t bytes) { bytes_.reserve(bytes); }

  // Append the low `bits` bits of `value`. The stream's type fixes how bytes
  // fill (LsbFirst = bottom up, MsbFirst = top down); the optional template
  // argument overrides the *field* order, which is how formats like DEFLATE
  // mix both in one stream.
  template <BitOrder FieldOrder = Order>
  constexpr void write(std::uint64_t value, unsigned bits) {
    assert(bits <= 64);
    if (bits == 0)
      return;
    if (bits < 64)
      value &= (std::uint64_t{1} << bits) - 1;
    // Emitting a field in the opposite order to the byte filling is a
    // within-field reversal.
    if constexpr (FieldOrder != Order)
      value = static_cast<std::uint64_t>(bit_reverse(value, bits));

    if constexpr (Order == BitOrder::LsbFirst) {
      if (const unsigned pos = static_cast<unsigned>(bits_ % 8); pos != 0) {
        const unsigned take = std::min(bits, 8u - pos);
        bytes_.back() |= static_cast<std::byte>(
            (value & ((std::uint64_t{1} << take) - 1)) << pos);
        value >>= take;
        bits -= take;
        bits_ += take;
      }
      while (bits >= 8) {
        bytes_.push_back(static_cast<std::byte>(value & 0xFF));
        value >>= 8;
        bits -= 8;
        bits_ += 8;
      }
      if (bits > 0) {
        bytes_.push_back(
            static_cast<std::byte>(value & ((std::uint64_t{1} << bits) - 1)));
        bits_ += bits;
      }
    } else {
      // MSB-first: the top bit of the field goes out first, filling each byte
      // from the top down. Full bytes are appended in bulk.
      unsigned remaining = bits;
      if (const unsigned used = static_cast<unsigned>(bits_ % 8); used != 0) {
        const unsigned take = std::min(remaining, 8u - used);
        const std::uint64_t chunk =
            (value >> (remaining - take)) & ((std::uint64_t{1} << take) - 1);
        bytes_.back() |= static_cast<std::byte>(chunk << (8 - used - take));
        remaining -= take;
        bits_ += take;
      }
      while (remaining >= 8) {
        bytes_.push_back(
            static_cast<std::byte>((value >> (remaining - 8)) & 0xFF));
        remaining -= 8;
        bits_ += 8;
      }
      if (remaining > 0) {
        bytes_.push_back(static_cast<std::byte>(
            (value & ((std::uint64_t{1} << remaining) - 1))
            << (8 - remaining)));
        bits_ += remaining;
      }
    }
  }

  constexpr void write_bit(bool value) { write(value ? 1u : 0u, 1); }

  // Pad to the next byte boundary with `pad` bits.
  constexpr void align_to_byte(bool pad = false) {
    while (bits_ % 8 != 0)
      write_bit(pad);
  }

  // Append every bit of a section, in stream order.
  constexpr void write(const_bit_span src) {
    std::size_t done = 0;
    while (done < src.size()) {
      const unsigned chunk =
          static_cast<unsigned>(std::min<std::size_t>(64, src.size() - done));
      write(src.read_at(done, chunk), chunk);
      done += chunk;
    }
  }

  constexpr std::size_t bit_count() const noexcept { return bits_; }

  constexpr std::size_t byte_count() const noexcept { return bytes_.size(); }

  constexpr std::span<std::byte const> data() const noexcept { return bytes_; }

  // Hand over the packed bytes (the writer is left empty).
  constexpr std::vector<std::byte> release() {
    bits_ = 0;
    return std::move(bytes_);
  }

  constexpr void clear() noexcept {
    bytes_.clear();
    bits_ = 0;
  }

private:
  std::vector<std::byte> bytes_;
  std::size_t bits_ = 0;
};

using BitWriter = basic_bit_writer<BitOrder::LsbFirst>;
using MsbWriter = basic_bit_writer<BitOrder::MsbFirst>;

// Reads bits from a byte buffer, least significant bit first by default. Can be
// bounded to a section (`BitReader{some_bit_span}`) so it stops at its end.
template <BitOrder Order = BitOrder::LsbFirst> class basic_bit_reader {
public:
  constexpr basic_bit_reader() noexcept = default;

  constexpr explicit basic_bit_reader(std::span<std::byte const> data) noexcept
      : bits_(data), pos_(0) {}

  constexpr explicit basic_bit_reader(const_bit_span span) noexcept
      : bits_(span), pos_(span.offset()) {}

  // Read `bits` bits (at most 64 per call), in this stream's order (or `O`).
  template <BitOrder FieldOrder = Order>
  [[nodiscard]] Result<std::uint64_t> read(unsigned bits) {
    if (bits > 64)
      return err<std::uint64_t>("BitReader::read: more than 64 bits");
    if (remaining() < bits)
      return err<std::uint64_t>("BitReader::read: out of bits");
    std::uint64_t value = read_raw(bits);
    if constexpr (FieldOrder != Order)
      value = static_cast<std::uint64_t>(bit_reverse(value, bits));
    pos_ += bits;
    return ok(value);
  }

  template <BitOrder FieldOrder = Order>
  [[nodiscard]] Result<std::uint64_t> peek(unsigned bits) {
    const std::size_t saved = pos_;
    auto result = read<FieldOrder>(bits);
    pos_ = saved;
    return result;
  }

  [[nodiscard]] Result<bool> read_bit() {
    if (remaining() == 0)
      return err<bool>("BitReader::read_bit: out of bits");
    const bool value = read_raw(1) != 0;
    ++pos_;
    return ok(value);
  }

  // A read-only view of the next `bits` bits; does not consume them.
  [[nodiscard]] Result<const_bit_span> peek_bits(unsigned bits) {
    if (bits == 0)
      return ok(const_bit_span{});
    if (remaining() < bits)
      return err<const_bit_span>("BitReader::peek_bits: out of bits");
    const std::size_t begin = pos_ - bits_.offset();
    return ok(bits_.section(begin, begin + bits - 1));
  }

  // Fill `dst` with the next dst.size() bits (a bit copy in stream order) and
  // advance past them.
  [[nodiscard]] Result<void> read_into(bit_span dst) {
    if (remaining() < dst.size())
      return err<void>("BitReader::read_into: out of bits");
    std::size_t done = 0;
    while (done < dst.size()) {
      const unsigned chunk = static_cast<unsigned>(
          std::min<std::size_t>(64, dst.size() - done));
      auto value = read_raw_checked(chunk);
      if (!value.is_ok())
        return err<void>(value.error());
      // read_raw gives the stream's bit sequence with the first bit in bit 0,
      // which is exactly the destination's layout.
      dst.write_at(done, value.value(), chunk);
      done += chunk;
    }
    return ok<void>();
  }

  [[nodiscard]] Result<void> skip(unsigned bits) {
    if (remaining() < bits)
      return err<void>("BitReader::skip: out of bits");
    pos_ += bits;
    return ok<void>();
  }

  // Skip to the next byte boundary. Fails if the padding is not there.
  [[nodiscard]] Result<void> align_to_byte() {
    const unsigned pad = static_cast<unsigned>((8 - pos_ % 8) % 8);
    if (remaining() < pad)
      return err<void>("BitReader::align_to_byte: out of bits");
    pos_ += pad;
    return ok<void>();
  }

  constexpr std::size_t remaining() const noexcept {
    return bits_.offset() + bits_.size() - pos_;
  }

  constexpr bool at_end() const noexcept { return remaining() == 0; }

  // Position within the view (0 for a reader over raw bytes).
  constexpr std::size_t position() const noexcept {
    return pos_ - bits_.offset();
  }

  constexpr void reset() noexcept { pos_ = bits_.offset(); }

private:
  // The stream's bits as a value: first bit read lands in bit 0.
  constexpr std::uint64_t read_raw(unsigned bits) const noexcept {
    if constexpr (Order == BitOrder::LsbFirst)
      return bits_.read_at(pos_ - bits_.offset(), bits);
    else
      return read_msb(bits);
  }

  [[nodiscard]] Result<std::uint64_t> read_raw_checked(unsigned bits) {
    if (remaining() < bits)
      return err<std::uint64_t>("BitReader: out of bits");
    const std::uint64_t value = read_raw(bits);
    pos_ += bits;
    return ok(value);
  }

  constexpr std::uint64_t read_msb(unsigned bits) const noexcept {
    std::uint64_t out = 0;
    std::size_t pos = pos_;
    unsigned done = 0;
    const auto storage = bits_.storage();
    while (done < bits) {
      const unsigned off = static_cast<unsigned>(pos % 8);
      const unsigned take = std::min(bits - done, 8u - off);
      const std::uint64_t byte = std::to_integer<std::uint64_t>(storage[pos / 8]);
      const std::uint64_t chunk =
          (byte >> (8 - off - take)) & ((std::uint64_t{1} << take) - 1);
      out = (out << take) | chunk;
      done += take;
      pos += take;
    }
    return out;
  }

  const_bit_span bits_;
  std::size_t pos_ = 0;
};

using BitReader = basic_bit_reader<BitOrder::LsbFirst>;
using MsbReader = basic_bit_reader<BitOrder::MsbFirst>;

} // namespace fp

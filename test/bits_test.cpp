#include <fp/bits.hpp>
#include <fp/string.hpp>

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstring>
#include <random>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

TEST(Bits, SingleBits) {
  EXPECT_FALSE(fp::bit(std::uint8_t{0b0000'0001}, 1));
  EXPECT_TRUE(fp::bit(std::uint8_t{0b0000'0010}, 1));
  EXPECT_EQ(fp::set_bit(std::uint8_t{0}, 7), 0b1000'0000);
  EXPECT_EQ(fp::set_bit(std::uint8_t{0b1111'1111}, 3, false), 0b1111'0111);
  EXPECT_EQ(fp::clear_bit(std::uint8_t{0b0000'0100}, 2), 0);
  EXPECT_EQ(fp::toggle_bit(std::uint8_t{0b1010}, 0), 0b1011);
  EXPECT_EQ(fp::toggle_bit(std::uint16_t{0xFFFF}, 15), 0x7FFF);

  // signed values keep their type
  EXPECT_EQ(fp::set_bit(-1, 31), -1);
  EXPECT_EQ(fp::clear_bit(-1, 0), -2);
  EXPECT_TRUE(fp::bit(std::int8_t{-128}, 7));
}

TEST(Bits, SingleBitsConstexpr) {
  static_assert(fp::bit(std::uint32_t{1} << 31, 31));
  static_assert(fp::set_bit(std::uint32_t{0}, 4) == 16);
  static_assert(fp::clear_bit(std::uint32_t{0xFFFFFFFF}, 0) == 0xFFFFFFFE);
  static_assert(fp::toggle_bit(std::uint32_t{0}, 0) == 1);
}

TEST(Bits, Ranges) {
  EXPECT_EQ(fp::low_mask<std::uint8_t>(0), 0);
  EXPECT_EQ(fp::low_mask<std::uint8_t>(3), 0b111);
  EXPECT_EQ(fp::low_mask<std::uint8_t>(8), 0xFF);
  EXPECT_EQ(fp::low_mask<std::uint16_t>(16), 0xFFFF);

  EXPECT_EQ(fp::bit_mask<std::uint8_t>(2, 5), 0b0011'1100);
  EXPECT_EQ(fp::bit_mask<std::uint32_t>(0, 31), 0xFFFFFFFFu);
  EXPECT_EQ(fp::bit_mask<std::uint32_t>(30, 31), 0xC0000000u);

  EXPECT_EQ(fp::extract_bits(std::uint8_t{0b1011'0100}, 2, 5), 0b1101);
  EXPECT_EQ(fp::extract_bits(std::uint8_t{0xFF}, 0, 7), 0xFF);
  EXPECT_EQ(fp::extract_bits(std::uint32_t{0xDEADBEEF}, 16, 31), 0xDEADu);
  EXPECT_EQ(fp::extract_bits(std::uint32_t{0xDEADBEEF}, 0, 15), 0xBEEFu);

  EXPECT_EQ(fp::insert_bits(std::uint8_t{0}, 2, 5, std::uint8_t{0b1101}),
            0b0011'0100);
  EXPECT_EQ(fp::insert_bits(std::uint8_t{0xFF}, 0, 3, std::uint8_t{0b0000}),
            0xF0);
  // value bits above the range are dropped
  EXPECT_EQ(fp::insert_bits(std::uint16_t{0}, 4, 7, std::uint16_t{0xFF}),
            0x00F0);
  // round trip
  const auto packed =
      fp::insert_bits(std::uint32_t{0}, 8, 19, std::uint32_t{0xABC});
  EXPECT_EQ(fp::extract_bits(packed, 8, 19), 0xABCu);
}

TEST(Bits, RangesConstexpr) {
  static_assert(fp::bit_mask<std::uint8_t>(2, 5) == 0b0011'1100);
  static_assert(fp::extract_bits(std::uint16_t{0xF0F0}, 4, 11) == 0x0F);
  static_assert(fp::insert_bits(std::uint16_t{0}, 8, 15, std::uint16_t{0xAB}) ==
                0xAB00);
}

TEST(Bits, Counting) {
  EXPECT_EQ(fp::popcount(std::uint32_t{0}), 0);
  EXPECT_EQ(fp::popcount(std::uint32_t{0xF0F0}), 8);
  EXPECT_EQ(fp::count_zeros(std::uint8_t{0x0F}), 4);
  EXPECT_EQ(fp::leading_zeros(std::uint8_t{1}), 7);
  EXPECT_EQ(fp::trailing_zeros(std::uint8_t{8}), 3);
  EXPECT_EQ(fp::leading_zeros(std::uint32_t{0}), 32);
  EXPECT_EQ(fp::bit_width(std::uint32_t{0}), 0);
  EXPECT_EQ(fp::bit_width(std::uint32_t{255}), 8);
  EXPECT_EQ(fp::bit_width(std::uint32_t{256}), 9);
  EXPECT_TRUE(fp::has_single_bit(std::uint32_t{1024}));
  EXPECT_FALSE(fp::has_single_bit(std::uint32_t{1023}));
}

TEST(Bits, CountingConstexpr) {
  static_assert(fp::popcount(std::uint64_t{0xFFFF'FFFF'FFFF'FFFF}) == 64);
  static_assert(fp::leading_zeros(std::uint64_t{1}) == 63);
  static_assert(fp::trailing_zeros(std::uint64_t{1} << 40) == 40);
  static_assert(fp::bit_width(std::uint8_t{1}) == 1);
  static_assert(fp::count_zeros(std::uint8_t{0}) == 8);
}

TEST(Bits, Rotate) {
  EXPECT_EQ(fp::rotl(std::uint8_t{0x81}, 1), 0x03);
  EXPECT_EQ(fp::rotr(std::uint8_t{0x81}, 1), 0xC0);
  EXPECT_EQ(fp::rotl(std::uint8_t{0x81}, 9), 0x03);
  EXPECT_EQ(fp::rotl(std::uint8_t{0x81}, -1), 0xC0);
  EXPECT_EQ(fp::rotr(std::uint8_t{0x81}, -1), 0x03);
  EXPECT_EQ(fp::rotl(std::uint32_t{0x12345678}, 0), 0x12345678u);
  EXPECT_EQ(fp::rotl(std::uint32_t{0x12345678}, 32), 0x12345678u);
}

TEST(Bits, RotateConstexpr) {
  static_assert(fp::rotl(std::uint16_t{0x8001}, 1) == 0x0003);
  static_assert(fp::rotr(std::uint16_t{0x0003}, 1) == 0x8001);
}

TEST(Bits, ByteSwap) {
  EXPECT_EQ(fp::byteswap(std::uint16_t{0x1122}), 0x2211);
  EXPECT_EQ(fp::byteswap(std::uint32_t{0x11223344}), 0x44332211u);
  EXPECT_EQ(fp::byteswap(std::uint64_t{0x1122334455667788}),
            0x8877665544332211ull);
  EXPECT_EQ(fp::byteswap(std::uint8_t{0xAB}), 0xAB);
  EXPECT_EQ(fp::byteswap(fp::byteswap(std::uint32_t{0xDEADBEEF})), 0xDEADBEEFu);
  EXPECT_FLOAT_EQ(fp::byteswap(fp::byteswap(1.5f)), 1.5f);
}

TEST(Bits, ByteSwapConstexpr) {
  static_assert(fp::byteswap(std::uint32_t{0x11223344}) == 0x44332211u);
  static_assert(fp::byteswap(std::uint16_t{0x0102}) == 0x0201);
}

TEST(Bits, ObjectRepresentation) {
  const std::uint32_t x = 0x11223344;
  const auto bytes = fp::bytes_of(x);
  ASSERT_EQ(bytes.size(), sizeof(x));
  if constexpr (std::endian::native == std::endian::little) {
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0x44);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x11);
  } else {
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0x11);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x44);
  }

  // bit_cast round trip
  EXPECT_EQ(std::bit_cast<std::uint32_t>(bytes), x);

  // a mutable byte view edits the value in place
  std::uint32_t y = 0;
  fp::as_bytes(y)[0] = std::byte{0xAB};
  if constexpr (std::endian::native == std::endian::little)
    EXPECT_EQ(y, 0xABu);
  else
    EXPECT_EQ(y, 0xAB000000u);

  // const view
  const std::uint32_t z = 7;
  EXPECT_EQ(fp::as_bytes(z).size(), sizeof(z));
}

TEST(Bits, FloatingPointBits) {
  float f = 1.0f;
  EXPECT_FALSE(fp::sign_bit(f));
  fp::as_bytes(f)[sizeof(float) - 1] |= std::byte{0x80};
  EXPECT_TRUE(fp::sign_bit(f));
  EXPECT_FLOAT_EQ(f, -1.0f);

  // 1.0f is 0x3F800000 in IEEE-754 bit order
  EXPECT_EQ(fp::word_at<std::uint32_t>(1.0f), 0x3F800000u);
}

TEST(Bits, WordAt) {
  const std::uint64_t value = 0x1122334455667788;
  const auto low = fp::word_at<std::uint32_t>(value);
  const auto high = fp::word_at<std::uint32_t>(value, 4);
  if constexpr (std::endian::native == std::endian::little) {
    EXPECT_EQ(low, 0x55667788u);
    EXPECT_EQ(high, 0x11223344u);
  } else {
    EXPECT_EQ(low, 0x11223344u);
    EXPECT_EQ(high, 0x55667788u);
  }
  // bytes past the end read as zero
  EXPECT_EQ(fp::word_at<std::uint64_t>(std::uint8_t{0xAB}),
            0xABull << (std::endian::native == std::endian::little ? 0 : 56));
}

TEST(Bits, WholeValueOps) {
  EXPECT_EQ(fp::bit_not(std::uint8_t{0x0F}), 0xF0);
  EXPECT_EQ(fp::bit_and(std::uint8_t{0x0F}, std::uint8_t{0x33}), 0x03);
  EXPECT_EQ(fp::bit_or(std::uint8_t{0x0F}, std::uint8_t{0x30}), 0x3F);
  EXPECT_EQ(fp::bit_xor(std::uint8_t{0xFF}, std::uint8_t{0x0F}), 0xF0);
  EXPECT_EQ(fp::bit_and(std::uint32_t{0xFFFF0000}, std::uint32_t{0x0F0F0F0F}),
            0x0F0F0000u);

  // works on floats and structs through the object representation
  EXPECT_FLOAT_EQ(fp::bit_not(fp::bit_not(2.5f)), 2.5f);
  struct Pair {
    std::uint16_t a;
    std::uint16_t b;
  };
  const Pair p{0x00FF, 0xFF00};
  const Pair q{0x0F0F, 0x0F0F};
  const Pair r = fp::bit_and(p, q);
  EXPECT_EQ(r.a, 0x000F);
  EXPECT_EQ(r.b, 0x0F00);
}

TEST(Bits, WholeValueOpsConstexpr) {
  static_assert(fp::bit_not(std::uint8_t{0x0F}) == 0xF0);
  static_assert(fp::bit_and(std::uint16_t{0xFF00}, std::uint16_t{0x0FF0}) ==
                0x0F00);
  static_assert(fp::bit_or(std::uint8_t{0x0F}, std::uint8_t{0xF0}) == 0xFF);
  static_assert(fp::bit_xor(std::uint8_t{0xFF}, std::uint8_t{0x0F}) == 0xF0);
}

TEST(Bits, SignBit) {
  EXPECT_TRUE(fp::sign_bit(-1));
  EXPECT_FALSE(fp::sign_bit(1u));
  EXPECT_TRUE(fp::sign_bit(-0.0));
  EXPECT_FALSE(fp::sign_bit(0.0));
}

TEST(Bits, ToBinary) {
  EXPECT_EQ(fp::to_binary(std::uint8_t{5}), "00000101");
  EXPECT_EQ(fp::to_binary(std::uint8_t{5}, true), "0000 0101");
  EXPECT_EQ(fp::to_binary(std::uint8_t{0xA5}, true), "1010 0101");
  EXPECT_EQ(fp::to_binary(std::uint8_t{0}), "00000000");
  EXPECT_EQ(fp::to_binary(std::uint16_t{1}).size(), 16u);
}

TEST(Bits, BitWriterReaderRoundTrip) {
  fp::BitWriter writer;
  writer.write(0b101, 3);
  writer.write(0xABCD, 16);
  writer.write_bit(true);
  writer.write_bit(false);
  writer.write(0x7F, 7);
  ASSERT_EQ(writer.bit_count(), 3u + 16u + 1u + 1u + 7u);
  ASSERT_EQ(writer.byte_count(), 4u); // 28 bits -> 4 bytes

  fp::BitReader reader(writer.data());
  auto a = reader.read(3);
  auto b = reader.read(16);
  auto c = reader.read_bit();
  auto d = reader.read_bit();
  auto e = reader.read(7);
  ASSERT_TRUE(a.is_ok());
  ASSERT_TRUE(b.is_ok());
  ASSERT_TRUE(c.is_ok());
  ASSERT_TRUE(d.is_ok());
  ASSERT_TRUE(e.is_ok());
  EXPECT_EQ(a.value(), 0b101u);
  EXPECT_EQ(b.value(), 0xABCDu);
  EXPECT_TRUE(c.value());
  EXPECT_FALSE(d.value());
  EXPECT_EQ(e.value(), 0x7Fu);
  EXPECT_EQ(reader.position(), writer.bit_count());
  EXPECT_EQ(reader.remaining(), 4u); // the last byte is only half used
  EXPECT_FALSE(reader.at_end());
}

TEST(Bits, BitWriterReuse) {
  fp::BitWriter writer;
  writer.write(0xFF, 8);
  EXPECT_EQ(writer.byte_count(), 1u);
  EXPECT_EQ(writer.bit_count(), 8u); // a full byte still counts as 8 bits
  writer.clear();
  EXPECT_EQ(writer.bit_count(), 0u);
  writer.write(0x1, 1);
  EXPECT_EQ(writer.byte_count(), 1u);
  EXPECT_EQ(std::to_integer<unsigned>(writer.data()[0]), 1u);
}

TEST(Bits, BitWriterByteBoundaries) {
  fp::BitWriter writer;
  writer.write(0b1010'0101, 8);
  writer.write(0b0000'1111, 8);
  ASSERT_EQ(writer.bit_count(), 16u);
  ASSERT_EQ(writer.byte_count(), 2u);
  EXPECT_EQ(std::to_integer<unsigned>(writer.data()[0]), 0b1010'0101u);
  EXPECT_EQ(std::to_integer<unsigned>(writer.data()[1]), 0b0000'1111u);

  fp::BitReader reader(writer.data());
  EXPECT_EQ(reader.read(8).value(), 0b1010'0101u);
  EXPECT_EQ(reader.read(8).value(), 0b0000'1111u);
  EXPECT_TRUE(reader.at_end());

  // a zero-length write is a no-op
  writer.write(0, 0);
  EXPECT_EQ(writer.bit_count(), 16u);
}

TEST(Bits, MixedWidthInsert) {
  // Regression: the value used to be shifted in its own type, so inserting a
  // narrow value at a high bit position was undefined (UBSan: shift too large).
  std::uint64_t x = 0;
  x = fp::insert_bits(x, 60, 63, std::uint8_t{0xF});
  EXPECT_EQ(x, 0xF000000000000000ull);
  x = fp::insert_bits(x, 0, 7, std::uint8_t{0xAB});
  EXPECT_EQ(fp::extract_bits(x, 0, 7), 0xAB);

  // and the wide-value / narrow-target direction: the value's *low* bits are
  // what lands in the range
  std::uint8_t y = 0;
  y = fp::insert_bits(y, 4, 7, std::uint64_t{0xF5});
  EXPECT_EQ(y, 0x50);
}

TEST(Bits, RotateEdgeCounts) {
  // Regression: rotr used to negate n, which overflows for INT_MIN.
  const std::uint32_t x = 0x80000001;
  EXPECT_EQ(fp::rotr(x, INT_MIN), x);      // INT_MIN % 32 == 0
  EXPECT_EQ(fp::rotl(x, INT_MIN), x);
  EXPECT_EQ(fp::rotr(x, INT_MIN + 1), fp::rotr(x, 1));
  EXPECT_EQ(fp::rotl(x, INT_MIN + 1), fp::rotl(x, 1));
  EXPECT_EQ(fp::rotr(x, 0), x);
  EXPECT_EQ(fp::rotr(x, 32), x);
  EXPECT_EQ(fp::rotr(x, 33), fp::rotr(x, 1));
  EXPECT_EQ(fp::rotl(x, -33), fp::rotr(x, 1));
  EXPECT_EQ(fp::rotr(std::uint8_t{0x01}, -1), 0x02);
}

namespace {
template <class T> concept can_bit_not = requires(T x) { fp::bit_not(x); };
template <class T, class U>
concept can_bit_and = requires(T x, U y) { fp::bit_and(x, y); };
template <class T, std::size_t... W>
concept can_bit_split = requires(T x) { fp::bit_split<W...>(x); };
} // namespace

TEST(Bits, WholeValueOpsRejectBool) {
  // bool is excluded: bitwise ops could leave a non-0/1 representation.
  static_assert(!can_bit_not<bool>);
  static_assert(can_bit_not<int>);
  static_assert(!can_bit_and<bool, bool>);
  static_assert(can_bit_and<int, unsigned>);
  // the object representation itself is still available
  EXPECT_TRUE(fp::byteswap(true));
  EXPECT_EQ(fp::bytes_of(true).size(), 1u);
}

TEST(Bits, ByteOrder) {
  const std::uint32_t x = 0x11223344;
  if constexpr (std::endian::native == std::endian::little) {
    EXPECT_EQ(fp::to_little_endian(x), x);
    EXPECT_EQ(fp::to_big_endian(x), 0x44332211u);
  } else {
    EXPECT_EQ(fp::to_big_endian(x), x);
    EXPECT_EQ(fp::to_little_endian(x), 0x44332211u);
  }
  EXPECT_EQ(fp::from_little_endian(fp::to_little_endian(x)), x);
  EXPECT_EQ(fp::from_big_endian(fp::to_big_endian(x)), x);
  static_assert(fp::to_big_endian(std::uint16_t{0x0102}) ==
                (std::endian::native == std::endian::big ? 0x0102 : 0x0201));
  EXPECT_FLOAT_EQ(fp::from_big_endian(fp::to_big_endian(1.5f)), 1.5f);
}

TEST(Bits, WordAtConstexpr) {
  static_assert(fp::word_at<std::uint8_t>(std::uint16_t{0x1234}) ==
                (std::endian::native == std::endian::little ? 0x34 : 0x12));
  static_assert(fp::word_at<std::uint16_t>(std::uint16_t{0x1234}) == 0x1234);
  static_assert(fp::word_at<std::uint32_t>(std::uint8_t{0xAB}) ==
                (std::endian::native == std::endian::little ? 0xABu
                                                            : 0xAB000000u));
}

TEST(Bits, PeekAndReserve) {
  fp::BitWriter writer;
  writer.reserve(16);
  writer.write(0b1011, 4);
  writer.write(0x3F, 6);

  fp::BitReader reader(writer.data());
  auto p = reader.peek(4);
  ASSERT_TRUE(p.is_ok());
  EXPECT_EQ(p.value(), 0b1011u);
  EXPECT_EQ(reader.position(), 0u); // peek does not consume
  EXPECT_EQ(reader.read(4).value(), 0b1011u);
  EXPECT_EQ(reader.read(6).value(), 0x3Fu);
  // 10 bits used, the buffer is padded to two bytes
  EXPECT_EQ(reader.remaining(), 6u);
  auto pad = reader.peek(1);
  ASSERT_TRUE(pad.is_ok());
  EXPECT_EQ(pad.value(), 0u);
}

TEST(Bits, StreamRoundTripFuzz) {
  // write/read must round-trip every (value, width) pair across byte
  // boundaries, including 64-bit and 0-bit writes.
  std::mt19937_64 rng(12345);
  fp::BitWriter writer;
  std::vector<std::pair<std::uint64_t, unsigned>> written;
  for (int i = 0; i < 2000; ++i) {
    const unsigned bits = static_cast<unsigned>(rng() % 65);
    std::uint64_t value = rng();
    if (bits < 64)
      value &= (std::uint64_t{1} << bits) - 1;
    writer.write(value, bits);
    written.emplace_back(value, bits);
  }

  fp::BitReader reader(writer.data());
  for (auto const &[value, bits] : written) {
    auto got = reader.read(bits);
    ASSERT_TRUE(got.is_ok()) << "width " << bits;
    EXPECT_EQ(got.value(), value) << "width " << bits;
  }
  EXPECT_LT(reader.remaining(), 8u); // only the final byte's padding is left
}

TEST(Bits, BitWriterConstexpr) {
  static_assert([] {
    fp::BitWriter writer;
    writer.write(0b101, 3);
    writer.write(0xFF, 8);
    if (writer.bit_count() != 11 || writer.byte_count() != 2)
      return false;
    if (std::to_integer<unsigned>(writer.data()[0]) != 0b1111'1101)
      return false;
    if (std::to_integer<unsigned>(writer.data()[1]) != 0b0000'0111)
      return false;
    writer.clear();
    return writer.bit_count() == 0 && writer.byte_count() == 0;
  }());
}

TEST(Bits, BitSpanConstruction) {
  std::uint32_t x = 0xDEADBEEF;
  fp::bit_span whole(x);
  EXPECT_EQ(whole.size(), 32u);
  EXPECT_EQ(whole.offset(), 0u);
  EXPECT_FALSE(whole.empty());

  fp::bit_span low(x, 0, 15);
  EXPECT_EQ(low.size(), 16u);
  EXPECT_EQ(low.offset(), 0u);
  EXPECT_EQ(low.read(), 0xBEEFu);

  fp::bit_span mid(x, 8, 23);
  EXPECT_EQ(mid.offset(), 8u);
  EXPECT_EQ(mid.read(), 0xADBEu);

  // from raw bytes
  std::array<std::byte, 4> buf{std::byte{0x11}, std::byte{0x22},
                               std::byte{0x33}, std::byte{0x44}};
  fp::bit_span from_bytes(std::span<std::byte>(buf), 4, 11);
  EXPECT_EQ(from_bytes.size(), 8u);
  // bit numbering follows memory order: bit 0 is the low bit of byte 0
  EXPECT_EQ(from_bytes.read(), 0x21u);

  // mutable views convert to const ones, not the other way round
  fp::const_bit_span cview = whole;
  EXPECT_EQ(cview.read(), whole.read());
  static_assert(std::is_convertible_v<fp::bit_span, fp::const_bit_span>);
  static_assert(!std::is_convertible_v<fp::const_bit_span, fp::bit_span>);
}

TEST(Bits, BitSpanCopyAndOverloads) {
  // Regression: the explicit object constructor used to outrank the copy
  // constructor (T& beats T const&), so copies viewed their own bytes.
  std::uint8_t x = 0xB5;
  fp::bit_span s(x);
  fp::bit_span copy = s;
  EXPECT_EQ(copy.size(), 8u);
  EXPECT_EQ(copy.read(), 0xB5u);
  EXPECT_EQ(&copy.storage()[0], &s.storage()[0]);

  fp::const_bit_span c = s;
  EXPECT_EQ(c.size(), 8u);
  EXPECT_EQ(c.read(), 0xB5u);

  auto ref = s[0];
  EXPECT_TRUE(static_cast<bool>(ref));
  EXPECT_TRUE(static_cast<bool>(s[0]));
  EXPECT_FALSE(static_cast<bool>(s[1]));
}

TEST(Bits, BitSpanReadWrite) {
  std::uint32_t x = 0xDEADBEEF;
  fp::bit_span mid(x, 8, 23);
  mid.write(0x1234);
  EXPECT_EQ(x, 0xDE1234EFu); // the 0xADBE field at bits 8..23 was replaced

  // a write only touches its own bits
  fp::bit_span nib(x, 4, 7);
  nib.write(0xF);
  EXPECT_EQ(x, 0xDE1234FFu);

  // non-byte-aligned round trip
  std::array<std::byte, 8> buf{};
  fp::bit_span span(buf);
  for (std::size_t lo = 0; lo < 64; ++lo) {
    for (std::size_t width = 1; width <= 64 - lo; ++width) {
      const std::uint64_t value = 0x9E3779B97F4A7C15ull >> (64 - width);
      span.section(lo, lo + width - 1).write(value);
      EXPECT_EQ(span.section(lo, lo + width - 1).read(), value)
          << "lo " << lo << " width " << width;
    }
  }

  // read_at / write_at inside a section
  std::uint16_t y = 0;
  fp::bit_span ys(y);
  ys.write_at(3, 0b101, 3);
  EXPECT_EQ(y, 0b101000u);
  EXPECT_EQ(ys.read_at(3, 3), 0b101u);
}

TEST(Bits, BitSpanSingleBits) {
  std::uint8_t x = 0;
  fp::bit_span s(x);
  s.set_bit(3);
  EXPECT_EQ(x, 0b1000u);
  EXPECT_TRUE(s.get_bit(3));
  s.toggle_bit(3);
  EXPECT_EQ(x, 0u);
  s[0] = true;
  s[7] = true;
  EXPECT_EQ(x, 0b1000'0001u);
  s[0] = false;
  EXPECT_EQ(x, 0b1000'0000u);
  EXPECT_TRUE(s[7]);
  EXPECT_FALSE(s[0]);

  // assignment between proxies and through the iterator
  auto a = s[1];
  a = static_cast<bool>(s[7]);
  EXPECT_TRUE(s[1]);
  for (auto bit : s)
    bit = true;
  EXPECT_EQ(x, 0xFFu);

  // iteration reads
  int ones = 0;
  for (bool bit : s)
    ones += bit;
  EXPECT_EQ(ones, 8);
  for (bool bit : fp::const_bit_span(s))
    EXPECT_TRUE(bit);
}

TEST(Bits, BitSpanSlicing) {
  std::uint32_t x = 0xDEADBEEF;
  fp::bit_span whole(x);

  auto lo = whole.section(0, 15);
  auto hi = whole.section(16, 31);
  EXPECT_EQ(lo.read(), 0xBEEFu);
  EXPECT_EQ(hi.read(), 0xDEADu);

  auto [a, b] = whole.split(16);
  EXPECT_EQ(a.read(), lo.read());
  EXPECT_EQ(b.read(), hi.read());

  // copy a section between objects
  std::uint32_t dst = 0;
  fp::bit_span(dst, 8, 23).copy_from(fp::bit_span(x, 0, 15));
  EXPECT_EQ(dst, 0x00BEEF00u);

  // equality compares bits, not identity
  EXPECT_TRUE(lo.equal(fp::bit_span(dst, 8, 23)));
  EXPECT_FALSE(lo.equal(hi));

  // sections are writable
  auto nib = whole.section(4, 7);
  nib.fill(true);
  EXPECT_EQ(x, 0xDEADBEFFu);
}

TEST(Bits, BitSpanBulk) {
  std::array<std::byte, 4> buf{};
  fp::bit_span s(buf, 3, 20); // 18 bits
  s.fill(true);
  EXPECT_EQ(s.popcount(), 18);
  EXPECT_EQ(s.read(), 0x3FFFFu);
  // only the section's bits changed
  EXPECT_EQ(std::to_integer<unsigned>(buf[0]) & 0b111u, 0u);
  EXPECT_EQ(std::to_integer<unsigned>(buf[2]) & 0b0001'1111u, 0x1Fu);
  EXPECT_EQ(std::to_integer<unsigned>(buf[2]) & 0b1110'0000u, 0u);

  s.fill(false);
  EXPECT_EQ(s.popcount(), 0);
  EXPECT_EQ(s.read(), 0u);
  EXPECT_EQ(std::to_integer<unsigned>(buf[1]), 0u);

  std::uint32_t x = 0xF0F0F0F0;
  EXPECT_EQ(fp::bit_span(x).popcount(), 16);
  EXPECT_EQ(fp::bit_span(x, 4, 11).popcount(), 4);
}

TEST(Bits, BitSpanOnObjects) {
  // a float, field by field
  float f = 1.5f;
  fp::bit_span sign(f, 31, 31), exponent(f, 23, 30), mantissa(f, 0, 22);
  EXPECT_EQ(sign.read(), 0u);
  EXPECT_EQ(exponent.read(), 127u);
  EXPECT_EQ(mantissa.read(), 0x400000u);
  sign.write(1);
  EXPECT_FLOAT_EQ(f, -1.5f);

  // a struct: offsets are over the raw representation, padding included
  struct Header {
    std::uint8_t version;
    std::uint16_t flags;
    std::uint32_t length;
  };
  Header h{3, 0, 0};
  fp::bit_span flags(h, 16, 31);
  flags.write(0xBEEF);
  EXPECT_EQ(h.flags, 0xBEEFu);
  EXPECT_EQ(fp::const_bit_span(h, 16, 31).read(), 0xBEEFu);

  // const objects yield const spans (read-only, no proxy)
  const std::uint32_t c = 0x12345678;
  fp::const_bit_span cs(c, 4, 11);
  EXPECT_EQ(cs.read(), 0x67u);
  static_assert(std::is_same_v<decltype(cs[0]), bool>);
  static_assert(std::is_same_v<decltype(std::declval<fp::bit_span>()[0]),
                               fp::bit_span::reference>);
}

TEST(Bits, BitSpanText) {
  std::uint8_t x = 0xA5;
  EXPECT_EQ(fp::bit_span(x).to_binary(), "10100101");
  EXPECT_EQ(fp::bit_span(x, 0, 3).to_binary(), "0101");
  EXPECT_EQ(fp::bit_span(x, 4, 7).to_binary(true), "1010");
  EXPECT_EQ(fp::bit_span(x).to_binary(true), "1010 0101");
}

TEST(Bits, BitField) {
  std::uint16_t rgb = 0;
  fp::bit_field<11, 15>(rgb) = 31;
  fp::bit_field<5, 10>(rgb) = 42;
  fp::bit_field<0, 4>(rgb) = 21;
  EXPECT_EQ(rgb, 0xFD55u);
  EXPECT_EQ(static_cast<unsigned>(fp::bit_field<11, 15>(rgb)), 31u);
  EXPECT_EQ(static_cast<unsigned>(fp::bit_field<5, 10>(rgb)), 42u);
  EXPECT_EQ(static_cast<unsigned>(fp::bit_field<0, 4>(rgb)), 21u);

  using green = decltype(fp::bit_field<5, 10>(rgb));
  static_assert(green::lo == 5 && green::hi == 10 && green::width == 6);

  // assignment between fields, and get/set
  auto red = fp::bit_field<11, 15>(rgb);
  auto other = fp::bit_field<0, 4>(rgb);
  other = red;
  EXPECT_EQ(static_cast<unsigned>(fp::bit_field<0, 4>(rgb)), 31u);
  other.set(7);
  EXPECT_EQ(other.get(), 7u);

  // the whole value is only touched inside the field
  std::uint8_t flags = 0xFF;
  fp::bit_field<2, 4>(flags) = 0;
  EXPECT_EQ(flags, 0b1110'0011u);

  // signed values keep their type
  std::int32_t v = 0;
  fp::bit_field<0, 3>(v) = 0b1111;
  EXPECT_EQ(v, 15);
}

TEST(Bits, BitSplit) {
  std::uint16_t rgb = 0;
  auto [blue, green, red] = fp::bit_split<5, 6, 5>(rgb);
  blue = 21;
  green = 42;
  red = 31;
  EXPECT_EQ(rgb, 0xFD55u);

  // the widths are consumed from bit 0 up
  std::uint32_t packed = 0;
  auto [a, b, c] = fp::bit_split<4, 8, 20>(packed);
  a = 0xF;
  b = 0xAB;
  c = 0x12345;
  EXPECT_EQ(packed, 0xF | (0xABu << 4) | (0x12345u << 12));
  EXPECT_EQ(a.get(), 0xFu);
  EXPECT_EQ(b.get(), 0xABu);
  EXPECT_EQ(c.get(), 0x12345u);

  // consecutive widths must fit the value
  static_assert(can_bit_split<std::uint16_t, 8, 8>);
  static_assert(can_bit_split<std::uint32_t, 4, 8, 20>);
  static_assert(!can_bit_split<std::uint8_t, 16, 16>);
  static_assert(!can_bit_split<std::uint16_t, 8, 9>);
}

TEST(Bits, BitSpanFuzzAgainstModel) {
  std::mt19937_64 rng(99);
  std::array<std::byte, 24> buf{};
  std::vector<bool> model(buf.size() * 8, false);
  fp::bit_span whole(buf);

  for (int iter = 0; iter < 400; ++iter) {
    const std::size_t lo = rng() % model.size();
    const std::size_t hi = lo + rng() % (model.size() - lo);
    auto section = whole.section(lo, hi);
    switch (rng() % 4) {
    case 0: {
      const unsigned width = static_cast<unsigned>(hi - lo + 1);
      if (width <= 64) {
        // whole-section read/write is a <= 64-bit operation
        std::uint64_t value = rng();
        if (width < 64)
          value &= (std::uint64_t{1} << width) - 1;
        section.write(value);
        for (unsigned i = 0; i < width; ++i)
          model[lo + i] = ((value >> i) & 1u) != 0u;
      } else {
        // wider sections: fill (or copy) instead
        const bool value = (rng() & 1u) != 0u;
        section.fill(value);
        for (std::size_t i = lo; i <= hi; ++i)
          model[i] = value;
      }
      break;
    }
    case 1: {
      const bool value = (rng() & 1u) != 0u;
      section.fill(value);
      for (std::size_t i = lo; i <= hi; ++i)
        model[i] = value;
      break;
    }
    case 2: {
      const std::size_t i = lo + rng() % (hi - lo + 1);
      const bool value = (rng() & 1u) != 0u;
      whole.set_bit(i, value);
      model[i] = value;
      break;
    }
    default: {
      const std::size_t i = lo + rng() % (hi - lo + 1);
      whole.toggle_bit(i);
      model[i] = !model[i];
      break;
    }
    }

    for (std::size_t i = 0; i < model.size(); ++i)
      ASSERT_EQ(whole.get_bit(i), model[i]) << "iter " << iter << " bit " << i;
    int expected = 0;
    for (bool bit : model)
      expected += bit;
    ASSERT_EQ(whole.popcount(), expected) << "iter " << iter;
  }
}

TEST(Bits, BitSpanConstexpr) {
  // Over raw bytes everything is constexpr; constructing from an *object* goes
  // through as_bytes (reinterpret_cast) and is therefore runtime-only.
  static_assert([] {
    std::array<std::byte, 4> buf{};
    std::span<std::byte> bytes(buf);
    fp::bit_span s(bytes);
    s.section(4, 11).write(0xAB);
    if (s.section(4, 11).read() != 0xAB)
      return false;
    s.set_bit(0);
    if (!s.get_bit(0) || s.popcount() != 6)
      return false;
    auto [lo, hi] = s.split(16);
    lo.fill(true);
    return lo.popcount() == 16 && hi.popcount() == 0;
  }());

  static_assert([] {
    std::uint16_t rgb = 0;
    fp::bit_field<11, 15>(rgb) = 31;
    auto [b, g, r] = fp::bit_split<5, 6, 5>(rgb);
    b = 1;
    g = 2;
    r = 3;
    return rgb == 0x1841;
  }());
}

TEST(Bits, BitReaderErrors) {
  const std::vector<std::byte> data{std::byte{0x0F}};
  fp::BitReader reader(data);
  EXPECT_TRUE(reader.read(4).is_ok());
  EXPECT_FALSE(reader.read(5).is_ok());
  EXPECT_TRUE(reader.read(4).is_ok());
  EXPECT_TRUE(reader.at_end());
  EXPECT_FALSE(reader.read(1).is_ok());
  EXPECT_FALSE(reader.read(65).is_ok());

  fp::BitReader other(data);
  EXPECT_FALSE(other.skip(9).is_ok());
  EXPECT_TRUE(other.skip(8).is_ok());
  EXPECT_EQ(other.position(), 8u);
  other.reset();
  EXPECT_EQ(other.remaining(), 8u);
}

TEST(Bits, WordAtPastEnd) {
  // Regression: byte_offset + i used to wrap for huge offsets and read inside
  // the object instead of returning zero.
  const std::uint32_t x = 0x11223344;
  EXPECT_EQ(fp::word_at<std::uint32_t>(x, ~std::size_t{0} - 2), 0u);
  EXPECT_EQ(fp::word_at<std::uint32_t>(x, 4), 0u);
  EXPECT_EQ(fp::word_at<std::uint32_t>(x, 1000), 0u);
  // the last in-range byte still reads
  EXPECT_NE(fp::word_at<std::uint8_t>(x, 3), 0u);
}

TEST(Bits, CopyFromOverlap) {
  // Regression: copy_from used memcpy semantics, so an in-place shift through
  // overlapping sections corrupted the tail.
  constexpr std::size_t total = 160;
  std::mt19937_64 rng(4242);
  for (int iter = 0; iter < 200; ++iter) {
    std::array<std::byte, total / 8> buf{};
    fp::bit_span whole(buf);
    std::vector<bool> model(total, false);
    for (std::size_t i = 0; i < total; ++i) {
      const bool v = (rng() & 1u) != 0u;
      model[i] = v;
      whole.set_bit(i, v);
    }
    const std::size_t n = 1 + rng() % 159;
    const std::size_t dst = rng() % (total - n + 1);
    const std::size_t src = rng() % (total - n + 1);

    std::vector<bool> moved(n);
    for (std::size_t i = 0; i < n; ++i)
      moved[i] = model[src + i];
    for (std::size_t i = 0; i < n; ++i)
      model[dst + i] = moved[i];

    whole.section(dst, dst + n - 1).copy_from(whole.section(src, src + n - 1));

    for (std::size_t i = 0; i < total; ++i)
      ASSERT_EQ(whole.get_bit(i), model[i])
          << "iter " << iter << " dst " << dst << " src " << src << " n " << n
          << " bit " << i;
  }
}

TEST(Bits, ReleaseSafeRanges) {
  // The runtime ranges assert in debug; these are the in-range edges.
  EXPECT_EQ(fp::bit_mask<std::uint8_t>(0, 7), 0xFF);
  EXPECT_EQ(fp::extract_bits(std::uint8_t{0xFF}, 0, 7), 0xFF);
  EXPECT_EQ(fp::extract_bits(std::uint8_t{0xFF}, 3, 3), 1);
  EXPECT_EQ(fp::insert_bits(std::uint8_t{0}, 0, 0, 1), 1);
  EXPECT_EQ(fp::bit_mask<std::uint64_t>(0, 63), ~std::uint64_t{0});
  EXPECT_EQ(fp::extract_bits(std::uint64_t{1} << 63, 63, 63), 1);
  EXPECT_EQ(fp::bit(std::uint8_t{1}, 0), true);
}

TEST(Bits, BitSplitRejectsEmptyWidths) {
  static_assert(can_bit_split<std::uint16_t, 8, 8>);
  static_assert(!can_bit_split<std::uint16_t, 0, 8>);   // zero width
  static_assert(!can_bit_split<std::uint16_t, 8, 0>);
  static_assert(!can_bit_split<std::uint16_t, 16, 16>); // too wide
}

TEST(Bits, RangesIntegration) {
  static_assert(std::ranges::input_range<fp::bit_span>);
  static_assert(std::ranges::input_range<fp::const_bit_span>);
  static_assert(std::ranges::sized_range<fp::bit_span>);
  static_assert(std::is_default_constructible_v<fp::bit_span>);
  static_assert(std::is_default_constructible_v<fp::bit_span::iterator>);
  static_assert(fp::bit_integral<std::uint32_t>);
  static_assert(!fp::bit_integral<bool>);

  std::uint8_t v = 0b1010'0101;
  fp::const_bit_span s(v);
  EXPECT_EQ(std::ranges::count_if(s, [](bool b) { return b; }), 4);
  EXPECT_EQ(std::ranges::distance(s), 8);

  fp::bit_span empty;
  EXPECT_EQ(empty.size(), 0u);
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(std::ranges::distance(empty), 0);
}

TEST(Bits, ByteSpans) {
  // 1-byte element spans: unsigned char / char buffers are the common case.
  std::vector<unsigned char> ubuf{0x11, 0x22, 0x33, 0x44};
  fp::bit_span ub{std::span<unsigned char>(ubuf)};
  EXPECT_EQ(ub.size(), 32u);
  if constexpr (std::endian::native == std::endian::little) {
    EXPECT_EQ(ub.read(), 0x44332211u);
  }
  ub.write(0xDEADBEEF);
  EXPECT_EQ(ubuf[0], 0xEF);

  std::vector<char> cbuf{'A', 'B'};
  fp::const_bit_span cb{std::span<char>(cbuf)};
  EXPECT_EQ(cb.size(), 16u);
  EXPECT_EQ(cb.read(), static_cast<std::uint64_t>('A') | (std::uint64_t('B') << 8));

  std::array<std::byte, 2> abuf{std::byte{0x0F}, std::byte{0xF0}};
  fp::const_bit_span fixed{std::span<std::byte, 2>(abuf)};
  EXPECT_EQ(fixed.size(), 16u);
}

TEST(Bits, SpanEquality) {
  std::uint16_t x = 0xABCD, y = 0xABCD;
  EXPECT_TRUE(fp::bit_span(x) == fp::bit_span(y));
  EXPECT_FALSE(fp::bit_span(x, 0, 7) == fp::bit_span(y, 8, 15));
  EXPECT_FALSE(fp::bit_span(x, 0, 7) == fp::bit_span(y, 0, 15));
  const std::uint16_t z = 0xABCD;
  EXPECT_TRUE(fp::bit_span(x) == fp::const_bit_span(z)); // mixed constness
  EXPECT_TRUE(fp::bit_span(x, 4, 11) == fp::bit_span(y, 4, 11));
}

TEST(Bits, SpanStorageHelpers) {
  std::uint32_t x = 0;
  fp::bit_span s(x, 4, 19);
  EXPECT_EQ(s.size(), 16u);
  EXPECT_EQ(s.size_bytes(), 2u);
  EXPECT_EQ(s.offset(), 4u);
  EXPECT_EQ(s.storage().size(), sizeof(x));
  EXPECT_EQ(fp::bit_span{}.size_bytes(), 0u);
}

TEST(Bits, BitReverse) {
  static_assert(fp::bit_reverse(std::uint8_t{0x01}) == 0x80);
  static_assert(fp::bit_reverse(std::uint8_t{0b1011'0001}) == 0b1000'1101);
  static_assert(fp::bit_reverse(std::uint16_t{0x0001}) == 0x8000);
  static_assert(fp::bit_reverse(std::uint32_t{1}) == 0x80000000u);
  static_assert(fp::bit_reverse(std::uint32_t{0x12345678}) == 0x1E6A2C48u);
  static_assert(fp::bit_reverse(std::uint8_t{0b1011}, 4) == 0b1101);
  static_assert(fp::bit_reverse(std::uint8_t{0b1011'0000}, 4) == 0);
  static_assert(fp::bit_reverse(std::uint8_t{0xFF}, 0) == 0);

  // involutive, and width-limited reversal only touches the low bits
  for (std::uint64_t v : {0ull, 1ull, 0x0123456789ABCDEFull, ~0ull}) {
    EXPECT_EQ(fp::bit_reverse(fp::bit_reverse(v)), v);
  }
  EXPECT_EQ(fp::bit_reverse(std::uint8_t{0b1111'0000}, 4), 0);
  EXPECT_EQ(fp::bit_reverse(std::uint8_t{0b0000'1011}, 4), 0b1101);
  // signed types reverse their two's-complement representation
  EXPECT_EQ(static_cast<std::uint8_t>(fp::bit_reverse(std::int8_t{-1})), 0xFF);
}

TEST(Bits, ToHex) {
  const std::vector<std::byte> buf{std::byte{0xDE}, std::byte{0xAD},
                                   std::byte{0xBE}, std::byte{0xEF}};
  EXPECT_EQ(fp::to_hex(buf), "deadbeef");
  EXPECT_EQ(fp::to_hex(buf, true), "de ad be ef");
  EXPECT_EQ(fp::str::to_upper(fp::to_hex(buf)), "DEADBEEF");
  EXPECT_EQ(fp::to_hex(std::vector<std::byte>{}), "");
  EXPECT_EQ(fp::to_hex(std::vector<unsigned char>{0xAB, 0xCD}), "abcd");
  EXPECT_EQ(fp::to_hex(std::string_view("AB")), "4142");
  // over an object's representation, via as_bytes
  const std::uint32_t x = 0x11223344;
  if constexpr (std::endian::native == std::endian::little)
    EXPECT_EQ(fp::to_hex(fp::as_bytes(x)), "44332211");
  else
    EXPECT_EQ(fp::to_hex(fp::as_bytes(x)), "11223344");
}

TEST(Bits, SearchAndPredicates) {
  std::uint16_t bits = 0;
  fp::bit_span s(bits);
  EXPECT_TRUE(s.none());
  EXPECT_FALSE(s.any());
  EXPECT_FALSE(s.all());
  EXPECT_FALSE(s.find_first_set().has_value());
  ASSERT_TRUE(s.find_first_clear().has_value());
  EXPECT_EQ(*s.find_first_clear(), 0u);

  s.set_bit(3);
  s.set_bit(9);
  s.set_bit(15);
  EXPECT_TRUE(s.any());
  EXPECT_FALSE(s.none());
  EXPECT_FALSE(s.all());
  ASSERT_TRUE(s.find_first_set().has_value());
  EXPECT_EQ(*s.find_first_set(), 3u);
  ASSERT_TRUE(s.find_next_set(4).has_value());
  EXPECT_EQ(*s.find_next_set(4), 9u);
  ASSERT_TRUE(s.find_next_set(10).has_value());
  EXPECT_EQ(*s.find_next_set(10), 15u);
  EXPECT_FALSE(s.find_next_set(16).has_value());
  ASSERT_TRUE(s.find_last_set().has_value());
  EXPECT_EQ(*s.find_last_set(), 15u);
  EXPECT_EQ(*s.find_first_clear(), 0u);
  EXPECT_EQ(*s.find_next_clear(3), 4u);

  s.fill(true);
  EXPECT_TRUE(s.all());
  EXPECT_FALSE(s.find_first_clear().has_value());
  EXPECT_EQ(*s.find_first_set(), 0u);
  EXPECT_EQ(*s.find_last_set(), 15u);

  s.flip();
  EXPECT_TRUE(s.none());
  EXPECT_FALSE(s.any());

  // a section with a bit offset (not byte aligned)
  std::uint32_t x = 0xDEADBEEF;
  fp::bit_span mid(x, 4, 19);
  mid.fill(false);
  mid.set_bit(1);
  mid.set_bit(13);
  EXPECT_EQ(*mid.find_first_set(), 1u);
  EXPECT_EQ(*mid.find_last_set(), 13u);
  EXPECT_EQ(mid.popcount(), 2);

  // empty span: nothing to find
  fp::bit_span empty;
  EXPECT_FALSE(empty.find_first_set().has_value());
  EXPECT_FALSE(empty.find_last_set().has_value());
  EXPECT_FALSE(empty.any());
  EXPECT_TRUE(empty.all()); // vacuously true, like std::bitset::all()
  EXPECT_TRUE(empty.none());
}

TEST(Bits, MsbStreams) {
  fp::MsbWriter w;
  w.write(0b101, 3);
  w.write(0xABCD, 16);
  ASSERT_EQ(w.bit_count(), 19u);
  ASSERT_EQ(w.byte_count(), 3u);
  // top bit of the first field lands in bit 7 of the first byte
  EXPECT_EQ(std::to_integer<unsigned>(w.data()[0]), 0xB5u);

  fp::MsbReader r{w.data()};
  auto a = r.read(3);
  auto b = r.read(16);
  ASSERT_TRUE(a.is_ok() && b.is_ok());
  EXPECT_EQ(a.value(), 0b101u);
  EXPECT_EQ(b.value(), 0xABCDu);
  EXPECT_EQ(r.remaining(), 5u); // the writer pads the last byte
  EXPECT_EQ(r.position(), 19u);
}

TEST(Bits, MsbStreamFuzz) {
  std::mt19937_64 rng(77);
  for (int iter = 0; iter < 500; ++iter) {
    fp::MsbWriter w;
    fp::BitWriter lw;
    std::vector<std::pair<std::uint64_t, unsigned>> written;
    const int count = 1 + static_cast<int>(rng() % 16);
    for (int i = 0; i < count; ++i) {
      const unsigned n = static_cast<unsigned>(rng() % 65);
      std::uint64_t v = rng();
      if (n < 64)
        v &= (std::uint64_t{1} << n) - 1;
      w.write(v, n);
      lw.write<fp::BitOrder::MsbFirst>(v, n); // override on an LSB stream
      written.emplace_back(v, n);
      if (rng() % 3 == 0) {
        w.align_to_byte();
        lw.align_to_byte();
        written.emplace_back(0, 0xFFFFFFFFu); // sentinel: alignment
      }
    }
    fp::MsbReader r{w.data()};
    fp::BitReader lr{lw.data()};
    for (auto const &[v, n] : written) {
      if (n == 0xFFFFFFFFu) {
        ASSERT_TRUE(r.align_to_byte().is_ok());
        ASSERT_TRUE(lr.align_to_byte().is_ok());
        continue;
      }
      auto msb = r.read(n);
      auto mixed = lr.read<fp::BitOrder::MsbFirst>(n);
      ASSERT_TRUE(msb.is_ok() && mixed.is_ok()) << "width " << n;
      EXPECT_EQ(msb.value(), v) << "width " << n;
      EXPECT_EQ(mixed.value(), v) << "width " << n;
    }
  }
}

TEST(Bits, StreamAlignment) {
  fp::BitWriter w;
  w.write(0b101, 3);
  w.align_to_byte(true); // pad with ones
  EXPECT_EQ(w.bit_count(), 8u);
  EXPECT_EQ(std::to_integer<unsigned>(w.data()[0]), 0xFDu);

  fp::BitReader r{w.data()};
  EXPECT_EQ(r.read(3).value(), 0b101u);
  ASSERT_TRUE(r.align_to_byte().is_ok());
  EXPECT_EQ(r.position(), 8u);
  EXPECT_TRUE(r.at_end());

  // no padding needed -> no-op
  fp::BitWriter w2;
  w2.write(0xFF, 8);
  w2.align_to_byte();
  EXPECT_EQ(w2.bit_count(), 8u);

  // a reader bounded to a 4-bit section cannot align to a byte
  std::uint8_t tiny = 0xF0;
  fp::BitReader r2{fp::const_bit_span(tiny, 0, 3)};
  EXPECT_EQ(r2.read(1).value(), 0u);
  EXPECT_FALSE(r2.align_to_byte().is_ok()); // needs 7, only 3 left
}

TEST(Bits, StreamViewInterop) {
  // write a section into a stream, read it back into another
  std::uint16_t src = 0xBEEF;
  fp::BitWriter w;
  w.write(fp::bit_span(src));
  ASSERT_EQ(w.bit_count(), 16u);

  std::uint16_t dst = 0;
  fp::BitReader r{w.data()};
  ASSERT_TRUE(r.read_into(fp::bit_span(dst)).is_ok());
  EXPECT_EQ(dst, 0xBEEF);

  // peek_bits gives a view without consuming
  fp::BitReader r2{w.data()};
  auto window = r2.peek_bits(8);
  ASSERT_TRUE(window.is_ok());
  EXPECT_EQ(window.value().size(), 8u);
  EXPECT_EQ(window.value().read(), 0xEFu);
  EXPECT_EQ(r2.position(), 0u);
  EXPECT_EQ(r2.read(8).value(), 0xEFu);

  // empty peek, and past the end
  EXPECT_TRUE(r2.peek_bits(0).is_ok());
  EXPECT_FALSE(r2.peek_bits(100).is_ok());

  // release hands over the bytes
  auto bytes = w.release();
  EXPECT_EQ(bytes.size(), 2u);
  EXPECT_EQ(w.byte_count(), 0u);
  EXPECT_EQ(w.bit_count(), 0u);

  // an MSB stream copies bits, not values
  fp::MsbWriter mw;
  mw.write(0b1011, 4);
  std::uint8_t nib = 0;
  fp::MsbReader mr{mw.data()};
  ASSERT_TRUE(mr.read_into(fp::bit_span(nib, 0, 3)).is_ok());
  EXPECT_EQ(fp::bit_span(nib, 0, 3).to_binary(), "1011");
}

TEST(Bits, BoundedReader) {
  std::uint32_t x = 0xDEADBEEF;
  fp::bit_span section(x, 8, 23);
  fp::BitReader r{section};
  EXPECT_EQ(r.remaining(), 16u);
  EXPECT_EQ(r.position(), 0u);
  auto v = r.read(16);
  ASSERT_TRUE(v.is_ok());
  EXPECT_EQ(v.value(), 0xADBEu);
  EXPECT_EQ(r.remaining(), 0u);
  EXPECT_TRUE(r.at_end());
  EXPECT_FALSE(r.read(1).is_ok()); // cannot read past the section
  r.reset();
  EXPECT_EQ(r.remaining(), 16u);
  EXPECT_EQ(r.read(4).value(), 0xEu);
  EXPECT_EQ(r.position(), 4u);

  // the section is a view: writes through it are visible
  section.write(0x1234);
  EXPECT_EQ(x, 0xDE1234EFu);

  // a reader over a const section reads the same bits
  const std::uint32_t y = 0xDEADBEEF;
  fp::BitReader cr{fp::const_bit_span(y, 8, 23)};
  EXPECT_EQ(cr.read(16).value(), 0xADBEu);
}

TEST(Bits, ToBinaryFuzzAgainstReference) {
  std::mt19937_64 rng(31337);
  std::array<std::byte, 12> buf{};
  for (auto &b : buf)
    b = static_cast<std::byte>(rng());
  fp::const_bit_span whole{buf};

  for (int iter = 0; iter < 400; ++iter) {
    const std::size_t total = buf.size() * 8;
    const std::size_t lo = rng() % total;
    const std::size_t hi = lo + rng() % (total - lo);
    const bool group = (rng() & 1u) != 0u;
    auto s = whole.section(lo, hi);

    std::string expected;
    for (std::size_t i = s.size(); i-- > 0;) {
      expected.push_back(s.get_bit(i) ? '1' : '0');
      if (group && i > 0 && i % 4 == 0)
        expected.push_back(' ');
    }
    ASSERT_EQ(s.to_binary(group), expected)
        << "lo " << lo << " hi " << hi << " group " << group;
  }
}

// Benchmark the bit helpers against hand-written counterparts.
//
// Element-wise operations are measured as throughput over an array (ns per
// element): at ~0.25 ns/op a single-op loop is all loop overhead and the
// numbers swing with code alignment. Bit streams are measured per value with
// the writer cleared between reps, so the packing loop is what is timed (and
// neither side can be turned into a bulk memcpy by the optimizer).
#include <fp/bits.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "bench.hpp"

namespace {

constexpr std::size_t n = 1 << 12;

// Hand-written references with the same semantics as the fp versions.

std::uint32_t hand_bit_and32(std::uint32_t a, std::uint32_t b) { return a & b; }
std::uint64_t hand_bit_and64(std::uint64_t a, std::uint64_t b) { return a & b; }

float hand_bit_and_float(float a, float b) {
  return std::bit_cast<float>(std::bit_cast<std::uint32_t>(a) &
                              std::bit_cast<std::uint32_t>(b));
}

std::uint32_t hand_rotl32(std::uint32_t x, int k) {
  return (x << k) | (x >> (32 - k));
}

std::uint32_t hand_extract(std::uint32_t x, unsigned lo, unsigned hi) {
  return (x >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

std::uint32_t hand_word_at(std::uint64_t const &x, std::size_t off) {
  std::uint32_t out{};
  std::memcpy(&out, reinterpret_cast<char const *>(&x) + off, sizeof(out));
  return out;
}

// Byte-wise bit stream writer: same LSB-first layout as fp::BitWriter.
struct HandBitWriter {
  std::vector<std::byte> bytes;
  std::size_t bits = 0;

  void write(std::uint64_t value, unsigned count) {
    unsigned pos = static_cast<unsigned>(bits % 8);
    if (pos != 0) {
      unsigned take = std::min(count, 8u - pos);
      bytes.back() |= std::byte((value & ((1u << take) - 1u)) << pos);
      value >>= take;
      count -= take;
      bits += take;
    }
    while (count >= 8) {
      bytes.push_back(std::byte(value & 0xFF));
      value >>= 8;
      count -= 8;
      bits += 8;
    }
    if (count > 0) {
      bytes.push_back(std::byte(value & ((1u << count) - 1u)));
      bits += count;
    }
  }
};

// The implementation fp::BitWriter had before it went byte-wise: one bit per
// loop iteration. Kept as the reference point for the rewrite.
struct BitByBitWriter {
  std::vector<std::byte> bytes;
  std::size_t bits = 0;

  void write(std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      if (bits % 8 == 0)
        bytes.push_back(std::byte{0});
      if (((value >> i) & 1u) != 0u)
        bytes.back() |= std::byte(1u << (bits % 8));
      ++bits;
    }
  }
};

// Bit-reverse reference: swap-and-mask SWAR.
std::uint64_t hand_bit_reverse64(std::uint64_t v) {
  v = ((v >> 1) & 0x5555555555555555ull) | ((v & 0x5555555555555555ull) << 1);
  v = ((v >> 2) & 0x3333333333333333ull) | ((v & 0x3333333333333333ull) << 2);
  v = ((v >> 4) & 0x0F0F0F0F0F0F0F0Full) | ((v & 0x0F0F0F0F0F0F0F0Full) << 4);
  v = ((v >> 8) & 0x00FF00FF00FF00FFull) | ((v & 0x00FF00FF00FF00FFull) << 8);
  v = ((v >> 16) & 0x0000FFFF0000FFFFull) | ((v & 0x0000FFFF0000FFFFull) << 16);
  return (v >> 32) | (v << 32);
}

// Bitmap scan reference: byte at a time.
std::size_t hand_find_next_set(std::span<std::byte const> bytes, std::size_t from) {
  for (std::size_t i = from; i < bytes.size() * 8; ++i) {
    const auto byte = std::to_integer<unsigned>(bytes[i / 8]);
    if ((byte >> (i % 8)) & 1u)
      return i;
  }
  return static_cast<std::size_t>(-1);
}

// Nibble-table reference for to_binary.
std::string hand_to_binary(std::uint32_t x) {
  static constexpr char nib[16][5] = {"0000", "0001", "0010", "0011",
                                      "0100", "0101", "0110", "0111",
                                      "1000", "1001", "1010", "1011",
                                      "1100", "1101", "1110", "1111"};
  std::string out(32, '0');
  for (int i = 0; i < 8; ++i) {
    const unsigned v = (x >> ((7 - i) * 4)) & 0xF;
    std::memcpy(out.data() + static_cast<std::size_t>(i) * 4, nib[v], 4);
  }
  return out;
}

} // namespace

int main() {
  volatile std::uint64_t sink64 = 0;
  std::vector<std::uint32_t> a32(n), b32(n), out32(n);
  std::vector<std::uint64_t> a64(n), b64(n), out64(n);
  std::vector<float> fa(n), fb(n), fout(n);
  for (std::size_t i = 0; i < n; ++i) {
    a32[i] = 0x12345678u + static_cast<std::uint32_t>(i);
    b32[i] = 0x0F0F0F0Fu ^ static_cast<std::uint32_t>(i);
    a64[i] = 0x123456789ABCDEF0ull + i;
    b64[i] = 0x0F0F0F0F0F0F0F0Full ^ i;
    fa[i] = 1.5f + static_cast<float>(i);
    fb[i] = 2.5f - static_cast<float>(i);
  }

  bench::measure("bit_and u32: hand", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = hand_bit_and32(a32[i], b32[i]);
    bench::keep(out32.data());
  });
  bench::measure("bit_and u32: fp::bit_and", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = fp::bit_and(a32[i], b32[i]);
    bench::keep(out32.data());
  });

  bench::measure("bit_and u64: hand", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out64[i] = hand_bit_and64(a64[i], b64[i]);
    bench::keep(out64.data());
  });
  bench::measure("bit_and u64: fp::bit_and", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out64[i] = fp::bit_and(a64[i], b64[i]);
    bench::keep(out64.data());
  });

  bench::measure("bit_and float: hand", [&] {
    for (std::size_t i = 0; i < n; ++i)
      fout[i] = hand_bit_and_float(fa[i], fb[i]);
    bench::keep(fout.data());
  });
  bench::measure("bit_and float: fp::bit_and", [&] {
    for (std::size_t i = 0; i < n; ++i)
      fout[i] = fp::bit_and(fa[i], fb[i]);
    bench::keep(fout.data());
  });

  bench::measure("bit_not u32: hand", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = ~a32[i];
    bench::keep(out32.data());
  });
  bench::measure("bit_not u32: fp::bit_not", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = fp::bit_not(a32[i]);
    bench::keep(out32.data());
  });

  bench::measure("byteswap u32: __builtin", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = __builtin_bswap32(a32[i]);
    bench::keep(out32.data());
  });
  bench::measure("byteswap u32: fp::byteswap", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = fp::byteswap(a32[i]);
    bench::keep(out32.data());
  });
  bench::measure("byteswap u64: __builtin", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out64[i] = __builtin_bswap64(a64[i]);
    bench::keep(out64.data());
  });
  bench::measure("byteswap u64: fp::byteswap", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out64[i] = fp::byteswap(a64[i]);
    bench::keep(out64.data());
  });

  bench::measure("rotl u32: hand", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = hand_rotl32(a32[i], 7);
    bench::keep(out32.data());
  });
  bench::measure("rotl u32: fp::rotl", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = fp::rotl(a32[i], 7);
    bench::keep(out32.data());
  });

  bench::measure("extract_bits: hand", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = hand_extract(a32[i], 8, 19);
    bench::keep(out32.data());
  });
  bench::measure("extract_bits: fp", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = fp::extract_bits(a32[i], 8, 19);
    bench::keep(out32.data());
  });

  bench::measure("insert_bits: fp", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = fp::insert_bits(a32[i], 8, 19, b32[i]);
    bench::keep(out32.data());
  });

  // Compile-time fields (bits 5..20: not byte aligned, so the comparison is
  // like for like) and runtime section views.
  constexpr std::uint32_t field_mask = ((1u << 16) - 1) << 5;
  bench::measure("bit_field write: hand shift", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = (out32[i] & ~field_mask) | ((a32[i] << 5) & field_mask);
    bench::keep(out32.data());
  });
  bench::measure("bit_field write: fp", [&] {
    for (std::size_t i = 0; i < n; ++i)
      fp::bit_field<5, 20>(out32[i]) = a32[i];
    bench::keep(out32.data());
  });
  bench::measure("bit_field read: hand shift", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = (a32[i] >> 5) & 0xFFFFu;
    bench::keep(out32.data());
  });
  bench::measure("bit_field read: fp", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = static_cast<std::uint32_t>(fp::bit_field<5, 20>(a32[i]));
    bench::keep(out32.data());
  });
  bench::measure("bit_span read 16b: fp", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] =
          static_cast<std::uint32_t>(fp::bit_span(a32[i], 5, 20).read());
    bench::keep(out32.data());
  });
  bench::measure("bit_span write 16b: fp", [&] {
    for (std::size_t i = 0; i < n; ++i)
      fp::bit_span(out32[i], 5, 20).write(a32[i]);
    bench::keep(out32.data());
  });

  bench::measure("word_at u32: memcpy", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = hand_word_at(a64[i], 4);
    bench::keep(out32.data());
  });
  bench::measure("word_at u32: fp::word_at", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = fp::word_at<std::uint32_t>(a64[i], 4);
    bench::keep(out32.data());
  });

  bench::measure("popcount: std", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = static_cast<std::uint32_t>(std::popcount(a32[i]));
    bench::keep(out32.data());
  });
  bench::measure("popcount: fp", [&] {
    for (std::size_t i = 0; i < n; ++i)
      out32[i] = static_cast<std::uint32_t>(fp::popcount(a32[i]));
    bench::keep(out32.data());
  });

  // Bit streams: ns per 64-bit value written / read.
  {
    BitByBitWriter writer;
    writer.bytes.reserve(n * 8);
    bench::measure("BitWriter 64b: bit-by-bit (old)", [&] {
      writer.bytes.clear();
      writer.bits = 0;
      for (std::size_t i = 0; i < n; ++i)
        writer.write(a64[i], 64);
      bench::keep(writer.bytes.data());
    });
  }
  {
    HandBitWriter writer;
    writer.bytes.reserve(n * 8);
    bench::measure("BitWriter 64b: hand byte-wise", [&] {
      writer.bytes.clear();
      writer.bits = 0;
      for (std::size_t i = 0; i < n; ++i)
        writer.write(a64[i], 64);
      bench::keep(writer.bytes.data());
    });
  }
  {
    fp::BitWriter writer;
    writer.reserve(n * 8);
    bench::measure("BitWriter 64b: fp", [&] {
      writer.clear();
      for (std::size_t i = 0; i < n; ++i)
        writer.write(a64[i], 64);
      bench::keep(writer.data().data());
    });
  }

  {
    fp::BitWriter writer;
    writer.reserve(n * 8);
    for (std::size_t i = 0; i < n; ++i)
      writer.write(a64[i], 64);
    auto data = writer.data();
    bench::measure("BitReader 64b: bit-by-bit (old)", [&] {
      std::uint64_t acc = 0;
      std::size_t pos = 0;
      for (std::size_t i = 0; i < n; ++i) {
        std::uint64_t out = 0;
        for (unsigned b = 0; b < 64; ++b) {
          const auto byte = std::to_integer<unsigned char>(data[pos / 8]);
          if (((byte >> (pos % 8)) & 1u) != 0u)
            out |= std::uint64_t{1} << b;
          ++pos;
        }
        acc += out;
      }
      bench::keep(&acc);
    });
    bench::measure("BitReader 64b: fp", [&] {
      std::uint64_t acc = 0;
      fp::BitReader reader(data);
      for (std::size_t i = 0; i < n; ++i)
        acc += reader.read(64).value();
      bench::keep(&acc);
    });
    bench::measure("BitReader 1b: fp", [&] {
      std::uint64_t acc = 0;
      fp::BitReader reader(data);
      for (std::size_t i = 0; i < n * 8; ++i)
        acc += reader.read_bit().value();
      bench::keep(&acc);
    });
  }

  // --- new features ---
  {
    std::vector<std::byte> bitmap(4096, std::byte{0});
    fp::bit_span bs{std::span<std::byte>(bitmap)};
    bs.set_bit(32000); // one set bit near the end
    bench::measure("find_next_set 32k: hand bytes", [&] {
      sink64 = hand_find_next_set(bitmap, 0);
      bench::keep(&sink64);
    });
    bench::measure("find_next_set 32k: fp", [&] {
      sink64 = bs.find_first_set().value_or(~std::size_t{0});
      bench::keep(&sink64);
    });
  }

  bench::measure("bit_reverse u64: SWAR", [&] {
    sink64 = hand_bit_reverse64(a64[n - 1]);
    bench::keep(&sink64);
  });
  bench::measure("bit_reverse u64: fp", [&] {
    sink64 = fp::bit_reverse(a64[n - 1]);
    bench::keep(&sink64);
  });

  {
    fp::MsbWriter mw;
    mw.reserve(n * 8);
    bench::measure("BitWriter 64b MSB: fp", [&] {
      mw.clear();
      for (std::size_t i = 0; i < n; ++i)
        mw.write(a64[i], 64);
      bench::keep(mw.data().data());
    });
    auto mdata = mw.data();
    bench::measure("BitReader 64b MSB: fp", [&] {
      std::uint64_t acc = 0;
      fp::MsbReader mr{mdata};
      for (std::size_t i = 0; i < n; ++i)
        acc += mr.read(64).value();
      bench::keep(&acc);
    });
  }

  {
    std::vector<std::byte> bytes(n);
    for (std::size_t i = 0; i < n; ++i)
      bytes[i] = static_cast<std::byte>(a64[i] & 0xFF);
    bench::measure("to_hex 4k bytes: fp", [&] {
      std::string s = fp::to_hex(bytes, true);
      bench::keep(s.data());
    });
  }

  bench::measure("to_binary u32: hand", [&] {
    std::string s = hand_to_binary(a32[n - 1]);
    bench::keep(s.data());
  });
  bench::measure("to_binary u32: fp", [&] {
    std::string s = fp::to_binary(a32[n - 1]);
    bench::keep(s.data());
  });
  bench::measure("to_binary u64 grouped: fp", [&] {
    std::string s = fp::to_binary(a64[n - 1], true);
    bench::keep(s.data());
  });
}

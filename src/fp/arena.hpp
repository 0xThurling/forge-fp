#pragma once
#include "scope.hpp"
#include <algorithm>
#include <cstddef>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace fp {

// A bump allocator. Hands out aligned raw memory from over-aligned blocks and
// reclaims everything at once with reset(). Pointers stay valid until reset()
// (growth adds blocks rather than moving existing ones).
class Arena {
public:
  static constexpr std::size_t alignment = 64;

  explicit Arena(std::size_t block = 64 * 1024) : block_(block) {}

  Arena(Arena const &) = delete;
  Arena &operator=(Arena const &) = delete;

  ~Arena() {
    for (auto const &b : blocks_)
      ::operator delete(b.data, std::align_val_t{alignment});
  }

  template <class T> T *alloc(std::size_t n = 1) {
    static_assert(alignof(T) <= alignment,
                  "Arena supports alignment up to 64 bytes");
    constexpr std::size_t align = alignof(T);
    const std::size_t bytes = n * sizeof(T);
    if (blocks_.empty() || !fits(align, bytes))
      add_block(align, bytes);
    auto &b = blocks_.back();
    std::size_t off = (b.used + align - 1) & ~(align - 1);
    T *p = reinterpret_cast<T *>(b.data + off);
    b.used = off + bytes;
    return p;
  }

  template <class T, class... Ts> T *make(Ts &&...ts) {
    T *p = alloc<T>();
    new (p) T(std::forward<Ts>(ts)...);
    return p;
  }

  // Raw bytes, aligned to `align` (clamped to the arena's 64-byte alignment).
  std::span<std::byte> alloc_bytes(std::size_t n,
                                   std::size_t align = alignment) {
    if (align > alignment)
      align = alignment;
    if (align == 0)
      align = 1;
    if (blocks_.empty() || !fits(align, n))
      add_block(align, n);
    auto &b = blocks_.back();
    std::size_t off = (b.used + align - 1) & ~(align - 1);
    auto *p = reinterpret_cast<std::byte *>(b.data + off);
    b.used = off + n;
    return {p, n};
  }

  template <class T> std::span<T> alloc_span(std::size_t n = 1) {
    auto bytes = alloc_bytes(n * sizeof(T), alignof(T));
    return {reinterpret_cast<T *>(bytes.data()), n};
  }

  void reset() {
    for (auto &b : blocks_)
      b.used = 0;
  }

  std::size_t used() const {
    std::size_t total = 0;
    for (auto const &b : blocks_)
      total += b.used;
    return total;
  }

  // Checkpoint/rollback: `mark` records the current high-water mark, and
  // `reset_to` rewinds allocations made after it without touching earlier ones.
  std::size_t mark() const { return used(); }

  void reset_to(std::size_t mark) {
    std::size_t total = used();
    for (auto it = blocks_.rbegin(); it != blocks_.rend() && total > mark;
         ++it) {
      const std::size_t take = std::min(total - mark, it->used);
      it->used -= take;
      total -= take;
    }
  }

private:
  struct Block {
    char *data;
    std::size_t size;
    std::size_t used;
  };

  bool fits(std::size_t align, std::size_t bytes) const {
    auto const &b = blocks_.back();
    std::size_t off = (b.used + align - 1) & ~(align - 1);
    return off + bytes <= b.size;
  }

  void add_block(std::size_t align, std::size_t bytes) {
    std::size_t size = std::max(block_, bytes + align);
    char *data =
        static_cast<char *>(::operator new(size, std::align_val_t{alignment}));
    blocks_.push_back(Block{data, size, 0});
  }

  std::size_t block_;
  std::vector<Block> blocks_;
};

template <class F> auto with_arena(std::size_t block, F f) {
  Arena a(block);
  return f(a);
}

// Scoped use of a long-lived arena: allocations made inside `f` are reclaimed
// when `f` returns, while everything allocated before stays valid.
template <class F> auto with_arena_scope(Arena &a, F f) {
  const std::size_t mark = a.mark();
  auto guard = defer([&a, mark] { a.reset_to(mark); });
  return f(a);
}

} // namespace fp

#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
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

} // namespace fp

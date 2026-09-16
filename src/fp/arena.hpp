#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace fp {

class Arena {
public:
  explicit Arena(size_t block = 64 * 1024) : block_(block) {
    buf_.reserve(block);
  }

  template <class T> T *alloc(size_t n = 1) {
    constexpr size_t align = alignof(T);
    size_t off = (offset_ + align - 1) & ~(align - 1);
    size_t bytes = n * sizeof(T);
    if (off + bytes > buf_.size())
      buf_.resize(buf_.size() + std::max(block_, bytes));
    T *p = reinterpret_cast<T *>(buf_.data() + off);
    offset_ = off + bytes;
    return p;
  }

  template <class T, class... Ts> T *make(Ts &&...ts) {
    T *p = alloc<T>();
    new (p) T(std::forward<Ts>(ts)...);
    return p;
  }

  void reset() { offset_ = 0; }
  size_t used() const { return offset_; }

private:
  size_t block_;
  size_t offset_ = 0;
  std::vector<char> buf_;
};

template <class F> auto with_arena(size_t block, F f) {
  Arena a(block);
  return f(a);
}

} // namespace fp

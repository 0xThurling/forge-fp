#pragma once
#include "result.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace fp {

// --- movement and access ----------------------------------------------------
// Named, constexpr one-liners. `ptr` is address-of, `ref`/`deref` dereference;
// none of them null-check (that is what optional/Box/Shared are for).

template <class T> constexpr std::remove_reference_t<T> &&move(T &&x) noexcept {
  return static_cast<std::remove_reference_t<T> &&>(x);
}

template <class T>
constexpr T &&forward(std::remove_reference_t<T> &x) noexcept {
  return static_cast<T &&>(x);
}

template <class T>
constexpr T &&forward(std::remove_reference_t<T> &&x) noexcept {
  static_assert(!std::is_lvalue_reference_v<T>,
                "cannot forward an lvalue as an rvalue");
  return static_cast<T &&>(x);
}

template <class T, class U> T exchange(T &obj, U &&replacement) {
  T old = std::move(obj);
  obj = std::forward<U>(replacement);
  return old;
}

template <class T> constexpr T *ptr(T &x) noexcept {
  return std::addressof(x);
}

template <class T> constexpr T const *ptr(T const &x) noexcept {
  return std::addressof(x);
}

template <class T> constexpr T &ref(T *p) noexcept { return *p; }

template <class T> constexpr T &deref(T *p) noexcept { return *p; }

template <class T> constexpr T const &as_const(T &x) noexcept { return x; }

template <class T> void as_const(T const &&) = delete;

// --- object lifetime --------------------------------------------------------

template <class T, class... Ts> T *construct_at(T *p, Ts &&...ts) {
  return ::new (static_cast<void *>(p)) T(std::forward<Ts>(ts)...);
}

template <class T> void destroy_at(T *p) { p->~T(); }

template <class T> void destroy(std::span<T> s) {
  for (auto &x : s)
    x.~T();
}

namespace detail {
#if defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
inline constexpr std::size_t default_new_alignment =
    __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
inline constexpr std::size_t default_new_alignment = alignof(std::max_align_t);
#endif
} // namespace detail

// --- owning contiguous raw block -------------------------------------------
// One allocation, one free, no per-element overhead. Elements are raw memory:
// use construct_at/destroy for non-trivial payloads, or just assign for
// trivially destructible ones. Buffer is a range, so fp combinators and
// fp::simd work on it directly.

template <class T> class Buffer {
  static_assert(std::is_trivially_destructible_v<T>,
                "Buffer is raw storage; use Box<T> for non-trivial types");

public:
  using value_type = T;
  using iterator = T *;
  using const_iterator = T const *;

  Buffer() noexcept = default;

  Buffer(Buffer &&other) noexcept
      : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }

  Buffer &operator=(Buffer &&other) noexcept {
    if (this != &other) {
      free_memory();
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  Buffer(Buffer const &) = delete;
  Buffer &operator=(Buffer const &) = delete;

  ~Buffer() { free_memory(); }

  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  T *data() noexcept { return data_; }
  T const *data() const noexcept { return data_; }

  T &operator[](std::size_t i) noexcept { return data_[i]; }
  T const &operator[](std::size_t i) const noexcept { return data_[i]; }

  std::span<T> span() noexcept { return {data_, size_}; }
  std::span<T const> span() const noexcept { return {data_, size_}; }

  T *begin() noexcept { return data_; }
  T *end() noexcept { return data_ + size_; }
  T const *begin() const noexcept { return data_; }
  T const *end() const noexcept { return data_ + size_; }

  void fill(T const &value) { std::fill_n(data_, size_, value); }

  [[nodiscard]] Result<Buffer<T>> clone() const { return copy_of(span()); }

  // New block of size `n`; copies min(size(), n) elements.
  [[nodiscard]] Result<Buffer<T>> resized(std::size_t n) const {
    auto out = alloc(n);
    if (!out.is_ok())
      return out;
    std::copy_n(data_, std::min(size_, n), out.value().data_);
    return out;
  }

  [[nodiscard]] static Result<Buffer<T>> alloc(std::size_t n) {
    Buffer b;
    if (n == 0)
      return ok(std::move(b));
    try {
      b.data_ = static_cast<T *>(allocate(n * sizeof(T)));
      b.size_ = n;
    } catch (std::bad_alloc const &) {
      return err<Buffer<T>>("allocation failed");
    }
    return ok(std::move(b));
  }

  [[nodiscard]] static Result<Buffer<T>> zeros(std::size_t n) {
    auto out = alloc(n);
    if (!out.is_ok())
      return out;
    std::fill_n(out.value().data_, n, T{});
    return out;
  }

  [[nodiscard]] static Result<Buffer<T>> copy_of(std::span<T const> src) {
    auto out = alloc(src.size());
    if (!out.is_ok())
      return out;
    std::copy_n(src.data(), src.size(), out.value().data_);
    return out;
  }

  [[nodiscard]] static Result<Buffer<T>> from(std::initializer_list<T> init) {
    auto out = alloc(init.size());
    if (!out.is_ok())
      return out;
    std::copy(init.begin(), init.end(), out.value().data_);
    return out;
  }

  // Give up ownership; the caller must free the block (explicit leak).
  T *release() noexcept {
    T *p = data_;
    data_ = nullptr;
    size_ = 0;
    return p;
  }

  // Take ownership of a raw block previously produced by allocate/release.
  static Buffer<T> adopt(T *raw, std::size_t n) noexcept {
    Buffer b;
    b.data_ = raw;
    b.size_ = n;
    return b;
  }

private:
  static void *allocate(std::size_t bytes) {
    if constexpr (alignof(T) > detail::default_new_alignment)
      return ::operator new(bytes, std::align_val_t{alignof(T)});
    else
      return ::operator new(bytes);
  }

  static void deallocate(void *p, std::size_t bytes) noexcept {
    if constexpr (alignof(T) > detail::default_new_alignment)
      ::operator delete(p, bytes, std::align_val_t{alignof(T)});
    else
      ::operator delete(p, bytes);
  }

  void free_memory() noexcept {
    if (data_)
      deallocate(data_, size_ * sizeof(T));
    data_ = nullptr;
    size_ = 0;
  }

  T *data_ = nullptr;
  std::size_t size_ = 0;
};

// --- runtime-aligned raw block ----------------------------------------------
// `Buffer<T>` aligns to `alignof(T)`, which is fixed at compile time. Device
// I/O and SIMD/GPU staging need an alignment that is only known at run time —
// the logical block size of a file — so this is the byte-oriented counterpart.

class AlignedBuffer {
public:
  AlignedBuffer() noexcept = default;

  AlignedBuffer(AlignedBuffer &&other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)),
        alignment_(std::exchange(other.alignment_, 0)) {}

  AlignedBuffer &operator=(AlignedBuffer &&other) noexcept {
    if (this != &other) {
      free_memory();
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
      alignment_ = std::exchange(other.alignment_, 0);
    }
    return *this;
  }

  AlignedBuffer(AlignedBuffer const &) = delete;
  AlignedBuffer &operator=(AlignedBuffer const &) = delete;

  ~AlignedBuffer() { free_memory(); }

  // `alignment` must be a power of two; `size` is rounded up to it, so
  // `size() % alignment() == 0` always holds (alignment is 0 only for a
  // moved-from buffer). An empty buffer allocates nothing.
  [[nodiscard]] static Result<AlignedBuffer> alloc(std::size_t size,
                                                   std::size_t alignment = 64) {
    AlignedBuffer b;
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
      return err<AlignedBuffer>("alignment must be a power of two");

    b.alignment_ = alignment;
    const std::size_t rounded = (size + alignment - 1) & ~(alignment - 1);
    if (rounded == 0)
      return ok(std::move(b));

    try {
      b.data_ = static_cast<std::byte *>(allocate(rounded, alignment));
      b.size_ = rounded;
    } catch (std::bad_alloc const &) {
      return err<AlignedBuffer>("allocation failed");
    }
    return ok(std::move(b));
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::size_t alignment() const noexcept { return alignment_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  std::byte *data() noexcept { return data_; }
  std::byte const *data() const noexcept { return data_; }

  std::span<std::byte> span() noexcept { return {data_, size_}; }
  std::span<std::byte const> span() const noexcept { return {data_, size_}; }

  std::byte *begin() noexcept { return data_; }
  std::byte *end() noexcept { return data_ + size_; }
  std::byte const *begin() const noexcept { return data_; }
  std::byte const *end() const noexcept { return data_ + size_; }

  void fill(std::byte value) { std::fill_n(data_, size_, value); }

  [[nodiscard]] Result<AlignedBuffer> clone() const {
    auto out = alloc(size_, alignment_ == 0 ? 64 : alignment_);
    if (!out.is_ok())
      return out;
    std::copy_n(data_, size_, out.value().data_);
    return out;
  }

private:
  static void *allocate(std::size_t bytes, std::size_t alignment) {
    return ::operator new(bytes, std::align_val_t{alignment});
  }

  static void deallocate(void *p, std::size_t bytes,
                         std::size_t alignment) noexcept {
    ::operator delete(p, bytes, std::align_val_t{alignment});
  }

  void free_memory() noexcept {
    if (data_)
      deallocate(data_, size_, alignment_);
    data_ = nullptr;
    size_ = 0;
    alignment_ = 0;
  }

  std::byte *data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t alignment_ = 0;
};

// --- single object ----------------------------------------------------------

template <class T> class Box {
public:
  Box() noexcept = default;
  explicit Box(T *p) noexcept : p_(p) {}

  Box(Box &&other) noexcept : p_(std::exchange(other.p_, nullptr)) {}

  Box &operator=(Box &&other) noexcept {
    if (this != &other) {
      reset();
      p_ = std::exchange(other.p_, nullptr);
    }
    return *this;
  }

  Box(Box const &) = delete;
  Box &operator=(Box const &) = delete;

  ~Box() { reset(); }

  T &operator*() const { return *p_; }
  T *operator->() const noexcept { return p_; }
  T *get() const noexcept { return p_; }
  explicit operator bool() const noexcept { return p_ != nullptr; }

  T *release() noexcept { return std::exchange(p_, nullptr); }

  void reset(T *p = nullptr) noexcept {
    if (p_)
      delete p_;
    p_ = p;
  }

  template <class... Ts> static Result<Box<T>> make(Ts &&...ts) {
    try {
      return ok(Box<T>(new T(std::forward<Ts>(ts)...)));
    } catch (std::bad_alloc const &) {
      return err<Box<T>>("allocation failed");
    }
  }

private:
  T *p_ = nullptr;
};

// --- shared ownership -------------------------------------------------------

template <class T> using Shared = std::shared_ptr<T>;

template <class T, class... Ts> Result<Shared<T>> make_shared(Ts &&...ts) {
  try {
    return ok(std::make_shared<T>(std::forward<Ts>(ts)...));
  } catch (std::bad_alloc const &) {
    return err<Shared<T>>("allocation failed");
  }
}

template <class T> Shared<T> share(Box<T> box) {
  return Shared<T>(box.release());
}

// --- scoped allocation: the owner cannot escape -----------------------------

// Allocates n elements, calls f(span), frees the block, and returns f's value.
// Allocation failure is returned as an error; f is not called then.
template <class T, class F>
auto with_buffer(std::size_t n, F f)
    -> Result<std::invoke_result_t<F, std::span<T>>> {
  using R = std::invoke_result_t<F, std::span<T>>;
  auto buffer = Buffer<T>::alloc(n);
  if (!buffer.is_ok())
    return err<R>(buffer.error());
  if constexpr (std::is_void_v<R>) {
    f(buffer.value().span());
    return ok<void>();
  } else {
    return ok(f(buffer.value().span()));
  }
}

template <class T, class F>
auto with_ptr(std::size_t n, F f)
    -> Result<std::invoke_result_t<F, T *, std::size_t>> {
  using R = std::invoke_result_t<F, T *, std::size_t>;
  auto buffer = Buffer<T>::alloc(n);
  if (!buffer.is_ok())
    return err<R>(buffer.error());
  if constexpr (std::is_void_v<R>) {
    f(buffer.value().data(), n);
    return ok<void>();
  } else {
    return ok(f(buffer.value().data(), n));
  }
}

// --- raw copying (no allocation) --------------------------------------------

template <class T> void copy_bytes(std::span<T> dst, std::span<T const> src) {
  std::copy_n(src.data(), src.size(), dst.data());
}

template <class T> void fill_bytes(std::span<T> dst, std::byte value) {
  std::memset(dst.data(), static_cast<unsigned char>(value), dst.size_bytes());
}

template <class T>
[[nodiscard]] Result<void> copy_into(std::span<T> dst, std::span<T const> src) {
  if (dst.size() < src.size())
    return err<void>("copy_into: destination too small");
  std::copy_n(src.data(), src.size(), dst.data());
  return ok<void>();
}

} // namespace fp

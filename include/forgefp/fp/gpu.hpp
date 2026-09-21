#pragma once
// GPU acceleration via SYCL — **opt-in**, like simd.hpp.
//
// When a SYCL implementation is available (`<sycl/sycl.hpp>` on the include
// path) the kernels below run on the default device; otherwise every function
// falls back to the CPU implementation, so the same code compiles and runs
// everywhere. See `GPU.md` for the design, phases, and the CPU-stub test.
//
// Phase 0/1: device detection, USM `Buffer<T>` (device + shared), elementwise
// kernels (`map_to`, `transform_inplace`, `map`, `axpy_inplace`,
// `softmax_rows`), and chunked device reductions (`reduce`, `dot`).

#include "forgefp/fp/inplace.hpp"
#include "forgefp/fp/linalg.hpp"
#include "forgefp/fp/numerics.hpp"
#include "forgefp/fp/ops.hpp"
#include "forgefp/fp/ranges.hpp"
#include "forgefp/fp/result.hpp"
#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <new>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Detection can be overridden by defining FP_GPU_SYCL before including this
// header (the CPU-stub test does exactly that via its include path).
#if !defined(FP_GPU_SYCL) && defined(__has_include)
#if __has_include(<sycl/sycl.hpp>)
#include <sycl/sycl.hpp>
#define FP_GPU_SYCL 1
#endif
#endif

// Local memory + work-group barriers power the tiled kernels (matmul, the
// work-group softmax). A real implementation provides them; the CPU stub
// cannot emulate barrier semantics faithfully, so it takes the portable path.
// Define FP_GPU_NO_GROUP_ALGORITHMS to force the portable path on a real
// toolchain as well.
#if defined(FP_GPU_SYCL) && !defined(FP_GPU_NO_GROUP_ALGORITHMS) && \
    !defined(SYCL_IMPLEMENTATION_FORGEFP_STUB)
#define FP_GPU_GROUP_ALGORITHMS 1
#endif

namespace fp::gpu {

// True when the translation unit was compiled with SYCL available.
inline constexpr bool available =
#ifdef FP_GPU_SYCL
    true;
#else
    false;
#endif

struct DeviceInfo {
  std::string name;
  std::string vendor;
  bool is_cpu = false;
  bool is_gpu = false;
  std::size_t global_memory = 0;
};

// The SYCL implementation this translation unit was compiled against
// (compile-time detection; the CPU stub reports itself).
inline constexpr char const *implementation_name =
#if defined(SYCL_IMPLEMENTATION_FORGEFP_STUB)
    "ForgeFP CPU stub";
#elif defined(SYCL_IMPLEMENTATION_ACPP) || defined(ACPP_VERSION_MAJOR)
    "AdaptiveCpp";
#elif defined(SYCL_IMPLEMENTATION_ONEAPI) || defined(__INTEL_LLVM_COMPILER)
    "oneAPI DPC++";
#elif defined(TRISYCL) || defined(__TRISYCL__)
    "triSYCL";
#elif defined(SIMSYCL)
    "SimSYCL";
#elif defined(FP_GPU_SYCL)
    "SYCL (unknown implementation)";
#else
    "none (CPU fallback)";
#endif

struct BackendInfo {
  std::string implementation;
  std::string platform;
  std::string device;
  std::string driver_version;
};

namespace detail {
#ifdef FP_GPU_SYCL
// Device selection: prefer a GPU, then a CPU. Override at compile time with
// FP_GPU_DEVICE_CPU / FP_GPU_DEVICE_ACCELERATOR (e.g. when testing the CPU
// backend of DPC++ on a machine that also has a GPU).
//
// The queue is in-order so kernels and copies observe each other in
// submission order; the functional API is synchronous, and this keeps it
// deterministic.
inline sycl::queue &queue() {
  static sycl::queue q{
      [](sycl::device const &device) {
#if defined(FP_GPU_DEVICE_CPU)
        return device.is_cpu() ? 100 : (device.is_gpu() ? 50 : 1);
#elif defined(FP_GPU_DEVICE_ACCELERATOR)
        return device.is_accelerator() ? 100
                                       : (device.is_gpu() ? 90
                                                          : (device.is_cpu() ? 50
                                                                             : 1));
#else
        return device.is_gpu() ? 100 : (device.is_cpu() ? 50 : 1);
#endif
      },
      sycl::property::queue::in_order{}};
  return q;
}

inline constexpr std::size_t work_group_size = 256;

// One-dimensional launch over `n` items with an explicit work-group size. The
// trailing group is masked with `index < n`. An `nd_range` launch hands the
// kernel an `nd_item` (not an `id`), which is what the SYCL 2020 spec requires
// and what real implementations type-check.
template <class Kernel> void launch_1d(std::size_t n, Kernel kernel) {
  if (n == 0)
    return;
  const std::size_t groups = (n + work_group_size - 1) / work_group_size;
  queue().parallel_for(
      sycl::nd_range<1>(sycl::range<1>(groups * work_group_size),
                        sycl::range<1>(work_group_size)),
      [n, kernel](sycl::nd_item<1> item) {
        const std::size_t index = item.get_global_id(0);
        if (index < n)
          kernel(index);
      });
}

// USM allocation, over-aligned when the type needs it.
template <class T> T *allocate(std::size_t n) {
  if constexpr (alignof(T) > alignof(std::max_align_t))
    return sycl::aligned_alloc_device<T>(alignof(T), n, queue());
  else
    return sycl::malloc_device<T>(n, queue());
}

template <class T> T *allocate_shared(std::size_t n) {
  if constexpr (alignof(T) > alignof(std::max_align_t))
    return sycl::aligned_alloc_shared<T>(alignof(T), n, queue());
  else
    return sycl::malloc_shared<T>(n, queue());
}

template <class T> void deallocate(T *p) { sycl::free(p, queue()); }

// Pinned host memory: DMA-able by the device without a driver bounce buffer.
template <class T> T *allocate_host(std::size_t n) {
  if constexpr (alignof(T) > alignof(std::max_align_t))
    return sycl::aligned_alloc_host<T>(alignof(T), n, queue());
  else
    return sycl::malloc_host<T>(n, queue());
}
#endif

// Elements one work-item reduces in the chunked `reduce`/`dot`.
inline constexpr std::size_t reduce_chunk_size = 1024;
} // namespace detail

// True when the selected device can actually back `Buffer` (USM device
// allocations). When false, every algorithm falls back to the CPU path.
inline bool usable() {
#ifdef FP_GPU_SYCL
  try {
    return detail::queue().get_device().has(
        sycl::aspect::usm_device_allocations);
  } catch (sycl::exception const &) {
    return false;
  }
#else
  return false;
#endif
}

inline Result<DeviceInfo> default_device_info() {
#ifdef FP_GPU_SYCL
  try {
    const auto device = detail::queue().get_device();
    DeviceInfo info;
    info.name = device.get_info<sycl::info::device::name>();
    info.vendor = device.get_info<sycl::info::device::vendor>();
    info.is_cpu = device.is_cpu();
    info.is_gpu = device.is_gpu();
    info.global_memory =
        device.get_info<sycl::info::device::global_mem_size>();
    return ok(std::move(info));
  } catch (sycl::exception const &e) {
    return err<DeviceInfo>(std::string("sycl: ") + e.what());
  }
#else
  return err<DeviceInfo>("no SYCL device: ForgeFP was built without SYCL");
#endif
}

inline Result<BackendInfo> backend_info() {
#ifdef FP_GPU_SYCL
  try {
    const auto device = detail::queue().get_device();
    BackendInfo info;
    info.implementation = implementation_name;
    info.platform =
        device.get_platform().get_info<sycl::info::platform::name>();
    info.device = device.get_info<sycl::info::device::name>();
    info.driver_version =
        device.get_info<sycl::info::device::driver_version>();
    return ok(std::move(info));
  } catch (sycl::exception const &e) {
    return err<BackendInfo>(std::string("sycl: ") + e.what());
  }
#else
  return err<BackendInfo>("no SYCL device: ForgeFP was built without SYCL");
#endif
}

// Pinned host staging. `sycl::malloc_host` memory is directly DMA-able by the
// device, which roughly doubles transfer bandwidth versus pageable memory
// (measured 12.6 vs 5.1 GB/s at 16 MiB) — but the allocation itself is
// expensive (~2.6 ms per malloc_host/free pair on the reference machine), so
// this type exists to be allocated once and reused:
//
//   auto staging = fp::gpu::HostBuffer<float>::alloc(1 << 22);
//   dev.copy_from(staging.value());                 // H2D: one DMA, no copy
//   dev.to_host(staging.value());                   // D2H: no allocation
//   std::span<float> values = staging.value().span();
//
// Fill the staging block *directly* (`staging.span()`) before an H2D transfer:
// copying an existing std::vector into staging first costs more than the
// pageable path it was meant to avoid (measured in GPU.md's phase 1b table).
// Without SYCL it is an ordinary host vector, so the same code compiles.
template <class T> class HostBuffer {
public:
  HostBuffer() noexcept = default;

  HostBuffer(HostBuffer &&other) noexcept { move_from(std::move(other)); }

  HostBuffer &operator=(HostBuffer &&other) noexcept {
    if (this != &other) {
      release();
      move_from(std::move(other));
    }
    return *this;
  }

  HostBuffer(HostBuffer const &) = delete;
  HostBuffer &operator=(HostBuffer const &) = delete;

  ~HostBuffer() { release(); }

  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  // True when the block is device-accessible pinned memory (always false in
  // the CPU fallback, where it is a plain vector).
  bool is_pinned() const noexcept {
#ifdef FP_GPU_SYCL
    return true;
#else
    return false;
#endif
  }

  T *data() noexcept {
#ifdef FP_GPU_SYCL
    return data_;
#else
    return data_.data();
#endif
  }

  T const *data() const noexcept {
#ifdef FP_GPU_SYCL
    return data_;
#else
    return data_.data();
#endif
  }

  std::span<T> span() noexcept { return {data(), size_}; }
  std::span<T const> span() const noexcept { return {data(), size_}; }

  static Result<HostBuffer<T>> alloc(std::size_t n) {
#ifdef FP_GPU_SYCL
    if (n == 0)
      return ok(HostBuffer<T>{});
    try {
      T *p = detail::allocate_host<T>(n);
      if (p == nullptr)
        return err<HostBuffer<T>>("sycl: pinned allocation failed");
      HostBuffer b;
      b.data_ = p;
      b.size_ = n;
      return ok(std::move(b));
    } catch (sycl::exception const &e) {
      return err<HostBuffer<T>>(std::string("sycl: ") + e.what());
    }
#else
    HostBuffer b;
    try {
      b.data_.resize(n);
    } catch (std::bad_alloc const &) {
      return err<HostBuffer<T>>("allocation failed");
    }
    b.size_ = n;
    return ok(std::move(b));
#endif
  }

private:
  void release() noexcept {
#ifdef FP_GPU_SYCL
    if (data_ != nullptr)
      detail::deallocate(data_);
    data_ = nullptr;
#else
    data_.clear();
#endif
    size_ = 0;
  }

  void move_from(HostBuffer &&other) noexcept {
#ifdef FP_GPU_SYCL
    data_ = other.data_;
    other.data_ = nullptr;
#else
    data_ = std::move(other.data_);
#endif
    size_ = other.size_;
    other.size_ = 0;
  }

#ifdef FP_GPU_SYCL
  T *data_ = nullptr;
  std::size_t size_ = 0;
#else
  std::vector<T> data_;
  std::size_t size_ = 0;
#endif
};

// Owning contiguous device block (USM under SYCL; a host vector otherwise).
// Move-only; factories return `Result`. `alloc_shared` is the small-buffer
// path whose `data()` is host-accessible.
template <class T> class Buffer {
public:
  Buffer() noexcept = default;

  Buffer(Buffer &&other) noexcept { move_from(std::move(other)); }

  Buffer &operator=(Buffer &&other) noexcept {
    if (this != &other) {
      release();
      move_from(std::move(other));
    }
    return *this;
  }

  Buffer(Buffer const &) = delete;
  Buffer &operator=(Buffer const &) = delete;

  ~Buffer() { release(); }

  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  // Host-accessible for shared buffers (and always in the CPU fallback).
  bool host_accessible() const noexcept {
#ifdef FP_GPU_SYCL
    return shared_;
#else
    return true;
#endif
  }

  T *data() noexcept {
#ifdef FP_GPU_SYCL
    return data_;
#else
    return data_.data();
#endif
  }

  T const *data() const noexcept {
#ifdef FP_GPU_SYCL
    return data_;
#else
    return data_.data();
#endif
  }

  Result<void> fill(T const &value) {
#ifdef FP_GPU_SYCL
    if (size_ == 0)
      return ok<void>();
    try {
      detail::queue().fill(data_, value, size_);
      detail::queue().wait_and_throw();
      return ok<void>();
    } catch (sycl::exception const &e) {
      return err<void>(std::string("sycl: ") + e.what());
    }
#else
    std::fill(data_.begin(), data_.end(), value);
    return ok<void>();
#endif
  }

  // Device allocation (USM device memory under SYCL).
  static Result<Buffer<T>> alloc(std::size_t n) { return make(n, false); }

  // Shared allocation: `data()` is host-accessible under SYCL.
  static Result<Buffer<T>> alloc_shared(std::size_t n) { return make(n, true); }

  static Result<Buffer<T>> from_host(std::span<T const> src) {
#ifdef FP_GPU_SYCL
    auto b = alloc(src.size());
    if (!b.is_ok() || src.empty())
      return b;
    try {
      detail::queue().memcpy(b.value().data(), src.data(),
                             src.size() * sizeof(T));
      detail::queue().wait_and_throw();
    } catch (sycl::exception const &e) {
      return err<Buffer<T>>(std::string("sycl: ") + e.what());
    }
    return b;
#else
    Buffer b;
    try {
      b.data_.assign(src.begin(), src.end());
    } catch (std::bad_alloc const &) {
      return err<Buffer<T>>("allocation failed");
    }
    b.size_ = src.size();
    return ok(std::move(b));
#endif
  }

  Result<std::vector<T>> to_host() const {
#ifdef FP_GPU_SYCL
    std::vector<T> out(size_);
    try {
      if (size_ > 0) {
        detail::queue().memcpy(out.data(), data_, size_ * sizeof(T));
        detail::queue().wait_and_throw();
      }
    } catch (sycl::exception const &e) {
      return err<std::vector<T>>(std::string("sycl: ") + e.what());
    }
    return ok(std::move(out));
#else
    return ok(std::vector<T>(data_.begin(), data_.end()));
#endif
  }

  // Read back into `staging` (no allocation); read the values through
  // `staging.span()`.
  Result<void> to_host(HostBuffer<T> &staging) const {
    if (staging.size() < size_)
      return err<void>("gpu::to_host: staging buffer too small");
#ifdef FP_GPU_SYCL
    try {
      if (size_ > 0) {
        detail::queue().memcpy(staging.data(), data_, size_ * sizeof(T));
        detail::queue().wait_and_throw();
      }
    } catch (sycl::exception const &e) {
      return err<void>(std::string("sycl: ") + e.what());
    }
#else
    std::copy(data_.begin(), data_.end(), staging.data());
#endif
    return ok<void>();
  }

  // Copy pinned staging into this buffer. This is the staging path with no
  // extra host copy: fill the staging block directly (`staging.span()`), then
  // move it once. Use it when the producer can write into pinned memory.
  Result<void> copy_from(HostBuffer<T> const &staging) {
    if (staging.size() < size_)
      return err<void>("gpu::copy_from: staging buffer too small");
#ifdef FP_GPU_SYCL
    try {
      if (size_ > 0) {
        detail::queue().memcpy(data_, staging.data(), size_ * sizeof(T));
        detail::queue().wait_and_throw();
      }
    } catch (sycl::exception const &e) {
      return err<void>(std::string("sycl: ") + e.what());
    }
#else
    std::copy(staging.data(), staging.data() + size_, data_.data());
#endif
    return ok<void>();
  }

  Result<Buffer<T>> clone() const {
    auto host = to_host();
    if (!host.is_ok())
      return err<Buffer<T>>(host.error());
    return from_host(host.value());
  }

private:
  static Result<Buffer<T>> make(std::size_t n, bool shared) {
#ifdef FP_GPU_SYCL
    if (n == 0)
      return ok(Buffer<T>{});
    try {
      T *p = shared ? detail::allocate_shared<T>(n) : detail::allocate<T>(n);
      if (p == nullptr)
        return err<Buffer<T>>("sycl: device allocation failed");
      Buffer b;
      b.data_ = p;
      b.size_ = n;
      b.shared_ = shared;
      return ok(std::move(b));
    } catch (sycl::exception const &e) {
      return err<Buffer<T>>(std::string("sycl: ") + e.what());
    }
#else
    (void)shared;
    Buffer b;
    try {
      b.data_.resize(n);
    } catch (std::bad_alloc const &) {
      return err<Buffer<T>>("allocation failed");
    }
    b.size_ = n;
    return ok(std::move(b));
#endif
  }

  void release() noexcept {
#ifdef FP_GPU_SYCL
    if (data_ != nullptr)
      detail::deallocate(data_);
    data_ = nullptr;
    shared_ = false;
#else
    data_.clear();
#endif
    size_ = 0;
  }

  void move_from(Buffer &&other) noexcept {
#ifdef FP_GPU_SYCL
    data_ = other.data_;
    other.data_ = nullptr;
    shared_ = other.shared_;
    other.shared_ = false;
#else
    data_ = std::move(other.data_);
#endif
    size_ = other.size_;
    other.size_ = 0;
  }

#ifdef FP_GPU_SYCL
  T *data_ = nullptr;
  bool shared_ = false;
  std::size_t size_ = 0;
#else
  std::vector<T> data_;
  std::size_t size_ = 0;
#endif
};

// Reusable device workspace for `reduce`/`dot`. Both need a partials buffer on
// the device and a small read-back; allocating them per call costs more than
// the reduction itself (`sycl::free` alone is ~0.6 ms and a pinned
// `malloc_host` + free ~2.6 ms on the reference machine — see GPU.md's phase
// 1b numbers). Declare one and pass it to every call:
//
//   fp::gpu::Scratch<float> scratch;
//   for (...) total = fp::gpu::reduce(buf, 0.0f, scratch);
//
// The buffers grow to fit on first use and are reused afterwards.
template <class T> struct Scratch {
  Buffer<T> partials;     // device partial sums
  HostBuffer<T> readback; // pinned read-back of the partials
};

namespace detail {
template <class T>
Result<void> ensure_scratch(Scratch<T> &scratch, std::size_t chunks) {
  if (scratch.partials.size() < chunks) {
    auto p = Buffer<T>::alloc(chunks);
    if (!p.is_ok())
      return err<void>(p.error());
    scratch.partials = std::move(p.value());
  }
  if (scratch.readback.size() < chunks) {
    auto r = HostBuffer<T>::alloc(chunks);
    if (!r.is_ok())
      return err<void>(r.error());
    scratch.readback = std::move(r.value());
  }
  return ok<void>();
}
} // namespace detail

// --- elementwise kernels ----------------------------------------------------

// dst[i] = f(src[i]) over the common prefix; dst must be at least as large.
template <class T, class F>
Result<void> map_to(Buffer<T> &dst, Buffer<T> const &src, F f) {
  if (dst.size() < src.size())
    return err<void>("gpu::map_to: destination too small");
  if (src.empty())
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *d = dst.data();
    T const *s = src.data();
    const std::size_t n = src.size();
    detail::launch_1d(n, [=](std::size_t i) { d[i] = f(s[i]); });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  std::span<T const> source(src.data(), src.size());
  std::span<T> target(dst.data(), src.size());
  fp::map_to(source, target, f);
  return ok<void>();
#endif
}

// buf[i] = f(buf[i]).
template <class T, class F>
Result<void> transform_inplace(Buffer<T> &buf, F f) {
  if (buf.empty())
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *d = buf.data();
    const std::size_t n = buf.size();
    detail::launch_1d(n, [=](std::size_t i) { d[i] = f(d[i]); });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  std::span<T> data(buf.data(), buf.size());
  fp::transform_inplace(data, f);
  return ok<void>();
#endif
}

// buf[i] = f(i, buf[i]). The index-aware kernel exists for the operations that
// depend on position rather than value — attention's causal mask, positional
// encodings, per-row scaling — where the lambda recovers (row, col) from `i`.
template <class T, class F>
Result<void> transform_inplace_indexed(Buffer<T> &buf, F f) {
  if (buf.empty())
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *d = buf.data();
    const std::size_t n = buf.size();
    detail::launch_1d(n, [=](std::size_t i) { d[i] = f(i, d[i]); });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  std::span<T> data(buf.data(), buf.size());
  fp::for_each_index(data,
                     [&](std::size_t i, T &x) { x = f(i, x); });
  return ok<void>();
#endif
}

// Host-in, host-out map. Under SYCL this round-trips through device memory;
// without SYCL it is plain fp::map.
template <std::ranges::contiguous_range R, class F>
auto map(R const &src, F f)
    -> Result<std::vector<std::invoke_result_t<
        F, std::ranges::range_value_t<R>>>> {
  using T = std::ranges::range_value_t<R>;
  const std::span<T const> input{std::ranges::data(src),
                                 std::ranges::size(src)};
#ifdef FP_GPU_SYCL
  using U = std::invoke_result_t<F, T>;
  auto in = Buffer<T>::from_host(input);
  if (!in.is_ok())
    return err<std::vector<U>>(in.error());
  auto out = Buffer<U>::alloc(input.size());
  if (!out.is_ok())
    return err<std::vector<U>>(out.error());
  try {
    T const *s = in.value().data();
    U *d = out.value().data();
    const std::size_t n = input.size();
    detail::launch_1d(n, [=](std::size_t i) { d[i] = f(s[i]); });
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<std::vector<U>>(std::string("sycl: ") + e.what());
  }
  return out.value().to_host();
#else
  return ok(fp::map(input, f));
#endif
}

// y[i] += a * x[i] over the common prefix (the optimizer update kernel).
template <class T>
Result<void> axpy_inplace(Buffer<T> &y, T a, Buffer<T> const &x) {
  if (y.size() < x.size())
    return err<void>("gpu::axpy_inplace: size mismatch");
  if (x.empty())
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *dy = y.data();
    T const *dx = x.data();
    const std::size_t n = x.size();
    detail::launch_1d(n, [=](std::size_t i) { dy[i] += a * dx[i]; });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  std::span<T> target(y.data(), x.size());
  std::span<T const> source(x.data(), x.size());
  fp::zip_transform_inplace(target, source,
                            [a](T yi, T xi) { return yi + a * xi; });
  return ok<void>();
#endif
}

// a[i] = f(a[i], b[i]) over the common prefix; `a` must be at least as large.
// The general two-input update (optimizer moments, gated activations, losses).
template <class T, class F>
Result<void> zip_transform_inplace(Buffer<T> &a, Buffer<T> const &b, F f) {
  if (a.size() < b.size())
    return err<void>("gpu::zip_transform_inplace: size mismatch");
  if (b.empty())
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *da = a.data();
    T const *db = b.data();
    const std::size_t n = b.size();
    detail::launch_1d(n, [=](std::size_t i) { da[i] = f(da[i], db[i]); });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  std::span<T> target(a.data(), b.size());
  std::span<T const> source(b.data(), b.size());
  fp::zip_transform_inplace(target, source, f);
  return ok<void>();
#endif
}

// a[i] = f(a[i], b[i], c[i]) over the shortest prefix. The three-input update
// (AdamW's `w -= lr * m / (sqrt(v) + eps)` without a temporary buffer).
template <class T, class F>
Result<void> zip3_transform_inplace(Buffer<T> &a, Buffer<T> const &b,
                                    Buffer<T> const &c, F f) {
  const std::size_t n = std::min(b.size(), c.size());
  if (a.size() < n)
    return err<void>("gpu::zip3_transform_inplace: size mismatch");
  if (n == 0)
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *da = a.data();
    T const *db = b.data();
    T const *dc = c.data();
    detail::launch_1d(n, [=](std::size_t i) { da[i] = f(da[i], db[i], dc[i]); });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  std::span<T> target(a.data(), n);
  fp::zip3_for_each(target, std::span<T const>(b.data(), n),
                    std::span<T const>(c.data(), n),
                    [&](T &x, T const &y, T const &z) { x = f(x, y, z); });
  return ok<void>();
#endif
}

// Row-wise stable softmax, in place. `rows * cols` must equal `size()`.
template <std::floating_point T>
Result<void> softmax_rows(Buffer<T> &logits, std::size_t rows,
                          std::size_t cols) {
  if (rows * cols != logits.size())
    return err<void>("gpu::softmax_rows: shape mismatch");
  if (logits.empty())
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *data = logits.data();
    detail::launch_1d(rows, [=](std::size_t r) {
      T *row = data + r * cols;
      T m = row[0];
      for (std::size_t j = 1; j < cols; ++j)
        m = sycl::fmax(m, row[j]);
      T sum = T{};
      for (std::size_t j = 0; j < cols; ++j) {
        row[j] = sycl::exp(row[j] - m);
        sum += row[j];
      }
      if (sum > T{})
        for (std::size_t j = 0; j < cols; ++j)
          row[j] /= sum;
    });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  T *data = logits.data();
  for (std::size_t r = 0; r < rows; ++r) {
    std::span<T> row(data + r * cols, cols);
    const auto probs = fp::softmax(row);
    std::copy(probs.begin(), probs.end(), row.begin());
  }
  return ok<void>();
#endif
}

// Work-group-per-row softmax: one work-group reduces a whole row (max, then
// sum) through local memory, which is ~an order of magnitude faster than the
// portable per-row kernel when the row fits in a work-group — the attention
// case (rows = tokens, cols = keys). Without group algorithms (the CPU stub,
// or FP_GPU_NO_GROUP_ALGORITHMS) it forwards to `softmax_rows`.
template <std::floating_point T>
Result<void> softmax_rows_wg(Buffer<T> &logits, std::size_t rows,
                             std::size_t cols) {
  if (rows * cols != logits.size())
    return err<void>("gpu::softmax_rows_wg: shape mismatch");
#if defined(FP_GPU_GROUP_ALGORITHMS)
  if (cols > detail::work_group_size)
    return err<void>("gpu::softmax_rows_wg: cols exceeds the work-group size");
  if (logits.empty())
    return ok<void>();
  try {
    T *data = logits.data();
    const std::size_t wg = detail::work_group_size;
    detail::queue().submit([&](sycl::handler &h) {
      sycl::local_accessor<T, 1> scratch(sycl::range<1>(wg), h);
      h.parallel_for(
          sycl::nd_range<1>(sycl::range<1>(rows * wg), sycl::range<1>(wg)),
          [=](sycl::nd_item<1> item) {
            const std::size_t row = item.get_group(0);
            const std::size_t lane = item.get_local_id(0);
            T *r = data + row * cols;
            const T neg_inf = -std::numeric_limits<T>::infinity();

            scratch[lane] = lane < cols ? r[lane] : neg_inf;
            sycl::group_barrier(item.get_group());
            for (std::size_t stride = wg / 2; stride > 0; stride /= 2) {
              if (lane < stride)
                scratch[lane] =
                    sycl::fmax(scratch[lane], scratch[lane + stride]);
              sycl::group_barrier(item.get_group());
            }
            const T m = scratch[0];
            sycl::group_barrier(item.get_group());

            const T local = lane < cols ? sycl::exp(r[lane] - m) : T{};
            scratch[lane] = local;
            sycl::group_barrier(item.get_group());
            for (std::size_t stride = wg / 2; stride > 0; stride /= 2) {
              if (lane < stride)
                scratch[lane] += scratch[lane + stride];
              sycl::group_barrier(item.get_group());
            }
            const T sum = scratch[0];
            if (lane < cols && sum > T{})
              r[lane] = local / sum;
          });
    });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  return softmax_rows(logits, rows, cols);
#endif
}

// --- row and column kernels -------------------------------------------------
// One work-item per output (row or column), with the other dimension as the
// serial inner loop. Device counterparts of fp::linalg's
// row_sums/row_means/col_sums and add_row_broadcast.

template <class T>
Result<Buffer<T>> row_sums(Buffer<T> const &buf, std::size_t rows,
                           std::size_t cols) {
  if (buf.size() != rows * cols)
    return err<Buffer<T>>("gpu::row_sums: shape mismatch");
  auto out = Buffer<T>::alloc(rows);
  if (!out.is_ok() || rows == 0)
    return out;
#ifdef FP_GPU_SYCL
  try {
    T const *src = buf.data();
    T *dst = out.value().data();
    detail::launch_1d(rows, [=](std::size_t r) {
      T const *row = src + r * cols;
      T acc = T{};
      for (std::size_t j = 0; j < cols; ++j)
        acc += row[j];
      dst[r] = acc;
    });
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<Buffer<T>>(std::string("sycl: ") + e.what());
  }
#else
  T const *src = buf.data();
  T *dst = out.value().data();
  for (std::size_t r = 0; r < rows; ++r)
    dst[r] =
        fp::fold_left(std::span<T const>(src + r * cols, cols), T{}, fp::plus);
#endif
  return out;
}

template <class T>
Result<Buffer<T>> row_means(Buffer<T> const &buf, std::size_t rows,
                            std::size_t cols) {
  if (buf.size() != rows * cols || cols == 0)
    return err<Buffer<T>>("gpu::row_means: shape mismatch");
  auto out = Buffer<T>::alloc(rows);
  if (!out.is_ok() || rows == 0)
    return out;
#ifdef FP_GPU_SYCL
  try {
    T const *src = buf.data();
    T *dst = out.value().data();
    detail::launch_1d(rows, [=](std::size_t r) {
      T const *row = src + r * cols;
      T acc = T{};
      for (std::size_t j = 0; j < cols; ++j)
        acc += row[j];
      dst[r] = acc / static_cast<T>(cols);
    });
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<Buffer<T>>(std::string("sycl: ") + e.what());
  }
#else
  T const *src = buf.data();
  T *dst = out.value().data();
  for (std::size_t r = 0; r < rows; ++r) {
    const T sum =
        fp::fold_left(std::span<T const>(src + r * cols, cols), T{}, fp::plus);
    dst[r] = sum / static_cast<T>(cols);
  }
#endif
  return out;
}

template <class T>
Result<Buffer<T>> col_sums(Buffer<T> const &buf, std::size_t rows,
                           std::size_t cols) {
  if (buf.size() != rows * cols)
    return err<Buffer<T>>("gpu::col_sums: shape mismatch");
  auto out = Buffer<T>::alloc(cols);
  if (!out.is_ok() || cols == 0)
    return out;
#ifdef FP_GPU_SYCL
  try {
    T const *src = buf.data();
    T *dst = out.value().data();
    detail::launch_1d(cols, [=](std::size_t c) {
      T acc = T{};
      for (std::size_t r = 0; r < rows; ++r)
        acc += src[r * cols + c];
      dst[c] = acc;
    });
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<Buffer<T>>(std::string("sycl: ") + e.what());
  }
#else
  T const *src = buf.data();
  T *dst = out.value().data();
  for (std::size_t c = 0; c < cols; ++c) {
    T acc = T{};
    for (std::size_t r = 0; r < rows; ++r)
      acc += src[r * cols + c];
    dst[c] = acc;
  }
#endif
  return out;
}

// buf[i][j] += bias[j] for every row (the bias-gradient shape).
template <class T>
Result<void> add_row_broadcast(Buffer<T> &buf, std::size_t rows,
                               std::size_t cols, Buffer<T> const &bias) {
  if (buf.size() != rows * cols || bias.size() != cols)
    return err<void>("gpu::add_row_broadcast: shape mismatch");
  if (buf.empty())
    return ok<void>();
#ifdef FP_GPU_SYCL
  try {
    T *dst = buf.data();
    T const *b = bias.data();
    detail::launch_1d(rows * cols,
                      [=](std::size_t i) { dst[i] += b[i % cols]; });
    detail::queue().wait_and_throw();
    return ok<void>();
  } catch (sycl::exception const &e) {
    return err<void>(std::string("sycl: ") + e.what());
  }
#else
  T *dst = buf.data();
  T const *b = bias.data();
  for (std::size_t r = 0; r < rows; ++r) {
    std::span<T> row(dst + r * cols, cols);
    fp::zip_transform_inplace(row, std::span<T const>(b, cols),
                              [](T x, T y) { return x + y; });
  }
  return ok<void>();
#endif
}

// --- reductions -------------------------------------------------------------
// Device path: each work-item sums one chunk of `detail::reduce_chunk_size`
// elements into a partials buffer, the partials are read back through pinned
// staging, and the (small) host fold finishes the job. That is portable
// across SYCL implementations (no local memory or sub-group primitives) and
// keeps the API synchronous. Pass a `Scratch<T>` to reuse the partials and
// the read-back across calls; the two-argument form allocates a temporary,
// which costs more than the reduction itself for small inputs (phase 1b
// numbers in GPU.md).

template <class T>
Result<T> reduce(Buffer<T> const &buf, T init, Scratch<T> &scratch) {
  if (buf.empty())
    return ok(std::move(init));
#ifdef FP_GPU_SYCL
  if (!usable()) {
    auto host = buf.to_host();
    if (!host.is_ok())
      return err<T>(host.error());
    return ok(fp::fold_left(host.value(), std::move(init), fp::plus));
  }
  constexpr std::size_t chunk_size = detail::reduce_chunk_size;
  const std::size_t n = buf.size();
  const std::size_t chunks = (n + chunk_size - 1) / chunk_size;
  auto ready = detail::ensure_scratch(scratch, chunks);
  if (!ready.is_ok())
    return err<T>(ready.error());
  try {
    T const *src = buf.data();
    T *dst = scratch.partials.data();
    detail::launch_1d(chunks, [=](std::size_t c) {
      const std::size_t begin = c * chunk_size;
      const std::size_t end = std::min(n, begin + chunk_size);
      T acc = T{};
      for (std::size_t i = begin; i < end; ++i)
        acc += src[i];
      dst[c] = acc;
    });
    detail::queue().memcpy(scratch.readback.data(), dst, chunks * sizeof(T));
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<T>(std::string("sycl: ") + e.what());
  }
  return ok(fp::fold_left(scratch.readback.span().first(chunks),
                          std::move(init), fp::plus));
#else
  (void)scratch; // host fallback: nothing to reuse
  return ok(fp::fold_left(std::span<T const>(buf.data(), buf.size()),
                          std::move(init), fp::plus));
#endif
}

template <class T> Result<T> reduce(Buffer<T> const &buf, T init = T{}) {
  Scratch<T> scratch;
  return reduce(buf, std::move(init), scratch);
}

template <class T>
Result<T> dot(Buffer<T> const &a, Buffer<T> const &b, Scratch<T> &scratch) {
  if (a.size() != b.size())
    return err<T>("gpu::dot: size mismatch");
  if (a.empty())
    return ok(T{});
#ifdef FP_GPU_SYCL
  if (!usable()) {
    auto ha = a.to_host();
    if (!ha.is_ok())
      return err<T>(ha.error());
    auto hb = b.to_host();
    if (!hb.is_ok())
      return err<T>(hb.error());
    return ok(fp::dot<T>(std::span<T const>(ha.value()),
                         std::span<T const>(hb.value())));
  }
  constexpr std::size_t chunk_size = detail::reduce_chunk_size;
  const std::size_t n = a.size();
  const std::size_t chunks = (n + chunk_size - 1) / chunk_size;
  auto ready = detail::ensure_scratch(scratch, chunks);
  if (!ready.is_ok())
    return err<T>(ready.error());
  try {
    T const *pa = a.data();
    T const *pb = b.data();
    T *dst = scratch.partials.data();
    detail::launch_1d(chunks, [=](std::size_t c) {
      const std::size_t begin = c * chunk_size;
      const std::size_t end = std::min(n, begin + chunk_size);
      T acc = T{};
      for (std::size_t i = begin; i < end; ++i)
        acc += pa[i] * pb[i];
      dst[c] = acc;
    });
    detail::queue().memcpy(scratch.readback.data(), dst, chunks * sizeof(T));
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<T>(std::string("sycl: ") + e.what());
  }
  return ok(fp::fold_left(scratch.readback.span().first(chunks), T{},
                          fp::plus));
#else
  (void)scratch; // host fallback: nothing to reuse
  return ok(fp::dot<T>(std::span<T const>(a.data(), a.size()),
                       std::span<T const>(b.data(), b.size())));
#endif
}

template <class T> Result<T> dot(Buffer<T> const &a, Buffer<T> const &b) {
  Scratch<T> scratch;
  return dot(a, b, scratch);
}

namespace detail {
// CPU fallback for matmul: flat row-major grids through fp::matmul.
template <class T>
Result<Buffer<T>> matmul_host(std::span<T const> a, std::span<T const> b,
                              std::size_t m, std::size_t k, std::size_t n) {
  std::vector<std::vector<T>> ga(m, std::vector<T>(k));
  std::vector<std::vector<T>> gb(k, std::vector<T>(n));
  for (std::size_t i = 0; i < m; ++i)
    std::copy_n(a.data() + i * k, k, ga[i].begin());
  for (std::size_t i = 0; i < k; ++i)
    std::copy_n(b.data() + i * n, n, gb[i].begin());
  const auto gc = fp::matmul(ga, gb);
  auto out = Buffer<T>::alloc(m * n);
  if (!out.is_ok())
    return out;
  for (std::size_t i = 0; i < m; ++i)
    std::copy_n(gc[i].begin(), n, out.value().data() + i * n);
  return out;
}
// CPU fallback for transpose: the flat block through fp::transpose.
template <class T>
Result<Buffer<T>> transpose_host(std::span<T const> src, std::size_t rows,
                                 std::size_t cols) {
  std::vector<std::vector<T>> grid(rows, std::vector<T>(cols));
  for (std::size_t r = 0; r < rows; ++r)
    std::copy_n(src.data() + r * cols, cols, grid[r].begin());
  const auto t = fp::transpose(grid);
  auto out = Buffer<T>::alloc(cols * rows);
  if (!out.is_ok())
    return out;
  for (std::size_t r = 0; r < cols; ++r)
    std::copy_n(t[r].begin(), rows, out.value().data() + r * rows);
  return out;
}
} // namespace detail

// --- dense linear algebra ---------------------------------------------------
// Row-major `batches` x (`m x k`) times (`k x n`) -> `batches` x (`m x n`),
// matrices laid out back to back. On a real implementation the kernel tiles
// 16x16 through local memory; without group algorithms (the CPU stub) it runs
// one work-item per output element, and the CPU fallback uses fp::matmul.

// Row-major transpose: `src` is rows x cols, the result is cols x rows.
// Attention needs it for `Q @ Kᵀ`; the tiled kernel keeps both the reads and
// the writes coalesced through a 16x16 local tile.
template <class T>
Result<Buffer<T>> transpose(Buffer<T> const &src, std::size_t rows,
                            std::size_t cols) {
  if (src.size() != rows * cols)
    return err<Buffer<T>>("gpu::transpose: shape mismatch");
  if (rows == 0 || cols == 0)
    return Buffer<T>::alloc(0);
#ifdef FP_GPU_SYCL
  auto out = Buffer<T>::alloc(cols * rows);
  if (!out.is_ok())
    return out;
  try {
    T const *in = src.data();
    T *o = out.value().data();
#if defined(FP_GPU_GROUP_ALGORITHMS)
    constexpr std::size_t tile = 16;
    const std::size_t prow = ((rows + tile - 1) / tile) * tile;
    const std::size_t pcol = ((cols + tile - 1) / tile) * tile;
    detail::queue().submit([&](sycl::handler &h) {
      sycl::local_accessor<T, 2> block(sycl::range<2>(tile, tile + 1), h);
      h.parallel_for(sycl::nd_range<2>(sycl::range<2>(prow, pcol),
                                       sycl::range<2>(tile, tile)),
                     [=](sycl::nd_item<2> item) {
                       const std::size_t r = item.get_global_id(0);
                       const std::size_t c = item.get_global_id(1);
                       const std::size_t lr = item.get_local_id(0);
                       const std::size_t lc = item.get_local_id(1);
                       const std::size_t gr = item.get_group(0) * tile;
                       const std::size_t gc = item.get_group(1) * tile;
                       if (r < rows && c < cols)
                         block[lr][lc] = in[r * cols + c];
                       sycl::group_barrier(item.get_group());
                       const std::size_t orow = gc + lr;
                       const std::size_t ocol = gr + lc;
                       if (orow < cols && ocol < rows)
                         o[orow * rows + ocol] = block[lc][lr];
                     });
    });
    detail::queue().wait_and_throw();
#else
    detail::launch_1d(rows * cols, [=](std::size_t i) {
      const std::size_t r = i / cols, c = i % cols;
      o[c * rows + r] = in[i];
    });
    detail::queue().wait_and_throw();
#endif
    return out;
  } catch (sycl::exception const &e) {
    return err<Buffer<T>>(std::string("sycl: ") + e.what());
  }
#else
  auto host = src.to_host();
  if (!host.is_ok())
    return err<Buffer<T>>(host.error());
  return detail::transpose_host<T>(host.value(), rows, cols);
#endif
}

template <class T>
Result<Buffer<T>> batched_matmul(Buffer<T> const &a, Buffer<T> const &b,
                                 std::size_t batches, std::size_t m,
                                 std::size_t k, std::size_t n) {
  if (a.size() != batches * m * k || b.size() != batches * k * n)
    return err<Buffer<T>>("gpu::batched_matmul: shape mismatch");
  if (batches == 0 || m == 0 || n == 0)
    return Buffer<T>::alloc(0);
  if (k == 0) {
    auto zeros = Buffer<T>::alloc(batches * m * n);
    if (!zeros.is_ok())
      return zeros;
    const auto filled = zeros.value().fill(T{});
    if (!filled.is_ok())
      return err<Buffer<T>>(filled.error());
    return zeros;
  }
#ifdef FP_GPU_SYCL
  auto out = Buffer<T>::alloc(batches * m * n);
  if (!out.is_ok())
    return out;
  try {
    T const *pa = a.data();
    T const *pb = b.data();
    T *pc = out.value().data();
#if defined(FP_GPU_GROUP_ALGORITHMS)
    constexpr std::size_t tile = 16;
    const std::size_t prow = ((m + tile - 1) / tile) * tile;
    const std::size_t pcol = ((n + tile - 1) / tile) * tile;
    detail::queue().submit([&](sycl::handler &h) {
      sycl::local_accessor<T, 2> as(sycl::range<2>(tile, tile), h);
      sycl::local_accessor<T, 2> bs(sycl::range<2>(tile, tile), h);
      h.parallel_for(sycl::nd_range<2>(sycl::range<2>(batches * prow, pcol),
                                       sycl::range<2>(tile, tile)),
                     [=](sycl::nd_item<2> item) {
                       const std::size_t gr = item.get_global_id(0);
                       const std::size_t batch = gr / prow;
                       const std::size_t row = gr % prow;
                       const std::size_t col = item.get_global_id(1);
                       const std::size_t lr = item.get_local_id(0);
                       const std::size_t lc = item.get_local_id(1);
                       T const *ba = pa + batch * m * k;
                       T const *bb = pb + batch * k * n;
                       T *bc = pc + batch * m * n;
                       T acc = T{};
                       for (std::size_t t = 0; t < k; t += tile) {
                         const std::size_t kc = t + lc, kr = t + lr;
                         as[lr][lc] =
                             (row < m && kc < k) ? ba[row * k + kc] : T{};
                         bs[lr][lc] =
                             (kr < k && col < n) ? bb[kr * n + col] : T{};
                         sycl::group_barrier(item.get_group());
                         for (std::size_t kk = 0; kk < tile; ++kk)
                           acc += as[lr][kk] * bs[kk][lc];
                         sycl::group_barrier(item.get_group());
                       }
                       if (row < m && col < n)
                         bc[row * n + col] = acc;
                     });
    });
    detail::queue().wait_and_throw();
#else
    detail::launch_1d(batches * m * n, [=](std::size_t index) {
      const std::size_t batch = index / (m * n);
      const std::size_t rest = index % (m * n);
      const std::size_t row = rest / n;
      const std::size_t col = rest % n;
      T const *ba = pa + batch * m * k;
      T const *bb = pb + batch * k * n;
      T acc = T{};
      for (std::size_t i = 0; i < k; ++i)
        acc += ba[row * k + i] * bb[i * n + col];
      pc[index] = acc;
    });
    detail::queue().wait_and_throw();
#endif
    return out;
  } catch (sycl::exception const &e) {
    return err<Buffer<T>>(std::string("sycl: ") + e.what());
  }
#else
  auto ha = a.to_host();
  if (!ha.is_ok())
    return err<Buffer<T>>(ha.error());
  auto hb = b.to_host();
  if (!hb.is_ok())
    return err<Buffer<T>>(hb.error());
  auto out = Buffer<T>::alloc(batches * m * n);
  if (!out.is_ok())
    return out;
  for (std::size_t batch = 0; batch < batches; ++batch) {
    auto part = detail::matmul_host<T>(
        std::span<T const>(ha.value().data() + batch * m * k, m * k),
        std::span<T const>(hb.value().data() + batch * k * n, k * n), m, k, n);
    if (!part.is_ok())
      return err<Buffer<T>>(part.error());
    std::copy_n(part.value().data(), m * n, out.value().data() + batch * m * n);
  }
  return out;
#endif
}

template <class T>
Result<Buffer<T>> matmul(Buffer<T> const &a, Buffer<T> const &b, std::size_t m,
                         std::size_t k, std::size_t n) {
  return batched_matmul(a, b, 1, m, k, n);
}

} // namespace fp::gpu

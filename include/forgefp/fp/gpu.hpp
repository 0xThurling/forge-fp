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
// trailing group is masked with `index < n`.
template <class Kernel> void launch_1d(std::size_t n, Kernel kernel) {
  if (n == 0)
    return;
  const std::size_t groups = (n + work_group_size - 1) / work_group_size;
  queue().parallel_for(
      sycl::nd_range<1>(sycl::range<1>(groups * work_group_size),
                        sycl::range<1>(work_group_size)),
      [n, kernel](sycl::id<1> item) {
        const std::size_t index = item[0];
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
#endif
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

// --- reductions -------------------------------------------------------------
// Device path: each work-item sums one chunk of `chunk_size` elements into a
// partials buffer; the (small) partials are finalized on the host. That is
// portable across SYCL implementations (no local memory or sub-group
// primitives) and keeps the API synchronous.

template <class T>
Result<T> reduce(Buffer<T> const &buf, T init = T{}) {
  if (buf.empty())
    return ok(std::move(init));
#ifdef FP_GPU_SYCL
  if (!usable()) {
    auto host = buf.to_host();
    if (!host.is_ok())
      return err<T>(host.error());
    return ok(fp::fold_left(host.value(), std::move(init), fp::plus));
  }
  constexpr std::size_t chunk_size = 4096;
  const std::size_t n = buf.size();
  const std::size_t chunks = (n + chunk_size - 1) / chunk_size;
  auto partials = Buffer<T>::alloc(chunks);
  if (!partials.is_ok())
    return err<T>(partials.error());
  try {
    T const *src = buf.data();
    T *dst = partials.value().data();
    detail::queue().parallel_for(sycl::range<1>(chunks), [=](sycl::id<1> c) {
      const std::size_t begin = c[0] * chunk_size;
      const std::size_t end = std::min(n, begin + chunk_size);
      T acc = T{};
      for (std::size_t i = begin; i < end; ++i)
        acc += src[i];
      dst[c[0]] = acc;
    });
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<T>(std::string("sycl: ") + e.what());
  }
  auto host = partials.value().to_host();
  if (!host.is_ok())
    return err<T>(host.error());
  return ok(fp::fold_left(host.value(), std::move(init), fp::plus));
#else
  return ok(fp::fold_left(std::span<T const>(buf.data(), buf.size()),
                          std::move(init), fp::plus));
#endif
}

template <class T> Result<T> dot(Buffer<T> const &a, Buffer<T> const &b) {
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
  constexpr std::size_t chunk_size = 4096;
  const std::size_t n = a.size();
  const std::size_t chunks = (n + chunk_size - 1) / chunk_size;
  auto partials = Buffer<T>::alloc(chunks);
  if (!partials.is_ok())
    return err<T>(partials.error());
  try {
    T const *pa = a.data();
    T const *pb = b.data();
    T *dst = partials.value().data();
    detail::queue().parallel_for(sycl::range<1>(chunks), [=](sycl::id<1> c) {
      const std::size_t begin = c[0] * chunk_size;
      const std::size_t end = std::min(n, begin + chunk_size);
      T acc = T{};
      for (std::size_t i = begin; i < end; ++i)
        acc += pa[i] * pb[i];
      dst[c[0]] = acc;
    });
    detail::queue().wait_and_throw();
  } catch (sycl::exception const &e) {
    return err<T>(std::string("sycl: ") + e.what());
  }
  auto host = partials.value().to_host();
  if (!host.is_ok())
    return err<T>(host.error());
  return ok(fp::fold_left(host.value(), T{}, fp::plus));
#else
  return ok(fp::dot<T>(std::span<T const>(a.data(), a.size()),
                       std::span<T const>(b.data(), b.size())));
#endif
}

} // namespace fp::gpu

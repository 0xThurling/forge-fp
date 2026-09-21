#pragma once
// Minimal SYCL emulation for testing fp/gpu.hpp's SYCL branch on a machine
// with no SYCL implementation. It is NOT a SYCL implementation: kernels run
// sequentially on the host, there is no parallelism, no real device, and no
// async execution. Its job is to compile and exercise the code path, catch
// API misuse, and verify buffer lifetimes and results.
//
// Use it by putting `test/support` on the include path (so
// `__has_include(<sycl/sycl.hpp>)` is true) or by defining FP_GPU_SYCL after
// including this header. See scripts/run_gpu_stub_test.sh.
//
// It models the SYCL item types: a `range` launch hands the kernel an `id`,
// an `nd_range` launch an `nd_item`. Getting that wrong is a compile error
// here as well as on a real toolchain.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

// Lets fp/gpu.hpp report which backend it is talking to.
#define SYCL_IMPLEMENTATION_FORGEFP_STUB 1

namespace sycl {

class exception : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

template <int Dims> class range {
public:
  range() = default;
  explicit range(std::size_t n) : n_(n) {}
  std::size_t size() const { return n_; }
  std::size_t operator[](int) const { return n_; }

private:
  std::size_t n_ = 0;
};

template <int Dims> class id {
public:
  id() = default;
  explicit id(std::size_t i) : i_(i) {}
  std::size_t operator[](int) const { return i_; }

private:
  std::size_t i_ = 0;
};

template <int Dims> class nd_range {
public:
  nd_range(range<Dims> global, range<Dims> local)
      : global_(global), local_(local) {}
  range<Dims> get_global_range() const { return global_; }
  range<Dims> get_local_range() const { return local_; }

private:
  range<Dims> global_;
  range<Dims> local_;
};

// The item handed to an `nd_range` kernel. Real implementations type-check
// this: an `nd_range` kernel must take `nd_item`, never `id`. Modelling it
// here means a signature mistake fails to compile in the stub too, instead of
// only showing up on a machine with a SYCL toolchain.
template <int Dims> class nd_item {
public:
  nd_item() = default;
  nd_item(id<Dims> global, id<Dims> local, id<Dims> group)
      : global_(global), local_(local), group_(group) {}

  std::size_t get_global_id(int) const { return global_[0]; }
  std::size_t get_local_id(int) const { return local_[0]; }
  std::size_t get_group_id(int) const { return group_[0]; }

private:
  id<Dims> global_;
  id<Dims> local_;
  id<Dims> group_;
};

namespace aspect {
enum aspect_enum {
  usm_device_allocations = 1,
  usm_shared_allocations = 2,
};
} // namespace aspect

namespace info {
namespace device {
struct name {
  using return_type = std::string;
};
struct vendor {
  using return_type = std::string;
};
struct global_mem_size {
  using return_type = std::size_t;
};
struct driver_version {
  using return_type = std::string;
};
} // namespace device
namespace platform {
struct name {
  using return_type = std::string;
};
} // namespace platform
} // namespace info

class platform {
public:
  template <class Param> typename Param::return_type get_info() const {
    if constexpr (std::is_same_v<Param, info::platform::name>)
      return std::string("ForgeFP stub platform");
    else
      return std::string("unknown");
  }
};

class device {
public:
  device() = default;
  bool has(unsigned aspect) const { return (aspects_ & aspect) != 0; }
  bool is_cpu() const { return false; }
  bool is_gpu() const { return true; }
  bool is_accelerator() const { return false; }

  platform get_platform() const { return platform{}; }

  // Mirrors SYCL's `Param::return_type` shape so the call sites look the same.
  template <class Param> typename Param::return_type get_info() const {
    if constexpr (std::is_same_v<Param, info::device::name>)
      return std::string("SYCL stub device");
    else if constexpr (std::is_same_v<Param, info::device::vendor>)
      return std::string("ForgeFP test stub");
    else if constexpr (std::is_same_v<Param, info::device::driver_version>)
      return std::string("stub-0");
    else
      return std::size_t{1} << 30;
  }

  unsigned aspects_ = aspect::usm_device_allocations |
                      aspect::usm_shared_allocations;
};

struct default_selector_t {};
inline constexpr default_selector_t default_selector_v{};

namespace property {
namespace queue {
struct in_order {};
} // namespace queue
} // namespace property

class queue {
public:
  template <class Selector, class... Props>
  explicit queue(Selector, Props...) {}

  device get_device() const { return device{}; }

  template <class T> void fill(T *ptr, T const &value, std::size_t count) {
    std::fill(ptr, ptr + count, value);
  }

  void memcpy(void *dst, void const *src, std::size_t bytes) {
    std::memcpy(dst, src, bytes);
  }

  template <class Kernel> void parallel_for(range<1> r, Kernel kernel) {
    for (std::size_t i = 0; i < r.size(); ++i)
      kernel(id<1>{i});
  }

  template <class Kernel> void parallel_for(nd_range<1> r, Kernel kernel) {
    const std::size_t n = r.get_global_range().size();
    const std::size_t local = r.get_local_range().size();
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t l = local == 0 ? 0 : i % local;
      const std::size_t g = local == 0 ? 0 : i / local;
      kernel(nd_item<1>{id<1>{i}, id<1>{l}, id<1>{g}});
    }
  }

  void wait_and_throw() {}
};

// --- USM --------------------------------------------------------------------

template <class T> T *malloc_device(std::size_t count, queue const &) {
  return static_cast<T *>(std::malloc(count * sizeof(T)));
}

template <class T>
T *aligned_alloc_device(std::size_t, std::size_t count, queue const &q) {
  return malloc_device<T>(count, q);
}

template <class T> T *malloc_shared(std::size_t count, queue const &) {
  return static_cast<T *>(std::malloc(count * sizeof(T)));
}

template <class T>
T *aligned_alloc_shared(std::size_t, std::size_t count, queue const &q) {
  return malloc_shared<T>(count, q);
}

// Pinned host memory: an ordinary host allocation in the stub (there is no
// device to DMA from), which is exactly what the CPU fallback does.
template <class T> T *malloc_host(std::size_t count, queue const &) {
  return static_cast<T *>(std::malloc(count * sizeof(T)));
}

template <class T>
T *aligned_alloc_host(std::size_t, std::size_t count, queue const &q) {
  return malloc_host<T>(count, q);
}

inline void free(void *ptr, queue const &) { std::free(ptr); }

// --- device math (forwarded to std) -----------------------------------------

template <class T> T exp(T x) { return std::exp(x); }

template <class T> T fmax(T a, T b) { return a < b ? b : a; }

} // namespace sycl

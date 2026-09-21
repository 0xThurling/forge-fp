// Exercises fp/gpu.hpp's SYCL branch on a machine with no SYCL toolchain, by
// compiling with `-I test/support`, which provides a stub <sycl/sycl.hpp>
// (sequential host kernels). Run via scripts/run_gpu_stub_test.sh.
//
// This is a correctness harness, not a benchmark: it verifies that the SYCL
// code path compiles, that buffers are allocated/freed, and that the kernels
// produce the same values as the CPU fallback.
#include <fp/gpu.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, char const *what) {
  if (ok) {
    std::printf("ok   %s\n", what);
  } else {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

} // namespace

int main() {
  check(fp::gpu::available, "available");
  check(fp::gpu::usable(), "usable");

  const auto info = fp::gpu::default_device_info();
  check(info.is_ok() && info.value().is_gpu && !info.value().name.empty(),
        "device info");

  const auto backend = fp::gpu::backend_info();
  check(backend.is_ok() &&
            backend.value().implementation ==
                std::string("ForgeFP CPU stub") &&
            !backend.value().platform.empty() &&
            !backend.value().driver_version.empty(),
        "backend info");

  // Buffers + elementwise kernels.
  auto src = fp::gpu::Buffer<int>::from_host(std::vector<int>{1, 2, 3});
  auto dst = fp::gpu::Buffer<int>::alloc(3);
  check(src.is_ok() && dst.is_ok(), "buffer alloc");
  check(fp::gpu::map_to(dst.value(), src.value(), [](int x) { return x * x; })
            .is_ok(),
        "map_to");
  check(dst.value().to_host().value() == std::vector<int>({1, 4, 9}),
        "map_to values");
  check(fp::gpu::transform_inplace(dst.value(), [](int x) { return x + 1; })
            .is_ok(),
        "transform_inplace");
  check(dst.value().to_host().value() == std::vector<int>({2, 5, 10}),
        "transform values");

  auto host_map =
      fp::gpu::map(std::vector<int>{1, 2, 3}, [](int x) { return x + 1; });
  check(host_map.is_ok() && host_map.value() == std::vector<int>({2, 3, 4}),
        "host map");

  // Reductions span multiple 4096-element chunks.
  std::vector<double> big(10'000, 1.5);
  auto bb = fp::gpu::Buffer<double>::from_host(big);
  check(bb.is_ok(), "big buffer");
  const auto sum = fp::gpu::reduce(bb.value(), 0.0);
  check(sum.is_ok() && std::abs(sum.value() - 15'000.0) < 1e-9, "reduce");
  const auto d = fp::gpu::dot(bb.value(), bb.value());
  check(d.is_ok() && std::abs(d.value() - 22'500.0) < 1e-9, "dot");

  // Optimizer + attention kernels.
  auto y = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 1.0});
  auto x = fp::gpu::Buffer<double>::from_host(std::vector<double>{2.0, 4.0});
  check(fp::gpu::axpy_inplace(y.value(), 0.5, x.value()).is_ok(), "axpy");
  check(y.value().to_host().value() == std::vector<double>({2.0, 3.0}),
        "axpy values");

  auto logits = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 0.0, 0.0, 0.0});
  check(fp::gpu::softmax_rows(logits.value(), 2, 3).is_ok(), "softmax_rows");
  const auto probs = logits.value().to_host().value();
  check(std::abs(probs[0] + probs[1] + probs[2] - 1.0) < 1e-12 &&
            std::abs(probs[3] - 1.0 / 3.0) < 1e-12 &&
            std::abs(probs[4] - 1.0 / 3.0) < 1e-12 &&
            std::abs(probs[5] - 1.0 / 3.0) < 1e-12,
        "softmax values");

  // Shared USM buffers are host-accessible.
  auto shared = fp::gpu::Buffer<double>::alloc_shared(2);
  check(shared.is_ok() && shared.value().host_accessible(), "alloc_shared");
  if (shared.is_ok())
    shared.value().data()[0] = 4.0;

  // Error paths.
  auto small = fp::gpu::Buffer<int>::alloc(1);
  auto small2 = fp::gpu::Buffer<int>::alloc(2);
  check(!fp::gpu::map_to(small.value(), src.value(), [](int v) { return v; })
             .is_ok(),
        "map_to size error");
  check(!fp::gpu::softmax_rows(logits.value(), 3, 3).is_ok(),
        "softmax shape error");
  check(!fp::gpu::dot(small.value(), small2.value()).is_ok(),
        "dot size error");

  if (failures == 0)
    std::printf("GPU SYCL stub: all checks passed\n");
  else
    std::printf("GPU SYCL stub: %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

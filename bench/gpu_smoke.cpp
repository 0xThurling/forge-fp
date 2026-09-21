// Smoke test for a real SYCL toolchain: prints the backend/device and runs the
// fp::gpu kernels. Build with scripts/run_gpu_build.sh (icpx / acpp / dpcpp).
#include <fp/gpu.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

int main() {
  if (!fp::gpu::available) {
    std::printf("no SYCL toolchain in this build\n");
    return 0;
  }

  const auto backend = fp::gpu::backend_info();
  if (!backend.is_ok()) {
    std::printf("backend info failed: %s\n", backend.error().c_str());
    return 1;
  }
  std::printf("implementation : %s\n", backend.value().implementation.c_str());
  std::printf("platform       : %s\n", backend.value().platform.c_str());
  std::printf("device         : %s\n", backend.value().device.c_str());
  std::printf("driver         : %s\n", backend.value().driver_version.c_str());
  std::printf("usable (USM)   : %s\n", fp::gpu::usable() ? "yes" : "no");

  if (!fp::gpu::usable()) {
    std::printf("device does not support USM device allocations; skipping kernels\n");
    return 0;
  }

  int failures = 0;

  auto buf = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0, 3.0, 4.0});
  if (!buf.is_ok()) {
    std::printf("buffer alloc failed: %s\n", buf.error().c_str());
    return 1;
  }

  const auto sum = fp::gpu::reduce(buf.value(), 0.0);
  if (!sum.is_ok() || std::abs(sum.value() - 10.0) > 1e-9) {
    std::printf("reduce mismatch\n");
    ++failures;
  }
  const auto dot = fp::gpu::dot(buf.value(), buf.value());
  if (!dot.is_ok() || std::abs(dot.value() - 30.0) > 1e-9) {
    std::printf("dot mismatch\n");
    ++failures;
  }
  if (!fp::gpu::axpy_inplace(buf.value(), 1.0, buf.value()).is_ok()) {
    std::printf("axpy failed\n");
    ++failures;
  }
  auto logits = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 1.0});
  if (!logits.is_ok() || !fp::gpu::softmax_rows(logits.value(), 1, 2).is_ok()) {
    std::printf("softmax_rows failed\n");
    ++failures;
  }

  std::printf(failures == 0 ? "gpu smoke: ok\n" : "gpu smoke: %d failure(s)\n",
              failures);
  return failures == 0 ? 0 : 1;
}

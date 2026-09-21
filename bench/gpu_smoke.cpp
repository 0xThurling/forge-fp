// Correctness harness for a real SYCL toolchain: prints the backend/device and
// runs the fp::gpu kernels, checking values against the CPU reference. Build
// with scripts/run_gpu_build.sh (icpx / acpp / dpcpp).
//
// This is the device counterpart of bench/gpu_stub_test.cpp: the stub verifies
// that the SYCL branch compiles and produces the right numbers on the host,
// this verifies the same numbers on the device — including the tiled kernels
// that need local memory and work-group barriers.
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

bool near(double a, double b, double tol = 1e-9) {
  return std::abs(a - b) <= tol * (1.0 + std::abs(b));
}

} // namespace

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

  // --- buffers and elementwise kernels --------------------------------------
  auto buf = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0, 3.0, 4.0});
  if (!buf.is_ok()) {
    std::printf("buffer alloc failed: %s\n", buf.error().c_str());
    return 1;
  }
  check(near(fp::gpu::reduce(buf.value(), 0.0).value(), 10.0), "reduce");
  check(near(fp::gpu::dot(buf.value(), buf.value()).value(), 30.0), "dot");
  check(fp::gpu::axpy_inplace(buf.value(), 1.0, buf.value()).is_ok(), "axpy");
  check(near(buf.value().to_host().value()[3], 8.0), "axpy values");

  auto logits = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 1.0});
  check(fp::gpu::softmax_rows(logits.value(), 1, 2).is_ok(), "softmax_rows");
  check(near(logits.value().to_host().value()[0], 0.5), "softmax values");

  // --- pinned staging --------------------------------------------------------
  auto staging = fp::gpu::HostBuffer<double>::alloc(8);
  check(staging.is_ok() && staging.value().is_pinned(), "pinned staging");
  staging.value().span()[0] = 5.0;
  staging.value().span()[1] = 6.0;
  staging.value().span()[2] = 7.0;
  auto staged = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{0.0, 0.0, 0.0});
  check(staged.is_ok() && staged.value().copy_from(staging.value()).is_ok() &&
            near(staged.value().to_host().value()[1], 6.0),
        "staged copy_from");
  check(staged.value().to_host(staging.value()).is_ok() &&
            near(staging.value().span()[2], 7.0),
        "staged read-back");

  // --- row and column kernels ------------------------------------------------
  auto grid = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
  const auto rs = fp::gpu::row_sums(grid.value(), 2, 3);
  check(rs.is_ok() && near(rs.value().to_host().value()[1], 15.0), "row_sums");
  const auto rm = fp::gpu::row_means(grid.value(), 2, 3);
  check(rm.is_ok() && near(rm.value().to_host().value()[1], 5.0), "row_means");
  const auto cs = fp::gpu::col_sums(grid.value(), 2, 3);
  check(cs.is_ok() && near(cs.value().to_host().value()[2], 9.0), "col_sums");
  auto bias = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{10.0, 20.0, 30.0});
  check(fp::gpu::add_row_broadcast(grid.value(), 2, 3, bias.value()).is_ok() &&
            near(grid.value().to_host().value()[4], 25.0),
        "add_row_broadcast");

  // --- work-group softmax matches the portable one ---------------------------
  const std::vector<double> attention{1.0, 2.0, 3.0, 0.5, -1.0, 0.0};
  auto portable = fp::gpu::Buffer<double>::from_host(attention);
  auto work_group = fp::gpu::Buffer<double>::from_host(attention);
  check(fp::gpu::softmax_rows(portable.value(), 2, 3).is_ok() &&
            fp::gpu::softmax_rows_wg(work_group.value(), 2, 3).is_ok(),
        "softmax_rows_wg runs");
  {
    const auto a = portable.value().to_host().value();
    const auto b = work_group.value().to_host().value();
    bool same = true;
    for (std::size_t i = 0; i < a.size(); ++i)
      same = same && near(a[i], b[i], 1e-12);
    check(same, "softmax_rows_wg values");
  }

  // --- scratch-backed reductions ---------------------------------------------
  fp::gpu::Scratch<double> scratch;
  auto big = fp::gpu::Buffer<double>::from_host(std::vector<double>(5000, 1.5));
  const auto total = fp::gpu::reduce(big.value(), 0.0, scratch);
  check(total.is_ok() && near(total.value(), 7500.0), "reduce with scratch");
  const auto square = fp::gpu::dot(big.value(), big.value(), scratch);
  check(square.is_ok() && near(square.value(), 11'250.0), "dot with scratch");

  // --- matmul / batched_matmul ------------------------------------------------
  auto ma = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0}); // 2 x 3
  auto mb = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{7.0, 8.0, 9.0, 10.0, 11.0, 12.0}); // 3 x 2
  const auto mc = fp::gpu::matmul(ma.value(), mb.value(), 2, 3, 2);
  check(mc.is_ok() &&
            mc.value().to_host().value() ==
                std::vector<double>({58.0, 64.0, 139.0, 154.0}),
        "matmul (non-tile-multiple)");

  // A larger product that tiles over k more than once, checked against the
  // CPU reference.
  constexpr std::size_t m = 33, k = 20, n = 17;
  std::vector<double> ha(m * k), hb(k * n);
  for (std::size_t i = 0; i < ha.size(); ++i)
    ha[i] = static_cast<double>(i % 11) - 5.0;
  for (std::size_t i = 0; i < hb.size(); ++i)
    hb[i] = static_cast<double>(i % 7) * 0.5 - 1.0;
  auto ta = fp::gpu::Buffer<double>::from_host(ha);
  auto tb = fp::gpu::Buffer<double>::from_host(hb);
  const auto tc = fp::gpu::matmul(ta.value(), tb.value(), m, k, n);
  {
    std::vector<std::vector<double>> ga(m, std::vector<double>(k));
    std::vector<std::vector<double>> gb(k, std::vector<double>(n));
    for (std::size_t i = 0; i < m; ++i)
      std::copy_n(ha.begin() + i * k, k, ga[i].begin());
    for (std::size_t i = 0; i < k; ++i)
      std::copy_n(hb.begin() + i * n, n, gb[i].begin());
    const auto gc = fp::matmul(ga, gb);
    const auto flat = tc.is_ok() ? tc.value().to_host().value()
                                 : std::vector<double>{};
    bool same = tc.is_ok() && flat.size() == m * n;
    for (std::size_t i = 0; same && i < m; ++i)
      for (std::size_t j = 0; same && j < n; ++j)
        same = near(flat[i * n + j], gc[i][j]);
    check(same, "matmul 33x20x17 vs fp::matmul");
  }

  // --- position-aware and multi-input kernels ---------------------------------
  auto idx = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0});
  check(fp::gpu::transform_inplace_indexed(
            idx.value(), [](std::size_t i, double x) { return x + double(i); })
                .is_ok() &&
            near(idx.value().to_host().value()[3], 7.0),
        "transform_inplace_indexed");

  auto zip_a =
      fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0, 3.0});
  auto zip_b =
      fp::gpu::Buffer<double>::from_host(std::vector<double>{10.0, 20.0, 30.0});
  check(fp::gpu::zip_transform_inplace(zip_a.value(), zip_b.value(),
                                      [](double x, double y) { return x + y; })
                .is_ok() &&
            near(zip_a.value().to_host().value()[2], 33.0),
        "zip_transform_inplace");
  check(fp::gpu::zip3_transform_inplace(
            zip_a.value(), zip_b.value(), zip_b.value(),
            [](double x, double y, double z) { return x + y + z; })
                .is_ok() &&
            near(zip_a.value().to_host().value()[2], 93.0),
        "zip3_transform_inplace");

  // --- transpose (tiled on device, including a non-tile-multiple shape) -------
  {
    constexpr std::size_t rows = 5, cols = 3;
    std::vector<double> h(rows * cols);
    for (std::size_t i = 0; i < h.size(); ++i)
      h[i] = static_cast<double>(i);
    auto m = fp::gpu::Buffer<double>::from_host(h);
    const auto t = fp::gpu::transpose(m.value(), rows, cols);
    bool same = t.is_ok() && t.value().size() == rows * cols;
    const auto flat = same ? t.value().to_host().value() : std::vector<double>{};
    for (std::size_t r = 0; same && r < rows; ++r)
      for (std::size_t c = 0; same && c < cols; ++c)
        same = near(flat[c * rows + r], h[r * cols + c]);
    check(same, "transpose 5x3 (tiled)");
  }

  auto ba = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 0.0, 0.0, 1.0, 2.0, 0.0, 0.0, 2.0});
  auto bb = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0, 1.0, 0.0, 0.0, 1.0});
  const auto bc = fp::gpu::batched_matmul(ba.value(), bb.value(), 2, 2, 2, 2);
  check(bc.is_ok() &&
            bc.value().to_host().value() ==
                std::vector<double>({1.0, 2.0, 3.0, 4.0, 2.0, 0.0, 0.0, 2.0}),
        "batched_matmul");

  std::printf(failures == 0 ? "gpu smoke: ok\n" : "gpu smoke: %d failure(s)\n",
              failures);
  return failures == 0 ? 0 : 1;
}

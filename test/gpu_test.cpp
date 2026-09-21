#include <fp/gpu.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <utility>
#include <vector>

// These tests run on every machine: without a SYCL implementation the whole
// module falls back to the CPU, so the API and the results are still verified.
// GPU-specific assertions are skipped when `fp::gpu::available` is false.

TEST(Gpu, AvailabilityMatchesDeviceInfo) {
  const auto info = fp::gpu::default_device_info();
  if (fp::gpu::available) {
    ASSERT_TRUE(info.is_ok());
    EXPECT_FALSE(info.value().name.empty());
  } else {
    EXPECT_FALSE(info.is_ok());
    EXPECT_NE(info.error().find("SYCL"), std::string::npos);
    EXPECT_FALSE(fp::gpu::usable());
  }
}

TEST(Gpu, BackendInfo) {
  const auto info = fp::gpu::backend_info();
  if (fp::gpu::available) {
    ASSERT_TRUE(info.is_ok());
    EXPECT_FALSE(info.value().device.empty());
    EXPECT_STRNE(fp::gpu::implementation_name, "none (CPU fallback)");
  } else {
    EXPECT_FALSE(info.is_ok());
    EXPECT_STREQ(fp::gpu::implementation_name, "none (CPU fallback)");
  }
}

TEST(Gpu, AxpyAndSoftmaxRows) {
  auto y = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 1.0});
  auto x = fp::gpu::Buffer<double>::from_host(std::vector<double>{2.0, 4.0});
  ASSERT_TRUE(y.is_ok());
  ASSERT_TRUE(x.is_ok());
  ASSERT_TRUE(fp::gpu::axpy_inplace(y.value(), 0.5, x.value()).is_ok());
  EXPECT_EQ(y.value().to_host().value(), (std::vector<double>{2.0, 3.0}));

  auto logits = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 0.0, 0.0, 0.0});
  ASSERT_TRUE(logits.is_ok());
  ASSERT_TRUE(fp::gpu::softmax_rows(logits.value(), 2, 3).is_ok());

  const auto probs = logits.value().to_host().value();
  EXPECT_NEAR(probs[0] + probs[1] + probs[2], 1.0, 1e-12);
  EXPECT_NEAR(probs[3], 1.0 / 3.0, 1e-12);
  EXPECT_NEAR(probs[4], 1.0 / 3.0, 1e-12);
  EXPECT_NEAR(probs[5], 1.0 / 3.0, 1e-12);

  EXPECT_FALSE(fp::gpu::softmax_rows(logits.value(), 3, 3).is_ok());
}

TEST(Gpu, SharedBuffers) {
  auto shared = fp::gpu::Buffer<double>::alloc_shared(2);
  ASSERT_TRUE(shared.is_ok());
  EXPECT_TRUE(shared.value().host_accessible());
  shared.value().data()[0] = 4.0;
  EXPECT_DOUBLE_EQ(shared.value().to_host().value()[0], 4.0);
}

TEST(Gpu, BufferRoundTrip) {
  auto buf =
      fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0, 3.0});
  ASSERT_TRUE(buf.is_ok());
  EXPECT_EQ(buf.value().size(), 3u);
  EXPECT_FALSE(buf.value().empty());

  auto back = buf.value().to_host();
  ASSERT_TRUE(back.is_ok());
  EXPECT_EQ(back.value(), (std::vector<double>{1.0, 2.0, 3.0}));

  auto copy = buf.value().clone();
  ASSERT_TRUE(copy.is_ok());
  EXPECT_EQ(copy.value().to_host().value(), back.value());
}

TEST(Gpu, BufferFillAndMove) {
  auto filled = fp::gpu::Buffer<int>::alloc(4);
  ASSERT_TRUE(filled.is_ok());
  ASSERT_TRUE(filled.value().fill(7).is_ok());
  EXPECT_EQ(filled.value().to_host().value(), (std::vector<int>{7, 7, 7, 7}));

  auto moved = std::move(filled.value());
  EXPECT_EQ(moved.size(), 4u);
  EXPECT_EQ(moved.to_host().value(), (std::vector<int>{7, 7, 7, 7}));
}

TEST(Gpu, MapToAndTransform) {
  auto src = fp::gpu::Buffer<int>::from_host(std::vector<int>{1, 2, 3});
  ASSERT_TRUE(src.is_ok());
  auto dst = fp::gpu::Buffer<int>::alloc(3);
  ASSERT_TRUE(dst.is_ok());

  ASSERT_TRUE(
      fp::gpu::map_to(dst.value(), src.value(), [](int x) { return x * x; })
          .is_ok());
  EXPECT_EQ(dst.value().to_host().value(), (std::vector<int>{1, 4, 9}));

  ASSERT_TRUE(
      fp::gpu::transform_inplace(dst.value(), [](int x) { return x + 1; })
          .is_ok());
  EXPECT_EQ(dst.value().to_host().value(), (std::vector<int>{2, 5, 10}));
}

TEST(Gpu, MapFromHost) {
  auto r = fp::gpu::map(std::vector<int>{1, 2, 3}, [](int x) { return x + 1; });
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(r.value(), (std::vector<int>{2, 3, 4}));
}

TEST(Gpu, ReduceAndDot) {
  auto buf = fp::gpu::Buffer<double>::from_host(std::vector<double>{3.0, 4.0});
  ASSERT_TRUE(buf.is_ok());

  EXPECT_DOUBLE_EQ(fp::gpu::reduce(buf.value(), 0.0).value(), 7.0);
  EXPECT_DOUBLE_EQ(fp::gpu::dot(buf.value(), buf.value()).value(), 25.0);
}

TEST(Gpu, Errors) {
  auto small = fp::gpu::Buffer<int>::alloc(2);
  auto big = fp::gpu::Buffer<int>::from_host(std::vector<int>{1, 2, 3});
  ASSERT_TRUE(small.is_ok());
  ASSERT_TRUE(big.is_ok());

  // destination too small
  EXPECT_FALSE(
      fp::gpu::map_to(small.value(), big.value(), [](int x) { return x; })
          .is_ok());

  // dot size mismatch
  EXPECT_FALSE(fp::gpu::dot(small.value(), big.value()).is_ok());
}

TEST(Gpu, HostStagingTransfers) {
  auto staging = fp::gpu::HostBuffer<double>::alloc(4);
  ASSERT_TRUE(staging.is_ok());
  EXPECT_EQ(staging.value().size(), 4u);
  EXPECT_FALSE(staging.value().empty());
  // Pinned only where there is a device to DMA from.
  EXPECT_EQ(staging.value().is_pinned(), fp::gpu::available);

  // Producer writes into the staging block, then one copy moves it in.
  const std::vector<double> src{1.0, 2.0, 3.0};
  std::copy(src.begin(), src.end(), staging.value().span().begin());
  auto dev = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{9.0, 9.0, 9.0});
  ASSERT_TRUE(dev.is_ok());
  ASSERT_TRUE(dev.value().copy_from(staging.value()).is_ok());
  EXPECT_EQ(dev.value().to_host().value(), src);

  // Read back through the same staging block (no allocation).
  ASSERT_TRUE(dev.value().to_host(staging.value()).is_ok());
  EXPECT_DOUBLE_EQ(staging.value().span()[0], 1.0);
  EXPECT_DOUBLE_EQ(staging.value().span()[2], 3.0);

  // Too-small staging is an error, not a crash.
  auto tiny = fp::gpu::HostBuffer<double>::alloc(1);
  ASSERT_TRUE(tiny.is_ok());
  EXPECT_FALSE(dev.value().copy_from(tiny.value()).is_ok());
  EXPECT_FALSE(dev.value().to_host(tiny.value()).is_ok());

  // Move-only, like Buffer.
  auto moved = std::move(staging.value());
  EXPECT_EQ(moved.size(), 4u);
}

TEST(Gpu, RowAndColumnKernels) {
  // 2 x 3 row-major
  auto m = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
  ASSERT_TRUE(m.is_ok());

  auto rs = fp::gpu::row_sums(m.value(), 2, 3);
  ASSERT_TRUE(rs.is_ok());
  EXPECT_EQ(rs.value().to_host().value(), (std::vector<double>{6.0, 15.0}));

  auto rm = fp::gpu::row_means(m.value(), 2, 3);
  ASSERT_TRUE(rm.is_ok());
  EXPECT_EQ(rm.value().to_host().value(), (std::vector<double>{2.0, 5.0}));

  auto cs = fp::gpu::col_sums(m.value(), 2, 3);
  ASSERT_TRUE(cs.is_ok());
  EXPECT_EQ(cs.value().to_host().value(), (std::vector<double>{5.0, 7.0, 9.0}));

  auto bias =
      fp::gpu::Buffer<double>::from_host(std::vector<double>{10.0, 20.0, 30.0});
  ASSERT_TRUE(bias.is_ok());
  ASSERT_TRUE(fp::gpu::add_row_broadcast(m.value(), 2, 3, bias.value()).is_ok());
  EXPECT_EQ(m.value().to_host().value(),
            (std::vector<double>{11.0, 22.0, 33.0, 14.0, 25.0, 36.0}));

  // Shape errors.
  EXPECT_FALSE(fp::gpu::row_sums(m.value(), 3, 3).is_ok());
  EXPECT_FALSE(fp::gpu::row_means(m.value(), 2, 0).is_ok());
  EXPECT_FALSE(fp::gpu::col_sums(m.value(), 2, 2).is_ok());
  EXPECT_FALSE(fp::gpu::add_row_broadcast(m.value(), 2, 3, m.value()).is_ok());
}

TEST(Gpu, SoftmaxRowsWorkGroupMatchesPortable) {
  const std::vector<double> input{1.0, 2.0, 3.0, 0.5, -1.0, 0.0};
  auto a = fp::gpu::Buffer<double>::from_host(input);
  auto b = fp::gpu::Buffer<double>::from_host(input);
  ASSERT_TRUE(a.is_ok());
  ASSERT_TRUE(b.is_ok());

  ASSERT_TRUE(fp::gpu::softmax_rows(a.value(), 2, 3).is_ok());
  ASSERT_TRUE(fp::gpu::softmax_rows_wg(b.value(), 2, 3).is_ok());

  const auto portable = a.value().to_host().value();
  const auto work_group = b.value().to_host().value();
  ASSERT_EQ(portable.size(), work_group.size());
  for (std::size_t i = 0; i < portable.size(); ++i)
    EXPECT_NEAR(portable[i], work_group[i], 1e-12);

  EXPECT_FALSE(fp::gpu::softmax_rows_wg(b.value(), 3, 3).is_ok());
}

TEST(Gpu, ScratchReduceAndDot) {
  fp::gpu::Scratch<double> scratch;
  const std::vector<double> values(5000, 1.5); // spans several chunks
  auto buf = fp::gpu::Buffer<double>::from_host(values);
  ASSERT_TRUE(buf.is_ok());

  const auto total = fp::gpu::reduce(buf.value(), 0.0, scratch);
  ASSERT_TRUE(total.is_ok());
  EXPECT_NEAR(total.value(), 7500.0, 1e-9);

  const auto square = fp::gpu::dot(buf.value(), buf.value(), scratch);
  ASSERT_TRUE(square.is_ok());
  EXPECT_NEAR(square.value(), 5000.0 * 2.25, 1e-9);

  // The scratch is reused across calls of different sizes.
  auto small = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0});
  ASSERT_TRUE(small.is_ok());
  const auto with_init = fp::gpu::reduce(small.value(), 1.0, scratch);
  ASSERT_TRUE(with_init.is_ok());
  EXPECT_NEAR(with_init.value(), 4.0, 1e-12);

  // Mismatched dot is still an error with a scratch.
  EXPECT_FALSE(fp::gpu::dot(small.value(), buf.value(), scratch).is_ok());
}

TEST(Gpu, Transpose) {
  // 2 x 3 -> 3 x 2
  auto m = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
  ASSERT_TRUE(m.is_ok());

  auto t = fp::gpu::transpose(m.value(), 2, 3);
  ASSERT_TRUE(t.is_ok());
  EXPECT_EQ(t.value().to_host().value(),
            (std::vector<double>{1.0, 4.0, 2.0, 5.0, 3.0, 6.0}));

  // Transposing twice is the identity.
  auto back = fp::gpu::transpose(t.value(), 3, 2);
  ASSERT_TRUE(back.is_ok());
  EXPECT_EQ(back.value().to_host().value(), m.value().to_host().value());

  EXPECT_FALSE(fp::gpu::transpose(m.value(), 3, 3).is_ok());
}

TEST(Gpu, IndexedAndZipKernels) {
  auto buf = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0,
                                                                  3.0, 4.0});
  ASSERT_TRUE(buf.is_ok());
  ASSERT_TRUE(fp::gpu::transform_inplace_indexed(
                  buf.value(),
                  [](std::size_t i, double x) { return x + double(i); })
                  .is_ok());
  EXPECT_EQ(buf.value().to_host().value(),
            (std::vector<double>{1.0, 3.0, 5.0, 7.0}));

  auto a = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0, 3.0});
  auto b =
      fp::gpu::Buffer<double>::from_host(std::vector<double>{10.0, 20.0, 30.0});
  ASSERT_TRUE(a.is_ok());
  ASSERT_TRUE(b.is_ok());
  ASSERT_TRUE(fp::gpu::zip_transform_inplace(
                  a.value(), b.value(),
                  [](double x, double y) { return x + y; })
                  .is_ok());
  EXPECT_EQ(a.value().to_host().value(), (std::vector<double>{11.0, 22.0, 33.0}));

  auto c = fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 1.0, 1.0});
  ASSERT_TRUE(c.is_ok());
  ASSERT_TRUE(fp::gpu::zip3_transform_inplace(
                  a.value(), b.value(), c.value(),
                  [](double x, double y, double z) { return x + y * z; })
                  .is_ok());
  EXPECT_EQ(a.value().to_host().value(), (std::vector<double>{21.0, 42.0, 63.0}));

  // Size errors.
  auto small =
      fp::gpu::Buffer<double>::from_host(std::vector<double>{1.0, 2.0});
  ASSERT_TRUE(small.is_ok());
  EXPECT_FALSE(fp::gpu::zip_transform_inplace(
                   small.value(), b.value(),
                   [](double x, double y) { return x + y; })
                   .is_ok());
  EXPECT_FALSE(fp::gpu::zip3_transform_inplace(
                   small.value(), b.value(), c.value(),
                   [](double x, double y, double z) { return x + y + z; })
                   .is_ok());
}

TEST(Gpu, Matmul) {
  auto a = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0}); // 2 x 3
  auto b = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{7.0, 8.0, 9.0, 10.0, 11.0, 12.0}); // 3 x 2
  ASSERT_TRUE(a.is_ok());
  ASSERT_TRUE(b.is_ok());

  auto c = fp::gpu::matmul(a.value(), b.value(), 2, 3, 2);
  ASSERT_TRUE(c.is_ok());
  EXPECT_EQ(c.value().to_host().value(),
            (std::vector<double>{58.0, 64.0, 139.0, 154.0}));

  // Shape mismatch is an error.
  EXPECT_FALSE(fp::gpu::matmul(a.value(), b.value(), 3, 3, 2).is_ok());
}

TEST(Gpu, MatmulNonTileMultiple) {
  // Sizes that are not multiples of the 16x16 tile exercise the masking.
  constexpr std::size_t m = 5, k = 3, n = 7;
  std::vector<double> ha(m * k), hb(k * n);
  for (std::size_t i = 0; i < ha.size(); ++i)
    ha[i] = static_cast<double>(i + 1);
  for (std::size_t i = 0; i < hb.size(); ++i)
    hb[i] = static_cast<double>(i) * 0.5;

  auto a = fp::gpu::Buffer<double>::from_host(ha);
  auto b = fp::gpu::Buffer<double>::from_host(hb);
  ASSERT_TRUE(a.is_ok());
  ASSERT_TRUE(b.is_ok());

  auto c = fp::gpu::matmul(a.value(), b.value(), m, k, n);
  ASSERT_TRUE(c.is_ok());

  // Reference through fp::linalg on the same row-major data.
  std::vector<std::vector<double>> ga(m, std::vector<double>(k));
  std::vector<std::vector<double>> gb(k, std::vector<double>(n));
  for (std::size_t i = 0; i < m; ++i)
    std::copy_n(ha.begin() + i * k, k, ga[i].begin());
  for (std::size_t i = 0; i < k; ++i)
    std::copy_n(hb.begin() + i * n, n, gb[i].begin());
  const auto gc = fp::matmul(ga, gb);

  const auto flat = c.value().to_host().value();
  for (std::size_t i = 0; i < m; ++i)
    for (std::size_t j = 0; j < n; ++j)
      EXPECT_NEAR(flat[i * n + j], gc[i][j], 1e-12);
}

TEST(Gpu, BatchedMatmul) {
  // Two 2x2 products: identity * B, then 2I * I.
  auto a = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 0.0, 0.0, 1.0, 2.0, 0.0, 0.0, 2.0});
  auto b = fp::gpu::Buffer<double>::from_host(
      std::vector<double>{1.0, 2.0, 3.0, 4.0, 1.0, 0.0, 0.0, 1.0});
  ASSERT_TRUE(a.is_ok());
  ASSERT_TRUE(b.is_ok());

  auto c = fp::gpu::batched_matmul(a.value(), b.value(), 2, 2, 2, 2);
  ASSERT_TRUE(c.is_ok());
  EXPECT_EQ(c.value().to_host().value(),
            (std::vector<double>{1.0, 2.0, 3.0, 4.0, 2.0, 0.0, 0.0, 2.0}));

  EXPECT_FALSE(fp::gpu::batched_matmul(a.value(), b.value(), 3, 2, 2, 2).is_ok());
}

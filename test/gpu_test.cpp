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

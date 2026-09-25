#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

TEST(Memory, MoveForwardExchange) {
  int a = 1;
  const int old = fp::exchange(a, 5);
  EXPECT_EQ(old, 1);
  EXPECT_EQ(a, 5);

  std::string s = "hello";
  std::string t = fp::move(s);
  EXPECT_EQ(t, "hello");
}

TEST(Memory, PtrRefAsConst) {
  int x = 3;
  EXPECT_EQ(fp::ptr(x), &x);
  EXPECT_EQ(fp::ref(fp::ptr(x)), 3);
  EXPECT_EQ(fp::deref(fp::ptr(x)), 3);

  const int &c = fp::as_const(x);
  EXPECT_EQ(c, 3);
}

TEST(Memory, ConstructAndDestroy) {
  alignas(std::string) std::byte raw[sizeof(std::string)];
  auto *s = fp::construct_at(reinterpret_cast<std::string *>(raw), "hi");
  EXPECT_EQ(*s, "hi");
  fp::destroy_at(s);
}

TEST(Memory, BufferAllocAndRange) {
  auto buf = fp::Buffer<int>::alloc(4);
  ASSERT_TRUE(buf.is_ok());

  fp::fill(buf.value(), 7);
  EXPECT_EQ(fp::fold_left(buf.value(), 0, fp::plus), 28);

  buf.value()[0] = 1;
  EXPECT_EQ(buf.value().span().size(), 4u);
  EXPECT_EQ(*buf.value().begin(), 1);
}

TEST(Memory, BufferZerosCloneResize) {
  auto zeros = fp::Buffer<double>::zeros(3);
  ASSERT_TRUE(zeros.is_ok());
  EXPECT_DOUBLE_EQ(fp::fold_left(zeros.value(), 0.0, fp::plus), 0.0);

  auto copy = zeros.value().clone();
  ASSERT_TRUE(copy.is_ok());
  EXPECT_EQ(copy.value().size(), 3u);

  auto bigger = zeros.value().resized(5);
  ASSERT_TRUE(bigger.is_ok());
  EXPECT_EQ(bigger.value().size(), 5u);
}

TEST(Memory, BufferMoveAndAdopt) {
  auto a = fp::Buffer<int>::from({1, 2, 3});
  ASSERT_TRUE(a.is_ok());

  auto b = std::move(a.value());
  EXPECT_EQ(b.size(), 3u);

  int *raw = b.release();
  EXPECT_EQ(b.size(), 0u);
  EXPECT_EQ(raw[0], 1);

  auto c = fp::Buffer<int>::adopt(raw, 3);
  EXPECT_EQ(fp::fold_left(c, 0, fp::plus), 6);
}

TEST(Memory, BoxMakeAndRelease) {
  auto box = fp::Box<std::string>::make("boxed");
  ASSERT_TRUE(box.is_ok());
  EXPECT_EQ(*box.value(), "boxed");
  EXPECT_TRUE(static_cast<bool>(box.value()));

  std::string *raw = box.value().release();
  EXPECT_EQ(*raw, "boxed");
  delete raw;
}

TEST(Memory, BoxReset) {
  auto box = fp::Box<int>::make(1);
  ASSERT_TRUE(box.is_ok());
  box.value().reset(new int(9));
  EXPECT_EQ(*box.value(), 9);
  box.value().reset();
  EXPECT_FALSE(static_cast<bool>(box.value()));
}

TEST(Memory, SharedAndShare) {
  auto shared = fp::make_shared<std::string>("shared");
  ASSERT_TRUE(shared.is_ok());
  EXPECT_EQ(*shared.value(), "shared");

  auto box = fp::Box<int>::make(42);
  ASSERT_TRUE(box.is_ok());
  auto moved = fp::share(std::move(box.value()));
  EXPECT_EQ(*moved, 42);
}

TEST(Memory, WithBuffer) {
  auto total = fp::with_buffer<double>(4, [](std::span<double> scratch) {
    // `Buffer::alloc` (and therefore `with_buffer`) hands out raw storage:
    // write before reading, or the sum reads uninitialized memory.
    fp::fill(scratch, 1.0);
    fp::transform_inplace(scratch, [](double x) { return x + 1.0; });
    return fp::fold_left(scratch, 0.0, fp::plus);
  });
  ASSERT_TRUE(total.is_ok());
  EXPECT_DOUBLE_EQ(total.value(), 8.0);
}

TEST(Memory, WithBufferVoid) {
  bool called = false;
  auto r = fp::with_buffer<int>(2, [&](std::span<int> s) {
    called = true;
    s[0] = 1;
  });
  EXPECT_TRUE(r.is_ok());
  EXPECT_TRUE(called);
}

TEST(Memory, WithPtr) {
  auto r = fp::with_ptr<int>(3, [](int *p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
      p[i] = static_cast<int>(i);
    return p[2];
  });
  ASSERT_TRUE(r.is_ok());
  EXPECT_EQ(r.value(), 2);
}

TEST(Memory, CopyHelpers) {
  const std::vector<int> src{1, 2, 3};
  std::vector<int> dst(3, 0);

  fp::copy_bytes<int>(dst, src);
  EXPECT_EQ(dst, src);

  fp::fill_bytes<int>(dst, std::byte{0});
  EXPECT_EQ(dst, (std::vector<int>{0, 0, 0}));

  std::vector<int> small(2, 0);
  auto r = fp::copy_into<int>(small, src);
  EXPECT_FALSE(r.is_ok());
}

TEST(Memory, AlignedBufferRoundsAndAligns) {
  auto buf = fp::AlignedBuffer::alloc(100, 512);
  ASSERT_TRUE(buf.is_ok()) << buf.error();
  EXPECT_EQ(buf.value().size(), 512u);
  EXPECT_EQ(buf.value().alignment(), 512u);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buf.value().data()) % 512, 0u);
  EXPECT_EQ(buf.value().span().size(), buf.value().size());
  EXPECT_EQ(buf.value().end() - buf.value().begin(), 512);
}

TEST(Memory, AlignedBufferRejectsBadAlignment) {
  EXPECT_FALSE(fp::AlignedBuffer::alloc(64, 0).is_ok());
  EXPECT_FALSE(fp::AlignedBuffer::alloc(64, 3).is_ok());
}

TEST(Memory, AlignedBufferEmptyAllocatesNothing) {
  auto buf = fp::AlignedBuffer::alloc(0, 4096);
  ASSERT_TRUE(buf.is_ok()) << buf.error();
  EXPECT_TRUE(buf.value().empty());
  EXPECT_EQ(buf.value().size(), 0u);
  EXPECT_EQ(buf.value().data(), nullptr);
  EXPECT_EQ(buf.value().alignment(), 4096u);
}

TEST(Memory, AlignedBufferMovesAndClones) {
  auto buf = fp::AlignedBuffer::alloc(64, 64);
  ASSERT_TRUE(buf.is_ok()) << buf.error();
  buf.value().fill(std::byte{0xAB});

  auto copy = buf.value().clone();
  ASSERT_TRUE(copy.is_ok());
  EXPECT_EQ(copy.value().size(), 64u);
  EXPECT_EQ(copy.value().data()[0], std::byte{0xAB});

  fp::AlignedBuffer moved = fp::move(buf.value());
  EXPECT_EQ(moved.size(), 64u);
  EXPECT_EQ(buf.value().data(), nullptr);
  EXPECT_EQ(buf.value().size(), 0u);
  EXPECT_TRUE(buf.value().empty());
}

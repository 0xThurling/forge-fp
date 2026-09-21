#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <string>

namespace {
struct alignas(64) CacheLine {
  int value;
};
} // namespace

TEST(Arena, AllocAndWrite) {
  fp::Arena arena;
  int *xs = arena.alloc<int>(4);
  for (int i = 0; i < 4; ++i)
    xs[i] = i * i;
  EXPECT_EQ(xs[3], 9);
  EXPECT_GT(arena.used(), 0u);
}

TEST(Arena, MakeConstructs) {
  fp::Arena arena;
  auto *s = arena.make<std::string>("hello");
  EXPECT_EQ(*s, "hello");
  auto *p = arena.make<std::pair<int, int>>(1, 2);
  EXPECT_EQ(p->second, 2);
}

TEST(Arena, OverAlignedTypes) {
  fp::Arena arena;
  for (int i = 0; i < 8; ++i) {
    auto *c = arena.alloc<CacheLine>();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(c) % 64, 0u);
    c->value = i;
  }
}

TEST(Arena, ResetReusesMemory) {
  fp::Arena arena;
  arena.alloc<int>(100);
  auto used = arena.used();
  arena.reset();
  EXPECT_EQ(arena.used(), 0u);
  arena.alloc<int>(100);
  EXPECT_EQ(arena.used(), used);
}

TEST(Arena, GrowsAcrossBlocks) {
  fp::Arena arena(64);
  for (int i = 0; i < 100; ++i)
    *arena.alloc<int>() = i;
}

TEST(Arena, WithArena) {
  auto result = fp::with_arena(1024, [](fp::Arena &a) {
    auto *v = a.make<std::string>("scoped");
    return v->size();
  });
  EXPECT_EQ(result, 6u);
}

TEST(Arena, PointerStability) {
  fp::Arena arena(128);
  auto *first = arena.alloc<int>(1);
  *first = 42;
  for (int i = 0; i < 1000; ++i)
    arena.alloc<std::uint64_t>();
  EXPECT_EQ(*first, 42);
}

TEST(Arena, AllocBytesAndSpan) {
  fp::Arena arena;
  auto bytes = arena.alloc_bytes(16);
  EXPECT_EQ(bytes.size(), 16u);

  auto xs = arena.alloc_span<int>(4);
  EXPECT_EQ(xs.size(), 4u);
  xs[0] = 42;
  EXPECT_EQ(xs[0], 42);
}

TEST(Arena, MarkAndResetTo) {
  fp::Arena arena(1024);
  auto *keep = arena.alloc<int>(1);
  *keep = 7;

  const auto mark = arena.mark();
  arena.alloc<int>(100);
  EXPECT_GT(arena.used(), mark);

  arena.reset_to(mark);
  EXPECT_EQ(arena.used(), mark);
  EXPECT_EQ(*keep, 7);
}

TEST(Arena, WithArenaScope) {
  fp::Arena arena(1024);
  auto *keep = arena.alloc<int>(1);
  *keep = 1;
  const auto before = arena.used();

  const auto result = fp::with_arena_scope(arena, [](fp::Arena &a) {
    auto s = a.alloc_span<int>(8);
    s[0] = 5;
    return s[0];
  });

  EXPECT_EQ(result, 5);
  EXPECT_EQ(arena.used(), before);
  EXPECT_EQ(*keep, 1);
}

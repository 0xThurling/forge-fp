#include <fp/all.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string temp_path(std::string const &name) {
  return std::string(::testing::TempDir()) + "forgefp_" + name;
}
} // namespace

TEST(Io, WriteReadRoundTrip) {
  auto path = temp_path("io_roundtrip.txt");
  ASSERT_TRUE(fp::write_file(path, "line one\nline two\n").is_ok());

  auto content = fp::read_file(path);
  ASSERT_TRUE(content.is_ok());
  EXPECT_EQ(content.value(), "line one\nline two\n");

  auto ls = fp::read_lines(path);
  ASSERT_TRUE(ls.is_ok());
  EXPECT_EQ(ls.value(), (std::vector<std::string>{"line one", "line two"}));
}

TEST(Io, MissingFileIsError) {
  EXPECT_FALSE(fp::read_file(temp_path("does_not_exist_12345.txt")).is_ok());
  EXPECT_FALSE(fp::read_lines(temp_path("does_not_exist_12345.txt")).is_ok());
  EXPECT_FALSE(fp::read_bytes(temp_path("does_not_exist_12345.bin")).is_ok());
}

TEST(Io, BytesRoundTrip) {
  auto path = temp_path("io_bytes.bin");
  std::vector<std::byte> data = {std::byte{1}, std::byte{2}, std::byte{255}};
  ASSERT_TRUE(fp::write_bytes(path, data).is_ok());

  auto back = fp::read_bytes(path);
  ASSERT_TRUE(back.is_ok());
  EXPECT_EQ(back.value(), data);

  EXPECT_TRUE(fp::exists(path));
  EXPECT_FALSE(fp::exists(temp_path("nope_12345.bin")));
}

TEST(Io, WriteLinesAndEnsureDirectory) {
  auto dir = temp_path("io_dir/sub");
  ASSERT_TRUE(fp::ensure_directory(dir).is_ok());
  ASSERT_TRUE(fp::ensure_directory(dir).is_ok()); // idempotent

  auto path = dir + "/lines.txt";
  ASSERT_TRUE(fp::write_lines(path, {"a", "b"}).is_ok());

  auto lines = fp::read_lines(path);
  ASSERT_TRUE(lines.is_ok());
  EXPECT_EQ(lines.value(), (std::vector<std::string>{"a", "b"}));
  EXPECT_TRUE(fp::exists(path));
}

TEST(Io, LiftAndInteract) {
  auto lifted = fp::lift_io([](std::string s) { return s.size(); });
  EXPECT_EQ(lifted("hello").value(), 5u);

  std::istringstream in("a\nb\n");
  std::ostringstream out;
  auto *old_in = std::cin.rdbuf(in.rdbuf());
  auto *old_out = std::cout.rdbuf(out.rdbuf());
  std::string line;
  std::getline(std::cin, line);
  std::cout << line;
  std::cin.rdbuf(old_in);
  std::cout.rdbuf(old_out);
  EXPECT_EQ(out.str(), "a");
}

TEST(Input, ReadLineAndAll) {
  std::istringstream in("first\nsecond\n");
  EXPECT_EQ(fp::read_line(in).value(), "first");
  EXPECT_EQ(fp::read_line(in).value(), "second");
  EXPECT_FALSE(fp::read_line(in).is_ok());

  std::istringstream all("x\ny\n");
  EXPECT_EQ(fp::read_all(all).value(), "x\ny\n");
}

TEST(Input, ReadLinesStream) {
  std::istringstream in("1\n2\n3\n");
  auto lines = fp::read_lines(in).collect();
  EXPECT_EQ(lines, (std::vector<std::string>{"1", "2", "3"}));
}

TEST(Input, ReadCharAndChars) {
  std::istringstream in("ab");
  EXPECT_EQ(fp::read_char(in).value(), 'a');

  std::istringstream all("xyz");
  auto chars = fp::read_chars(all).collect();
  EXPECT_EQ(chars, (std::vector<char>{'x', 'y', 'z'}));
  EXPECT_FALSE(fp::read_char(all).is_ok());
}

TEST(Input, FeedLinesIntoChannel) {
  fp::Channel<std::string> ch;
  std::istringstream in("hello\nworld\n");
  fp::feed_lines(ch, in);
  EXPECT_EQ(ch.recv(), "hello");
  EXPECT_EQ(ch.recv(), "world");
  ch.close();
  EXPECT_FALSE(ch.try_recv().has_value());
}

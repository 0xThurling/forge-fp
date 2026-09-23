#include <fp/reflect.hpp>

#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <string>
#include <type_traits>
#include <vector>

namespace {

struct Point {
  int x;
  float y;
  char tag;
};

struct Empty {};

struct Nested {
  Point p;
  std::string label;
};

struct NonAggregate {
  explicit NonAggregate(int v) : value(v) {}
  int value;
};

union Union {
  int i;
  float f;
};

enum class Colour { Red, Green, Blue };

enum class Big : unsigned char { A = 200, B = 255 };

// A non-aggregate that opts in through the FieldAccess customization point.
struct Widget {
  explicit Widget(int v) : value(v) {}
  int value;
};

struct WithStrings {
  std::string label;
  int id;
  bool flag;
  double ratio;
  Colour colour = Colour::Red;
};

struct Reordered {
  double ratio;
  std::string label;
  Colour colour = Colour::Red;
  int id;
  bool flag;
};

struct MembersWidget {
  int a = 1;
  float b = 2.5f;
};

// Used only by the registry concurrency test, so it cannot disturb the
// canonical name of a type other tests assert on.
struct RegistryProbe {
  int value;
};

} // namespace

namespace fp::reflect {
template <> struct FieldAccess<Widget> {
  static constexpr std::size_t count = 1;
  static constexpr FieldInfo value[] = {
      {"value", type_id<int>(), type_name<int>(), sizeof(int)}};
  static constexpr FieldInfo const *fields = value;
  template <std::size_t I, class Self>
  static constexpr auto &&get(Self &&w) noexcept {
    static_assert(I == 0);
    return std::forward<Self>(w).value;
  }
};
} // namespace fp::reflect

using fp::reflect::FieldInfo;
using fp::reflect::type_id;
using fp::reflect::type_name;

// A non-aggregate exposing fields through the `members<...>` helper.
struct MembersAccess : fp::reflect::members<&MembersWidget::a, &MembersWidget::b> {
  static constexpr FieldInfo value[] = {
      {"a", type_id<int>(), type_name<int>(), sizeof(int)},
      {"b", type_id<float>(), type_name<float>(), sizeof(float)}};
  static constexpr FieldInfo const *fields = value;
};


TEST(Reflect, DynamicFieldByZeroIndexLiteral) {
  // Regression: `field(p, 0)` must pick the index overload, not the
  // std::string_view one (0 is a null pointer constant).
  Point p{7, 1.5f, 'a'};
  auto by_literal = fp::reflect::field(p, 0);
  ASSERT_TRUE(by_literal.is_ok());
  EXPECT_EQ(*by_literal.value().as<int>(), 7);
  auto by_size_t = fp::reflect::field(p, std::size_t{0});
  ASSERT_TRUE(by_size_t.is_ok());
  EXPECT_EQ(*by_size_t.value().as<int>(), 7);
  EXPECT_FALSE(fp::reflect::field(p, std::size_t{9}).is_ok());
}

TEST(Reflect, DescribeShape) {
  constexpr auto shape = fp::reflect::describe_shape<Point>();
  static_assert(shape.size == sizeof(Point));
  static_assert(shape.align == alignof(Point));
  static_assert(shape.kind == fp::reflect::TypeKind::Class);
  static_assert(shape.fields.size() == 3);
  static_assert(shape.fields[0].name == "field0");
  static_assert(shape.fields[0].type == type_id<int>());
  // same shape as describe(), different names
  constexpr auto named = fp::reflect::describe<Point>();
  EXPECT_EQ(shape.fields.size(), named.fields.size());
  EXPECT_EQ(shape.fields[1].type, named.fields[1].type);
}

TEST(Reflect, WholeObjectToString) {
  // Test types live in an anonymous namespace, so build the expectation from
  // the same spelled names.
  const std::string point = std::string(fp::reflect::type_name<Point>());
  const std::string nested = std::string(fp::reflect::type_name<Nested>());

  Point p{7, 1.5f, 'a'};
  EXPECT_EQ(fp::reflect::to_string(p),
            point + "{x=7, y=1.5, tag='a'}");

  Nested n{{1, 2.0f, 'b'}, "hi"};
  EXPECT_EQ(fp::reflect::to_string(n),
            nested + "{p=" + point + "{x=1, y=2, tag='b'}, label=hi}");

  WithStrings w{"s", 3, true, 0.25, Colour::Green};
  const auto text = fp::reflect::to_string(w);
  EXPECT_NE(text.find("label=s"), std::string::npos);
  EXPECT_NE(text.find("id=3"), std::string::npos);
  EXPECT_NE(text.find("flag=true"), std::string::npos);
  EXPECT_NE(text.find("ratio=0.25"), std::string::npos);
  EXPECT_NE(text.find("colour=Green"), std::string::npos);
}

TEST(Reflect, Equal) {
  Point a{1, 2.0f, 'x'}, b{1, 2.0f, 'x'};
  EXPECT_TRUE(fp::reflect::equal(a, b));
  b.y = 3.0f;
  EXPECT_FALSE(fp::reflect::equal(a, b));

  // matching by name, not by position
  WithStrings w{"hi", 1, true, 0.5, Colour::Blue};
  Reordered r{0.5, "hi", Colour::Blue, 1, true};
  EXPECT_TRUE(fp::reflect::equal(w, r));
  r.id = 2;
  EXPECT_FALSE(fp::reflect::equal(w, r));

  // different field sets
  EXPECT_FALSE(fp::reflect::equal(a, Nested{}));

  // nested reflected fields compare recursively
  Nested n1{{1, 2.0f, 'c'}, "a"}, n2{{1, 2.0f, 'c'}, "a"};
  EXPECT_TRUE(fp::reflect::equal(n1, n2));
  n2.p.y = 9.0f;
  EXPECT_FALSE(fp::reflect::equal(n1, n2));
}

TEST(Reflect, CopyFields) {
  WithStrings src{"hello", 42, true, 1.25, Colour::Blue};
  Reordered dst{};
  EXPECT_EQ(fp::reflect::copy_fields(dst, src), 5u);
  EXPECT_EQ(dst.label, "hello");
  EXPECT_EQ(dst.id, 42);
  EXPECT_TRUE(dst.flag);
  EXPECT_DOUBLE_EQ(dst.ratio, 1.25);
  EXPECT_EQ(dst.colour, Colour::Blue);

  // fields that do not exist on the destination are skipped, and the count says so
  Point only_two_fields{1, 2.0f, 'z'};
  WithStrings partial{};
  EXPECT_EQ(fp::reflect::copy_fields(partial, only_two_fields), 0u);
  EXPECT_EQ(partial.id, 0);
}

TEST(Reflect, CompileTimeFieldQueries) {
  static_assert(fp::reflect::has_field<Point>("x"));
  static_assert(fp::reflect::has_field<Point>("tag"));
  static_assert(!fp::reflect::has_field<Point>("nope"));
  static_assert(fp::reflect::field_index<Point>("x") == 0);
  static_assert(fp::reflect::field_index<Point>("tag") == 2);
  static_assert(fp::reflect::field_index<Point>("nope") ==
                fp::reflect::no_field);
  static_assert(fp::reflect::has_field<WithStrings>("colour"));

  // usable for compile-time dispatch
  constexpr bool has_position = fp::reflect::has_field<Point>("x");
  static_assert(has_position);
  EXPECT_TRUE(has_position);
}

TEST(Reflect, MembersHelper) {
  static_assert(MembersAccess::count == 2);
  EXPECT_TRUE(fp::reflect::has_fields<MembersWidget>());
  EXPECT_EQ(fp::reflect::arity<MembersWidget>(), 2);
  MembersWidget w;
  EXPECT_EQ(MembersAccess::get<0>(w), 1);
  EXPECT_FLOAT_EQ(MembersAccess::get<1>(w), 2.5f);
  MembersAccess::get<1>(w) = 3.5f;
  EXPECT_FLOAT_EQ(w.b, 3.5f);
  const MembersWidget cw{4, 5.0f};
  static_assert(std::is_same_v<decltype(MembersAccess::get<0>(cw)),
                               const int &>);
  EXPECT_EQ(MembersAccess::get<0>(cw), 4);

  // the dynamic API sees it like any other reflected type
  auto found = fp::reflect::field(w, "b");
  ASSERT_TRUE(found.is_ok());
  EXPECT_FLOAT_EQ(*found.value().as<float>(), 3.5f);
  const auto info = fp::reflect::describe<MembersWidget>();
  ASSERT_EQ(info.fields.size(), 2u);
  EXPECT_EQ(info.fields[0].name, "a");
}

TEST(Reflect, FormatValueKinds) {
  struct Mixed {
    bool b = true;
    char c = 'z';
    int i = -7;
    double d = 0.5;
    const char *p = nullptr;
    Colour e = Colour::Green;
    std::string s = "text";
    MembersWidget nested{};
  } m;

  const auto field_text = [&](std::string_view name) {
    auto r = fp::reflect::field(m, name);
    return r.is_ok() ? fp::reflect::to_string(r.value()) : "<error>";
  };
  EXPECT_EQ(field_text("b"), "true");
  EXPECT_EQ(field_text("c"), "'z'");
  EXPECT_EQ(field_text("i"), "-7");
  EXPECT_EQ(field_text("d"), "0.5");
  EXPECT_EQ(field_text("p"), "0x0");
  EXPECT_EQ(field_text("e"), "Green");
  EXPECT_EQ(field_text("s"), "text");
  EXPECT_NE(field_text("nested").find("MembersWidget{"), std::string::npos);
}

TEST(Reflect, RegistryReadsWhileWriting) {
  // A light concurrency smoke test; run under TSan for real coverage.
  const std::string name(fp::reflect::type_name<RegistryProbe>());
  (void)fp::reflect::type_info<RegistryProbe>(); // register the canonical name
  std::atomic<int> reads{0};
  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i)
    readers.emplace_back([&] {
      for (int n = 0; n < 2000; ++n)
        if (fp::reflect::find_type(name))
          reads.fetch_add(1, std::memory_order_relaxed);
    });
  for (int i = 0; i < 50; ++i)
    fp::reflect::register_type<RegistryProbe>("RegistryProbeAlias");
  for (auto &t : readers)
    t.join();
  EXPECT_GT(reads.load(), 0);
  EXPECT_TRUE(fp::reflect::find_type("RegistryProbeAlias").has_value());
}

TEST(Reflect, TypeId) {
  static_assert(fp::reflect::type_id<int>() == fp::reflect::type_id<int>());
  static_assert(fp::reflect::type_id<int>() ==
                fp::reflect::type_id<const int &>());
  // Inequality via EXPECT: comparing the addresses of two distinct objects is
  // not a constant expression under some sanitizers (ASan).
  EXPECT_NE(fp::reflect::type_id<int>(), fp::reflect::type_id<float>());
  EXPECT_NE(fp::reflect::type_id<Point>(), fp::reflect::type_id<Empty>());
  EXPECT_NE(fp::reflect::type_id<int>(), nullptr);
}

TEST(Reflect, TypeName) {
  static_assert(fp::reflect::type_name<int>() == "int");
  static_assert(fp::reflect::type_name<const int &>() == "int");
  // Test types live in an anonymous namespace, so their spelled names carry
  // the namespace prefix; check the readable part.
  EXPECT_NE(fp::reflect::type_name<Point>().find("Point"), std::string::npos);
  EXPECT_NE(fp::reflect::type_name<Colour>().find("Colour"), std::string::npos);
  EXPECT_NE(fp::reflect::type_name<std::vector<int>>().find("vector"),
            std::string::npos);
}

TEST(Reflect, TypeKind) {
  using fp::reflect::TypeKind;
  static_assert(fp::reflect::type_kind<void>() == TypeKind::Void);
  static_assert(fp::reflect::type_kind<bool>() == TypeKind::Bool);
  static_assert(fp::reflect::type_kind<int>() == TypeKind::Integral);
  static_assert(fp::reflect::type_kind<double>() == TypeKind::Floating);
  static_assert(fp::reflect::type_kind<Colour>() == TypeKind::Enum);
  static_assert(fp::reflect::type_kind<int *>() == TypeKind::Pointer);
  static_assert(fp::reflect::type_kind<int[3]>() == TypeKind::Array);
  static_assert(fp::reflect::type_kind<Union>() == TypeKind::Union);
  static_assert(fp::reflect::type_kind<Point>() == TypeKind::Class);
  EXPECT_EQ(fp::reflect::to_string(TypeKind::Integral), "integral");
}

TEST(Reflect, Arity) {
  static_assert(fp::reflect::arity<Point>() == 3);
  static_assert(fp::reflect::arity<Empty>() == 0);
  static_assert(fp::reflect::arity<Nested>() == 2);
  static_assert(fp::reflect::arity<NonAggregate>() == 0);
  static_assert(fp::reflect::arity<Union>() == 0);
  static_assert(fp::reflect::arity<int>() == 0);
  static_assert(fp::reflect::arity<int[3]>() == 3); // arrays reflect element-wise
  static_assert(fp::reflect::arity<Widget>() == 1); // via FieldAccess
  static_assert(fp::reflect::has_fields<Point>());
  static_assert(!fp::reflect::has_fields<Empty>());
  static_assert(!fp::reflect::has_fields<NonAggregate>());
}

TEST(Reflect, ArrayFields) {
  int values[3] = {10, 20, 30};
  static_assert(fp::reflect::arity<int[3]>() == 3);
  EXPECT_EQ(fp::reflect::field_at<0>(values), 10);
  EXPECT_EQ(fp::reflect::field_at<2>(values), 30);
  fp::reflect::field_at<1>(values) = 21;
  EXPECT_EQ(values[1], 21);
  EXPECT_EQ(fp::reflect::fields<int[3]>().size(), 3u);
  EXPECT_EQ(fp::reflect::describe<int[3]>().kind, fp::reflect::TypeKind::Array);
}

TEST(Reflect, ArrayMembersDoNotElide) {
  // A C-array member is one field, not one field per element.
  struct WithArray {
    char name[8];
    int id;
  };
  static_assert(fp::reflect::arity<WithArray>() == 2);
  static_assert(std::is_same_v<fp::reflect::field_type<0, WithArray>, char[8]>);
  WithArray w{{'a', 'b', 'c'}, 7};
  EXPECT_EQ(fp::reflect::field_at<1>(w), 7);
  EXPECT_EQ(fp::reflect::fields<WithArray>().size(), 2u);

  // std::array is tuple-like: it reflects element-wise like a C array.
  static_assert(fp::reflect::arity<std::array<int, 3>>() == 3);
  std::array<int, 3> a{1, 2, 3};
  EXPECT_EQ(fp::reflect::field_at<2>(a), 3);
  fp::reflect::field_at<0>(a) = 10;
  EXPECT_EQ(a[0], 10);
  EXPECT_EQ((fp::reflect::fields<std::array<int, 3>>().size()), 3u);
  EXPECT_EQ((fp::reflect::field_name<std::array<int, 3>, 0>()), "field0");

  // std::pair and std::tuple go through the tuple protocol too
  static_assert(fp::reflect::arity<std::pair<int, float>>() == 2);
  std::pair<int, float> pr{1, 2.0f};
  EXPECT_EQ(fp::reflect::field_at<0>(pr), 1);
  static_assert(fp::reflect::arity<std::tuple<int, int, char>>() == 3);
}

TEST(Reflect, IndexNameFallback) {
  // The "fieldN" names used when member names are unavailable.
  static_assert(fp::reflect::detail::index_name_holder<0>::value == "field0");
  static_assert(fp::reflect::detail::index_name_holder<9>::value == "field9");
  static_assert(fp::reflect::detail::index_name_holder<10>::value == "field10");
  static_assert(fp::reflect::detail::index_name_holder<31>::value == "field31");
}

TEST(Reflect, FieldCountCap) {
  // 32 fields is the maximum automatic reflection binds; more than that is
  // reported as "not reflectable" rather than a truncated count.
#define FP_TEST_8(P)                                                           \
  int P##0;                                                                    \
  int P##1;                                                                    \
  int P##2;                                                                    \
  int P##3;                                                                    \
  int P##4;                                                                    \
  int P##5;                                                                    \
  int P##6;                                                                    \
  int P##7;
  struct Fields32 {
    FP_TEST_8(a)
    FP_TEST_8(b)
    FP_TEST_8(c)
    FP_TEST_8(d)
  };
  struct Fields33 {
    FP_TEST_8(a)
    FP_TEST_8(b)
    FP_TEST_8(c)
    FP_TEST_8(d)
    int a8;
  };
#undef FP_TEST_8

  static_assert(fp::reflect::arity<Fields32>() == 32);
  static_assert(fp::reflect::arity<Fields33>() == 0);
  EXPECT_EQ((fp::reflect::fields<Fields32>().size()), 32u);
  EXPECT_TRUE(fp::reflect::fields<Fields33>().empty());
}

TEST(Reflect, FieldAccess) {
  Point p{7, 1.5f, 'a'};
  EXPECT_EQ(fp::reflect::field_at<0>(p), 7);
  EXPECT_FLOAT_EQ(fp::reflect::field_at<1>(p), 1.5f);
  EXPECT_EQ(fp::reflect::field_at<2>(p), 'a');

  fp::reflect::field_at<0>(p) = 42;
  EXPECT_EQ(p.x, 42);

  static_assert(std::is_same_v<fp::reflect::field_type<0, Point>, int>);
  static_assert(std::is_same_v<fp::reflect::field_type<1, Point>, float>);
  static_assert(std::is_same_v<fp::reflect::field_type<0, Nested>, Point>);
  static_assert(
      std::is_same_v<fp::reflect::field_type<1, Nested>, std::string>);

  // const access keeps constness
  const Point cp{1, 2.0f, 'b'};
  static_assert(std::is_same_v<decltype(fp::reflect::field_at<0>(cp)),
                               const int &>);
  EXPECT_EQ(fp::reflect::field_at<0>(cp), 1);
}

TEST(Reflect, FieldNames) {
  if constexpr (fp::reflect::names_available) {
    EXPECT_EQ((fp::reflect::field_name<Point, 0>()), "x");
    EXPECT_EQ((fp::reflect::field_name<Point, 1>()), "y");
    EXPECT_EQ((fp::reflect::field_name<Point, 2>()), "tag");
    EXPECT_EQ((fp::reflect::field_name<Nested, 1>()), "label");
  } else {
    EXPECT_EQ((fp::reflect::field_name<Point, 0>()), "field0");
    EXPECT_EQ((fp::reflect::field_name<Point, 2>()), "field2");
  }
}

TEST(Reflect, FieldsTable) {
  const auto f = fp::reflect::fields<Point>();
  ASSERT_EQ(f.size(), 3u);
  EXPECT_EQ(f[0].type, fp::reflect::type_id<int>());
  EXPECT_EQ(f[0].size, sizeof(int));
  EXPECT_EQ(f[0].type_name, "int");
  EXPECT_EQ(f[1].type, fp::reflect::type_id<float>());
  EXPECT_EQ(f[2].type, fp::reflect::type_id<char>());
  if constexpr (fp::reflect::names_available) {
    EXPECT_EQ(f[0].name, "x");
    EXPECT_EQ(f[2].name, "tag");
  }
  EXPECT_TRUE(fp::reflect::fields<NonAggregate>().empty());
  EXPECT_TRUE(fp::reflect::fields<Empty>().empty());
  EXPECT_EQ(fp::reflect::fields<Widget>().size(), 1u);
  EXPECT_EQ(fp::reflect::fields<Widget>()[0].name, "value");
}

TEST(Reflect, ForEachFieldCompileTime) {
  Point p{1, 2.0f, 'c'};
  int count = 0;
  std::string seen;
  fp::reflect::for_each_field(p, [&](std::size_t i, auto &value) {
    ++count;
    seen += std::to_string(i);
    if (i == 0)
      value = 10;
  });
  EXPECT_EQ(count, 3);
  EXPECT_EQ(seen, "012");
  EXPECT_EQ(p.x, 10);

  // visiting a nested aggregate sees the real member type; per-field code is
  // instantiated once per field type
  Nested n{{1, 2.0f, 'd'}, "hello"};
  fp::reflect::for_each_field(n, [&](std::size_t i, auto &value) {
    using Field = std::remove_cvref_t<decltype(value)>;
    if constexpr (std::is_same_v<Field, std::string>) {
      if (i == 1)
        value = "world";
    }
  });
  EXPECT_EQ(n.label, "world");
}

TEST(Reflect, Describe) {
  constexpr auto info = fp::reflect::describe<Point>();
  static_assert(info.size == sizeof(Point));
  static_assert(info.align == alignof(Point));
  static_assert(info.kind == fp::reflect::TypeKind::Class);
  static_assert(info.trivially_copyable);
  static_assert(info.standard_layout);
  static_assert(info.aggregate);
  static_assert(info.fields.size() == 3);
  EXPECT_EQ(info.id, fp::reflect::type_id<Point>());
  EXPECT_EQ(info.name, fp::reflect::type_name<Point>());

  constexpr auto int_info = fp::reflect::describe<int>();
  static_assert(int_info.kind == fp::reflect::TypeKind::Integral);
  static_assert(int_info.fields.empty());
}

TEST(Reflect, TypeInfoAndRegistry) {
  const auto &info = fp::reflect::type_info<Point>();
  EXPECT_EQ(info.id, fp::reflect::type_id<Point>());
  EXPECT_EQ(&info, &fp::reflect::type_info<Point>()); // cached

  const std::string name(fp::reflect::type_name<Point>());
  auto by_name = fp::reflect::find_type(name);
  ASSERT_TRUE(by_name.has_value());
  EXPECT_EQ(by_name->id, fp::reflect::type_id<Point>());
  EXPECT_EQ(by_name->size, sizeof(Point));

  auto by_id = fp::reflect::find_type_by_id(fp::reflect::type_id<Point>());
  ASSERT_TRUE(by_id.has_value());
  EXPECT_EQ(by_id->name, fp::reflect::type_name<Point>());

  EXPECT_FALSE(fp::reflect::find_type("DefinitelyNotRegistered").has_value());
  EXPECT_FALSE(
      fp::reflect::find_type_by_id(fp::reflect::type_id<NonAggregate>())
          .has_value());

  fp::reflect::register_type<Nested>("NestedAlias");
  auto alias = fp::reflect::find_type("NestedAlias");
  ASSERT_TRUE(alias.has_value());
  EXPECT_EQ(alias->id, fp::reflect::type_id<Nested>());

  bool saw_point = false;
  for (const auto &t : fp::reflect::types())
    if (t.id == fp::reflect::type_id<Point>())
      saw_point = true;
  EXPECT_TRUE(saw_point);
}

TEST(Reflect, DynamicFieldByName) {
  Point p{7, 1.5f, 'a'};

  auto x = fp::reflect::field(p, "x");
  ASSERT_TRUE(x.is_ok());
  ASSERT_TRUE(x.value().is<int>());
  EXPECT_EQ(*x.value().as<int>(), 7);
  EXPECT_EQ(x.value().kind, fp::reflect::TypeKind::Integral);
  EXPECT_EQ(x.value().type_name, "int");

  auto tag = fp::reflect::field(p, "tag");
  ASSERT_TRUE(tag.is_ok());
  EXPECT_EQ(*tag.value().as<char>(), 'a');

  // write through the erased reference
  *x.value().as<int>() = 99;
  EXPECT_EQ(p.x, 99);

  auto missing = fp::reflect::field(p, "nope");
  ASSERT_FALSE(missing.is_ok());
  EXPECT_NE(missing.error().find("no field"), std::string::npos);

  NonAggregate plain{1};
  auto on_plain = fp::reflect::field(plain, "value");
  ASSERT_FALSE(on_plain.is_ok());
  EXPECT_NE(on_plain.error().find("no reflected fields"), std::string::npos);
}

TEST(Reflect, DynamicFieldByIndex) {
  Point p{7, 1.5f, 'a'};
  auto y = fp::reflect::field(p, std::size_t{1});
  ASSERT_TRUE(y.is_ok());
  EXPECT_EQ(*y.value().as<float>(), 1.5f);

  auto out = fp::reflect::field(p, std::size_t{3});
  ASSERT_FALSE(out.is_ok());
  EXPECT_NE(out.error().find("out of range"), std::string::npos);
}

TEST(Reflect, DynamicFieldConst) {
  const Point p{7, 1.5f, 'a'};
  auto x = fp::reflect::field(p, "x");
  ASSERT_TRUE(x.is_ok());
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(x.value())>,
                               fp::reflect::ConstAnyRef>);
  static_assert(std::is_same_v<decltype(x.value().as<int>()), const int *>);
  EXPECT_EQ(*x.value().as<int>(), 7);
}

TEST(Reflect, GetField) {
  Point p{7, 1.5f, 'a'};
  auto x = fp::reflect::get_field<int>(p, "x");
  ASSERT_TRUE(x.is_ok());
  EXPECT_EQ(*x.value(), 7);
  *x.value() = 8;
  EXPECT_EQ(p.x, 8);

  auto mismatch = fp::reflect::get_field<float>(p, "x");
  ASSERT_FALSE(mismatch.is_ok());
  EXPECT_NE(mismatch.error().find("not float"), std::string::npos);

  auto missing = fp::reflect::get_field<int>(p, "nope");
  EXPECT_FALSE(missing.is_ok());

  const Point cp{1, 2.0f, 'b'};
  auto cx = fp::reflect::get_field<int>(cp, "x");
  ASSERT_TRUE(cx.is_ok());
  static_assert(std::is_same_v<std::remove_reference_t<decltype(cx.value())>,
                               const int *>);
}

TEST(Reflect, SetField) {
  Point p{7, 1.5f, 'a'};
  EXPECT_TRUE(fp::reflect::set_field(p, "x", 21).is_ok());
  EXPECT_EQ(p.x, 21);
  EXPECT_TRUE(fp::reflect::set_field(p, "tag", 'z').is_ok());
  EXPECT_EQ(p.tag, 'z');

  auto mismatch = fp::reflect::set_field(p, "x", std::string("no"));
  ASSERT_FALSE(mismatch.is_ok());
  EXPECT_NE(mismatch.error().find("not std::"), std::string::npos);
  EXPECT_FALSE(fp::reflect::set_field(p, "nope", 1).is_ok());
}

TEST(Reflect, ForEachFieldRef) {
  Point p{1, 2.0f, 'c'};
  std::vector<std::string> names;
  fp::reflect::for_each_field_ref(p, [&](const fp::reflect::FieldInfo &info,
                                         fp::reflect::AnyRef ref) {
    names.emplace_back(info.name);
    if (info.type == fp::reflect::type_id<int>())
      *ref.as<int>() = 5;
  });
  ASSERT_EQ(names.size(), 3u);
  if constexpr (fp::reflect::names_available) {
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "y");
    EXPECT_EQ(names[2], "tag");
  }
  EXPECT_EQ(p.x, 5);

  // const overload yields ConstAnyRef
  const Point cp{9, 1.0f, 'd'};
  std::size_t seen = 0;
  fp::reflect::for_each_field_ref(cp, [&](const fp::reflect::FieldInfo &,
                                          fp::reflect::ConstAnyRef ref) {
    ++seen;
    static_assert(std::is_same_v<decltype(ref.as<int>()), const int *>);
  });
  EXPECT_EQ(seen, 3u);
}

TEST(Reflect, FieldOffsets) {
  EXPECT_EQ((fp::reflect::field_offset<0, Point>()), offsetof(Point, x));
  EXPECT_EQ((fp::reflect::field_offset<1, Point>()), offsetof(Point, y));
  EXPECT_EQ((fp::reflect::field_offset<2, Point>()), offsetof(Point, tag));
  const auto offsets = fp::reflect::field_offsets<Point>();
  ASSERT_EQ(offsets.size(), 3u);
  EXPECT_EQ(offsets[0], offsetof(Point, x));
  EXPECT_EQ(offsets[2], offsetof(Point, tag));
}

TEST(Reflect, CustomFieldAccess) {
  Widget w{5};
  static_assert(fp::reflect::arity<Widget>() == 1);
  EXPECT_EQ(fp::reflect::field_at<0>(w), 5);
  fp::reflect::field_at<0>(w) = 6;
  EXPECT_EQ(w.value, 6);

  auto info = fp::reflect::describe<Widget>();
  ASSERT_EQ(info.fields.size(), 1u);
  EXPECT_EQ(info.fields[0].name, "value");
  EXPECT_EQ(info.fields[0].type, fp::reflect::type_id<int>());

  auto value = fp::reflect::field(w, "value");
  ASSERT_TRUE(value.is_ok());
  EXPECT_EQ(*value.value().as<int>(), 6);
  EXPECT_TRUE(fp::reflect::set_field(w, "value", 7).is_ok());
  EXPECT_EQ(w.value, 7);
}

TEST(Reflect, EnumNames) {
  EXPECT_EQ(fp::reflect::enum_name(Colour::Red), "Red");
  EXPECT_EQ(fp::reflect::enum_name(Colour::Green), "Green");
  EXPECT_EQ(fp::reflect::enum_name(Colour::Blue), "Blue");
  EXPECT_EQ(fp::reflect::enum_name(static_cast<Colour>(7)), "");
  EXPECT_TRUE(fp::reflect::is_valid_enum(Colour::Red));
  EXPECT_FALSE(fp::reflect::is_valid_enum(static_cast<Colour>(7)));
  EXPECT_EQ((fp::reflect::enum_name_exact<Colour, Colour::Green>()), "Green");
}

TEST(Reflect, EnumLookup) {
  auto green = fp::reflect::enum_from_name<Colour>("Green");
  ASSERT_TRUE(green.has_value());
  EXPECT_EQ(*green, Colour::Green);
  EXPECT_FALSE(fp::reflect::enum_from_name<Colour>("Nope").has_value());
  EXPECT_FALSE(fp::reflect::enum_from_name<Colour>("").has_value());

  const auto values = fp::reflect::enum_values<Colour>();
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], Colour::Red);
  EXPECT_EQ(values[2], Colour::Blue);

  // unsigned underlying type scans [0, 255]
  EXPECT_EQ(fp::reflect::enum_name(Big::A), "A");
  EXPECT_EQ(fp::reflect::enum_name(Big::B), "B");
  EXPECT_EQ(fp::reflect::enum_values<Big>().size(), 2u);
}

TEST(Reflect, AnyRef) {
  int value = 3;
  fp::reflect::AnyRef ref{&value, fp::reflect::type_id<int>(),
                          fp::reflect::TypeKind::Integral, "int", nullptr};
  EXPECT_TRUE(static_cast<bool>(ref));
  EXPECT_TRUE(ref.is<int>());
  EXPECT_FALSE(ref.is<float>());
  EXPECT_EQ(ref.as<int>(), &value);
  EXPECT_EQ(ref.as<float>(), nullptr);

  fp::reflect::AnyRef empty;
  EXPECT_FALSE(static_cast<bool>(empty));
}

TEST(Reflect, ToString) {
  const auto info = fp::reflect::to_string(fp::reflect::describe<Point>());
  EXPECT_NE(info.find("Point"), std::string::npos);
  EXPECT_NE(info.find("size"), std::string::npos);
  if constexpr (fp::reflect::names_available) {
    EXPECT_NE(info.find("x: int"), std::string::npos);
  }

  Point p{7, 1.5f, 'a'};
  auto x = fp::reflect::field(p, "x");
  ASSERT_TRUE(x.is_ok());
  EXPECT_EQ(fp::reflect::to_string(x.value()), "7");
  auto tag = fp::reflect::field(p, "tag");
  ASSERT_TRUE(tag.is_ok());
  EXPECT_EQ(fp::reflect::to_string(tag.value()), "'a'");
  // the object must outlive the AnyRef: bind it to a named variable
  Nested labelled{{1, 2.0f, 'b'}, "hi"};
  auto label = fp::reflect::field(labelled, "label");
  ASSERT_TRUE(label.is_ok());
  EXPECT_EQ(fp::reflect::to_string(label.value()), "hi");

  EXPECT_EQ(fp::reflect::to_string(fp::reflect::AnyRef{}), "<null>");
}

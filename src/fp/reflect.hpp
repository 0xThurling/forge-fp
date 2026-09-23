#pragma once
#include "result.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fp::reflect {

// Dynamic reflection without macros and without RTTI.
//
//   type queries     arity, has_fields, type_id, type_name, type_kind
//                    has_field<T>("x"), field_index<T>("x")  (compile time)
//   field access     field_at<I>(obj), field_type<I, T>, for_each_field
//                    fields<T>()  (the metadata table)
//   describing       describe<T>() (constexpr), describe_shape<T>() (cheap:
//                    no field names), type_info<T>() (cached + registered)
//   registry         find_type, find_type_by_id, types, register_type;
//                    reads are lock-free (immutable snapshot)
//   dynamic access   field(obj, name|index) -> AnyRef, get_field/set_field,
//                    for_each_field_ref, to_string(AnyRef)
//   whole objects    to_string(obj) (recursive), equal(a, b), copy_fields
//   enums            enum_name, enum_from_name, enum_values, is_valid_enum
//   opt-in           FieldAccess<T> (+ the members<...> helper) for types the
//                    aggregate path cannot see
//
// Aggregates are reflected automatically: the structure comes from aggregate
// initialization and structured bindings, and the field *names* come from a
// compile-time pointer spelling (the Boost.PFR technique: one pretty function
// per type spelling out every field). Where the compiler cannot spell names
// out, fields fall back to "field0", "field1", ... — check `names_available`.
//
// Compile-time cost is dominated by names. arity/field_at/for_each_field and
// describe_shape<T>() never touch them; fields<T>(), describe<T>(),
// type_info<T>(), field(obj, name) and to_string do. See .docs/reflection.md
// for the measured numbers and the `extern template` trick for large projects.
//
// Limits (inherent to macro-free C++20 reflection):
//   - only aggregates (no user constructors, no private members, no unions),
//     plus C arrays and tuple-like types (std::array/pair/tuple), which reflect
//     element-wise;
//   - at most max_fields (32) fields;
//   - field offsets are not constexpr — `field_offset<I, T>()` computes them at
//     runtime and needs a standard-layout type;
//   - enum scanning covers [-128, 127] for signed underlying types and
//     [0, 255] for unsigned ones (see enum_scan_min/max).
// A non-aggregate can still opt in by specializing `FieldAccess<T>`.

// ---------------------------------------------------------------------------
// type identity
// ---------------------------------------------------------------------------

// A stable per-type tag; comparing TypeIds is an address comparison. RTTI-free.
using TypeId = void const *;

template <class T> struct TypeTag {
  static constexpr char value = 0;
};

template <class T> constexpr TypeId type_id() noexcept {
  return &TypeTag<std::remove_cvref_t<T>>::value;
}

// ---------------------------------------------------------------------------
// type names
// ---------------------------------------------------------------------------

namespace detail {

template <class T> constexpr std::string_view raw_pretty() noexcept {
  return __PRETTY_FUNCTION__;
}

template <class T> constexpr std::string_view type_name_impl() noexcept {
  constexpr std::string_view pretty = raw_pretty<T>();
#if defined(__clang__)
  constexpr std::string_view key = "T = ";
  const auto begin = pretty.find(key);
  const auto end = pretty.rfind(']');
#elif defined(__GNUC__)
  constexpr std::string_view key = "with T = ";
  const auto begin = pretty.find(key);
  const auto end = pretty.rfind(';');
#elif defined(_MSC_VER)
  constexpr std::string_view key = "type_name_impl<";
  const auto begin = pretty.find(key);
  const auto end = pretty.rfind(">(void)");
#else
  constexpr std::string_view key = "";
  const auto begin = std::string_view::npos;
  const auto end = std::string_view::npos;
#endif
  if (begin == std::string_view::npos || end == std::string_view::npos ||
      end <= begin)
    return pretty;
  std::string_view name = pretty.substr(begin + key.size(), end - begin - key.size());
  // MSVC spells out "struct Point" / "class Point" / "enum Colour".
  for (std::string_view tag : {"struct ", "class ", "enum ", "union "})
    if (name.starts_with(tag))
      name.remove_prefix(tag.size());
  return name;
}

} // namespace detail

template <class T> constexpr std::string_view type_name() noexcept {
  return detail::type_name_impl<std::remove_cvref_t<T>>();
}

// ---------------------------------------------------------------------------
// type kinds
// ---------------------------------------------------------------------------

enum class TypeKind {
  Void,
  Bool,
  Integral,
  Floating,
  Enum,
  Pointer,
  MemberPointer,
  Array,
  Union,
  Class,
  Function,
  Other,
};

constexpr std::string_view to_string(TypeKind kind) noexcept {
  switch (kind) {
  case TypeKind::Void: return "void";
  case TypeKind::Bool: return "bool";
  case TypeKind::Integral: return "integral";
  case TypeKind::Floating: return "floating";
  case TypeKind::Enum: return "enum";
  case TypeKind::Pointer: return "pointer";
  case TypeKind::MemberPointer: return "member pointer";
  case TypeKind::Array: return "array";
  case TypeKind::Union: return "union";
  case TypeKind::Class: return "class";
  case TypeKind::Function: return "function";
  default: return "other";
  }
}

template <class T> constexpr TypeKind type_kind() noexcept {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_void_v<U>)
    return TypeKind::Void;
  else if constexpr (std::is_same_v<U, bool>)
    return TypeKind::Bool;
  else if constexpr (std::is_integral_v<U>)
    return TypeKind::Integral;
  else if constexpr (std::is_floating_point_v<U>)
    return TypeKind::Floating;
  else if constexpr (std::is_enum_v<U>)
    return TypeKind::Enum;
  else if constexpr (std::is_member_pointer_v<U>)
    return TypeKind::MemberPointer;
  else if constexpr (std::is_pointer_v<U>)
    return TypeKind::Pointer;
  else if constexpr (std::is_array_v<U>)
    return TypeKind::Array;
  else if constexpr (std::is_union_v<U>)
    return TypeKind::Union;
  else if constexpr (std::is_function_v<U>)
    return TypeKind::Function;
  else if constexpr (std::is_class_v<U>)
    return TypeKind::Class;
  else
    return TypeKind::Other;
}

// ---------------------------------------------------------------------------
// field metadata
// ---------------------------------------------------------------------------

struct FieldInfo {
  std::string_view name;      // "x", or "field0" when names are unavailable
  TypeId type = nullptr;      // type_id of the field
  std::string_view type_name; // readable name of the field's type
  std::size_t size = 0;       // sizeof the field

  friend bool operator==(FieldInfo const &, FieldInfo const &) = default;
};

struct TypeInfo {
  TypeId id = nullptr;
  std::string_view name;
  std::size_t size = 0;
  std::size_t align = 0;
  TypeKind kind = TypeKind::Other;
  bool trivially_copyable = false;
  bool standard_layout = false;
  bool aggregate = false;
  std::span<FieldInfo const> fields{};

  bool has_fields() const noexcept { return !fields.empty(); }
};

// ---------------------------------------------------------------------------
// aggregate reflection (no macros)
// ---------------------------------------------------------------------------

// Customization point for types the aggregate path cannot see (classes with
// constructors, private members, C++ types outside your control). Specialize
// and provide `count`, `fields` and `get<I>`:
//
//   template <> struct FieldAccess<Widget> {
//     static constexpr std::size_t count = 1;
//     static constexpr FieldInfo const *fields = ...;   // array of `count`
//     template <std::size_t I, class Self>
//     static constexpr auto &&get(Self &&w) { return w.value; }
//   };
template <class T> struct FieldAccess {
  static constexpr std::size_t count = 0;
  static constexpr FieldInfo const *fields = nullptr;
};

// Opt-in helper for the FieldAccess case: generates `count` and a
// value-category-preserving `get<I>` from member pointers, so a specialization
// only has to spell out the metadata table:
//
//   template <> struct fp::reflect::FieldAccess<Widget>
//       : fp::reflect::members<&Widget::value, &Widget::other> {
//     static constexpr FieldInfo value[] = {
//         {"value", type_id<int>(), type_name<int>(), sizeof(int)},
//         {"other", type_id<float>(), type_name<float>(), sizeof(float)}};
//     static constexpr FieldInfo const *fields = value;
//   };
//
// (Names cannot be template arguments: std::string_view is not a structural
// type, so the table stays explicit.)
template <auto... Ptrs> struct members {
  static constexpr std::size_t count = sizeof...(Ptrs);
  static constexpr auto pointers = std::make_tuple(Ptrs...);

  template <std::size_t I, class Self>
  static constexpr decltype(auto) get(Self &&self) noexcept {
    static_assert(I < sizeof...(Ptrs), "members: index out of range");
    return std::forward<Self>(self).*std::get<I>(pointers);
  }
};

namespace detail {

// Accepts any single argument of any type, so `T{{ubiq<I>{}}...}` only
// compiles when the brace list matches T's fields. Each placeholder gets its
// own braces so a C-array member cannot swallow the ones that follow it
// (brace elision would otherwise over-count: `struct { int f[40]; }` would
// look like 40 fields).
template <std::size_t> struct ubiq {
  template <class T> constexpr operator T &() const noexcept;
  template <class T> constexpr operator T &&() const noexcept;
};

template <class T, std::size_t... I>
consteval bool braces_constructible(std::index_sequence<I...>) noexcept {
  return requires { T{{ubiq<I>{}}...}; };
}

// Types with a std::tuple_size (std::array, std::pair, std::tuple) decompose
// through the tuple protocol in structured bindings, so they are counted that
// way rather than by aggregate members.
template <class T, class = void> struct tuple_like : std::false_type {};
template <class T>
struct tuple_like<T, std::void_t<decltype(std::tuple_size<T>::value)>>
    : std::true_type {};

template <class T, std::size_t N>
consteval std::size_t arity_upto() noexcept {
  if constexpr (!std::is_aggregate_v<T> || std::is_union_v<T>)
    return 0;
  else if constexpr (braces_constructible<T>(std::make_index_sequence<N>{}))
    return N;
  else if constexpr (N == 0)
    return 0;
  else
    return arity_upto<T, N - 1>();
}

} // namespace detail

// The maximum number of fields automatic reflection detects.
inline constexpr std::size_t max_fields = 32;

// Number of fields of T (0 when T is not reflectable).
template <class T> consteval std::size_t arity() noexcept {
  using U = std::remove_cvref_t<T>;
  if constexpr (FieldAccess<U>::count > 0)
    return FieldAccess<U>::count;
  else if constexpr (detail::tuple_like<U>::value)
    return std::tuple_size_v<U>;
  else if constexpr (detail::arity_upto<U, max_fields>() == max_fields &&
                     detail::braces_constructible<U>(
                         std::make_index_sequence<max_fields + 1>{}))
    // More fields than we can bind: report "not reflectable" rather than a
    // truncated count that field_at could not honour.
    return 0;
  else
    return detail::arity_upto<U, max_fields>();
}

template <class T> consteval bool has_fields() noexcept { return arity<T>() > 0; }

namespace detail {
// Memoized arity: computed once per type instead of on every constraint check.
template <class T> inline constexpr std::size_t arity_v = arity<T>();
} // namespace detail

// --- structured-binding accessors (generated for 1..max_fields) -------------
namespace detail {

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 1)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0] = std::forward<T>(t);
  return std::forward_as_tuple(a0);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 2)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 3)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 4)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 5)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 6)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 7)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 8)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 9)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 10)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 11)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 12)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 13)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 14)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 15)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 16)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 17)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 18)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 19)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 20)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 21)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 22)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 23)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 24)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 25)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 26)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 27)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 28)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 29)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 30)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 31)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30);
}

template <class T>
  requires(detail::arity_v<std::remove_cvref_t<T>> == 32)
constexpr decltype(auto) tie_fields(T &&t) noexcept {
  auto &&[a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31] = std::forward<T>(t);
  return std::forward_as_tuple(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31);
}

} // namespace detail

// The I-th field of `obj`; the reference keeps the object's constness.
// (Rvalues bind as lvalues, so use std::move(field_at<I>(obj)) to move a field.)
template <std::size_t I, class T>
  requires(I < detail::arity_v<std::remove_cvref_t<T>>)
constexpr decltype(auto) field_at(T &&obj) noexcept {
  using U = std::remove_cvref_t<T>;
  if constexpr (FieldAccess<U>::count > 0)
    return FieldAccess<U>::template get<I>(std::forward<T>(obj));
  else
    return std::get<I>(detail::tie_fields(std::forward<T>(obj)));
}

template <std::size_t I, class T>
using field_type =
    std::remove_reference_t<decltype(field_at<I>(std::declval<T &>()))>;

// --- field names -------------------------------------------------------------

namespace detail {

constexpr bool is_ident_char(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

#if defined(__GNUC__) || defined(__clang__)

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-var-template"
#pragma clang diagnostic ignored "-Wundefined-internal"
#endif

template <class T> struct name_holder {
  const T value;
};

// Deliberately undefined: only ever used to name a field at compile time.
// Using it at runtime fails to link (the same link-time assert Boost.PFR uses).
template <class T> extern const name_holder<T> fake_object_holder;

template <class T> constexpr const T &fake_object() noexcept {
  return fake_object_holder<T>.value;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// The pointer spelling of *every* field comes out of a single pretty function
// per type: one instantiation and one parse instead of one per field, which is
// several times cheaper to compile (and to keep in the object file).
template <auto... Ptrs> consteval std::string_view raw_all_names() noexcept {
  return __PRETTY_FUNCTION__;
}

// Split the pack region on top-level commas and take the last identifier after
// the last '.' of every entry. Handles GCC's "{(& ...)}" and Clang's "<&...>"
// spellings, and field types whose own spelling contains commas.
template <std::size_t N>
consteval std::array<std::string_view, N>
parse_all_names(std::string_view raw) noexcept {
  std::array<std::string_view, N> out{};
  const auto pos = raw.find("= ");
  if (pos == std::string_view::npos || pos + 2 >= raw.size())
    return out;
  std::size_t i = pos + 2;
  const char close = raw[i] == '{' ? '}' : '>';
  ++i;
  int depth = 0;
  std::size_t entry_start = i;
  std::size_t n = 0;
  const auto flush = [&](std::size_t end) {
    const std::string_view entry = raw.substr(entry_start, end - entry_start);
    const auto dot = entry.rfind('.');
    if (dot == std::string_view::npos)
      return;
    std::string_view tail = entry.substr(dot + 1);
    if (const auto qualifier = tail.rfind("::");
        qualifier != std::string_view::npos)
      tail = tail.substr(qualifier + 2);
    std::size_t k = 0;
    while (k < tail.size() && is_ident_char(tail[k]))
      ++k;
    if (n < N)
      out[n++] = tail.substr(0, k);
  };
  for (; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c == '<' || c == '{' || c == '(' || c == '[') {
      ++depth;
    } else if (c == '>' || c == '}' || c == ')' || c == ']') {
      if (depth == 0 && c == close) {
        flush(i);
        break;
      }
      --depth;
    } else if (c == ',' && depth == 0) {
      flush(i);
      entry_start = i + 1;
    }
  }
  return out;
}

// Everything stays inside the consteval function: the fake object must never
// be materialised (it has no definition).
template <class T, std::size_t... I>
consteval auto build_names(std::index_sequence<I...>) noexcept {
  if constexpr (sizeof...(I) == 0) {
    return std::array<std::string_view, 0>{};
  } else {
    return parse_all_names<sizeof...(I)>(raw_all_names<std::addressof(
        std::get<I>(tie_fields(fake_object<T>())))...>());
  }
}

#else // other compilers: fall back to index names

#endif

// "field0", "field1", ... for when names are unavailable.
template <std::size_t I> consteval std::size_t index_name_digits() noexcept {
  std::size_t digits = 1;
  for (std::size_t v = I; v >= 10; v /= 10)
    ++digits;
  return digits;
}

template <std::size_t I> struct index_name_holder {
  static constexpr std::size_t digits = index_name_digits<I>();

  static constexpr std::array<char, 24> buffer = [] {
    std::array<char, 24> out{};
    constexpr std::string_view prefix = "field";
    std::size_t n = 0;
    for (char c : prefix)
      out[n++] = c;
    for (std::size_t d = 0; d < digits; ++d) {
      std::size_t place = 1;
      for (std::size_t p = 0; p < d; ++p)
        place *= 10;
      out[n + digits - 1 - d] = static_cast<char>('0' + (I / place) % 10);
    }
    return out;
  }();

  static constexpr std::string_view value{buffer.data(), 5 + digits};
};

#if defined(__GNUC__) || defined(__clang__)

// All names of T, extracted in one go (empty when the parse did not work).
template <class T> struct name_table {
  static constexpr auto value =
      build_names<T>(std::make_index_sequence<arity<T>()>{});
};

struct name_probe {
  int size_at_begin;
  float other;
};

// True when the compiler spells field names out for us.
inline constexpr bool names_available = [] {
  constexpr auto names = name_table<name_probe>::value;
  return names.size() == 2 && names[0] == "size_at_begin";
}();

#else // other compilers: fall back to index names

inline constexpr bool names_available = false;

#endif

} // namespace detail

// The name of field I of T: the real member name when the compiler provides
// it, otherwise "field<I>". Tuple-like types and C arrays use index names:
// their elements have no member names (and the tuple protocol would spell out
// implementation details like libstdc++'s "_M_elems").
template <class T, std::size_t I>
  requires(I < detail::arity_v<T> &&
           FieldAccess<std::remove_cvref_t<T>>::count == 0)
constexpr std::string_view field_name() noexcept {
  using U = std::remove_cvref_t<T>;
  if constexpr (std::is_array_v<U> || detail::tuple_like<U>::value) {
    return detail::index_name_holder<I>::value;
  } else if constexpr (detail::names_available) {
    constexpr auto names = detail::name_table<U>::value;
    if constexpr (I < names.size() && !names[I].empty())
      return names[I];
    else
      return detail::index_name_holder<I>::value;
  } else {
    return detail::index_name_holder<I>::value;
  }
}

// Whether field_name gives real member names (true on GCC/Clang) or
// "field0", "field1", ... fallbacks.
inline constexpr bool names_available = detail::names_available;

// --- the compile-time field table -------------------------------------------

namespace detail {

template <class T> struct field_table {
  static constexpr std::size_t count = arity<T>();
  static constexpr auto value =
      []<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<FieldInfo, sizeof...(I)>{{
            FieldInfo{field_name<T, I>(), type_id<field_type<I, T>>(),
                      type_name<field_type<I, T>>(),
                      sizeof(field_type<I, T>)}...}};
      }(std::make_index_sequence<count>{});
};

} // namespace detail

namespace detail {
// Like field_table, but with index names: no name extraction, so it is much
// cheaper to compile. Used by describe_shape<T>().
template <class T> struct shape_field_table {
  static constexpr std::size_t count = arity<T>();
  static constexpr auto value =
      []<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<FieldInfo, sizeof...(I)>{{
            FieldInfo{index_name_holder<I>::value,
                      type_id<field_type<I, T>>(),
                      type_name<field_type<I, T>>(),
                      sizeof(field_type<I, T>)}...}};
      }(std::make_index_sequence<count>{});
};
} // namespace detail

// Metadata for every field of T (empty when T has no reflected fields).
template <class T> constexpr std::span<FieldInfo const> fields() noexcept {
  using U = std::remove_cvref_t<T>;
  if constexpr (FieldAccess<U>::count > 0)
    return {FieldAccess<U>::fields, FieldAccess<U>::count};
  else
    return {detail::field_table<U>::value};
}

// ---------------------------------------------------------------------------
// describing a type
// ---------------------------------------------------------------------------

// Everything describe<T>() gives, but field names are "field0", "field1", ...
// and the name machinery is never instantiated. That makes it several times
// cheaper to *compile*, at the cost of readable field names. Reach for it when
// only sizes, kinds and field types are needed (layout checks, validation,
// generic tooling).
template <class T> constexpr TypeInfo describe_shape() noexcept {
  using U = std::remove_cvref_t<T>;
  TypeInfo info{};
  info.id = type_id<U>();
  info.name = type_name<U>();
  info.kind = type_kind<U>();
  info.trivially_copyable = std::is_trivially_copyable_v<U>;
  info.standard_layout = std::is_standard_layout_v<U>;
  info.aggregate = std::is_aggregate_v<U>;
  if constexpr (!std::is_void_v<U>) {
    info.size = sizeof(U);
    info.align = alignof(U);
  }
  if constexpr (FieldAccess<U>::count > 0)
    info.fields = {FieldAccess<U>::fields, FieldAccess<U>::count};
  else if constexpr (has_fields<U>())
    info.fields = {detail::shape_field_table<U>::value};
  return info;
}

template <class T> constexpr TypeInfo describe() noexcept {
  using U = std::remove_cvref_t<T>;
  TypeInfo info{};
  info.id = type_id<U>();
  info.name = type_name<U>();
  info.kind = type_kind<U>();
  info.trivially_copyable = std::is_trivially_copyable_v<U>;
  info.standard_layout = std::is_standard_layout_v<U>;
  info.aggregate = std::is_aggregate_v<U>;
  if constexpr (!std::is_void_v<U>) {
    info.size = sizeof(U);
    info.align = alignof(U);
  }
  if constexpr (has_fields<U>())
    info.fields = fields<U>();
  return info;
}

// Byte offset of field I, computed at runtime. Requires a standard-layout
// type: without member names the compiler gives us no constexpr offset.
template <std::size_t I, class T>
  requires(I < arity<std::remove_cvref_t<T>>() &&
           std::is_standard_layout_v<std::remove_cvref_t<T>>)
std::size_t field_offset() noexcept {
  using U = std::remove_cvref_t<T>;
  alignas(U) static const std::byte storage[sizeof(U)]{};
  auto *obj = reinterpret_cast<U *>(const_cast<std::byte *>(storage));
  return static_cast<std::size_t>(
      reinterpret_cast<std::byte *>(std::addressof(field_at<I>(*obj))) -
      storage);
}

template <class T>
  requires std::is_standard_layout_v<std::remove_cvref_t<T>>
std::array<std::size_t, arity<std::remove_cvref_t<T>>()>
field_offsets() noexcept {
  using U = std::remove_cvref_t<T>;
  return []<std::size_t... I>(std::index_sequence<I...>) {
    return std::array<std::size_t, sizeof...(I)>{{field_offset<I, U>()...}};
  }(std::make_index_sequence<arity<U>()>{});
}

// ---------------------------------------------------------------------------
// enums
// ---------------------------------------------------------------------------

// Enumerators are discovered by asking the compiler to spell out each value in
// the scan range. Values outside it are not listed; `enum_name_exact<E, V>()`
// works for any single constant.
inline constexpr int enum_scan_min_signed = -128;
inline constexpr int enum_scan_max_signed = 127;
inline constexpr int enum_scan_min_unsigned = 0;
inline constexpr int enum_scan_max_unsigned = 255;

namespace detail {

template <auto V> consteval std::string_view raw_enum_spelling() noexcept {
  return __PRETTY_FUNCTION__;
}

consteval std::string_view parse_enum_name(std::string_view raw) noexcept {
  constexpr std::string_view key = "V = ";
  const auto pos = raw.find(key);
  if (pos == std::string_view::npos)
    return {};
  std::string_view text = raw.substr(pos + key.size());
  if (const auto cut = text.find_first_of(";]"); cut != std::string_view::npos)
    text = text.substr(0, cut);
  // Strip namespace/enum qualifiers: "(anonymous namespace)::Colour::Red".
  if (const auto qualifier = text.rfind("::"); qualifier != std::string_view::npos)
    text = text.substr(qualifier + 2);
  if (text.empty())
    return {};
  // A value without an enumerator is spelled as a cast, "(Colour)7", which
  // fails the identifier check below.
  for (char c : text)
    if (!is_ident_char(c))
      return {};
  return text;
}

template <class E> consteval int enum_scan_min() noexcept {
  return std::is_signed_v<std::underlying_type_t<E>> ? enum_scan_min_signed
                                                     : enum_scan_min_unsigned;
}

template <class E> consteval int enum_scan_max() noexcept {
  return std::is_signed_v<std::underlying_type_t<E>> ? enum_scan_max_signed
                                                     : enum_scan_max_unsigned;
}

template <class E, int V> consteval std::string_view enum_spelling() noexcept {
  return parse_enum_name(raw_enum_spelling<static_cast<E>(V)>());
}

template <class E, int... V>
consteval auto make_enum_table(std::integer_sequence<int, V...>) noexcept {
  return std::array<std::pair<E, std::string_view>, sizeof...(V)>{{
      {static_cast<E>(V), enum_spelling<E, V>()}...}};
}

} // namespace detail

// (value, name) for every enumerator the scan finds, ascending by value.
template <class E>
  requires std::is_enum_v<E>
inline constexpr auto enum_table = detail::make_enum_table<E>(
    std::make_integer_sequence<int, detail::enum_scan_max<E>() -
                                       detail::enum_scan_min<E>() + 1>{});

// The name of `value`, or an empty view when no enumerator has that value.
template <class E>
  requires std::is_enum_v<E>
constexpr std::string_view enum_name(E value) noexcept {
  for (auto const &[v, name] : enum_table<E>)
    if (v == value)
      return name;
  return {};
}

// The spelling of one specific compile-time enumerator, with no scan limit.
template <class E, E V>
constexpr std::string_view enum_name_exact() noexcept {
  return detail::parse_enum_name(detail::raw_enum_spelling<V>());
}

template <class E>
  requires std::is_enum_v<E>
constexpr bool is_valid_enum(E value) noexcept {
  return !enum_name(value).empty();
}

template <class E>
  requires std::is_enum_v<E>
[[nodiscard]] std::optional<E> enum_from_name(std::string_view name) {
  if (name.empty())
    return std::nullopt;
  for (auto const &[v, n] : enum_table<E>)
    if (!n.empty() && n == name)
      return v;
  return std::nullopt;
}

template <class E>
  requires std::is_enum_v<E>
std::vector<E> enum_values() {
  std::vector<E> out;
  for (auto const &[v, n] : enum_table<E>)
    if (!n.empty())
      out.push_back(v);
  return out;
}

// ---------------------------------------------------------------------------
// type-erased field references
// ---------------------------------------------------------------------------

// A reflected object as text: `Point{x=7, y=1.5, tag='a'}`. Defined below;
// declared here so the field formatter can recurse into reflected fields.
template <class T>
  requires(has_fields<std::remove_cvref_t<T>>())
std::string to_string(T const &obj);

namespace detail {

// Numbers format through std::to_chars: no allocation, no locale, and floats
// come out in the shortest round-trip form rather than with six decimals.
template <class T> std::string format_number(T value) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

template <class T> std::string format_value(void const *p) {
  using U = std::remove_cv_t<T>;
  auto const &v = *static_cast<U const *>(p);
  if constexpr (std::is_same_v<U, bool>) {
    return v ? "true" : "false";
  } else if constexpr (std::is_same_v<U, char>) {
    return std::string("'") + v + "'";
  } else if constexpr (std::is_enum_v<U>) {
    const auto name = enum_name(v);
    if (!name.empty())
      return std::string(name);
    return format_number(static_cast<std::underlying_type_t<U>>(v));
  } else if constexpr (std::is_same_v<U, std::string>) {
    return v;
  } else if constexpr (std::is_same_v<U, std::string_view>) {
    return std::string(v);
  } else if constexpr (std::is_pointer_v<U>) {
    char buffer[2 + 2 * sizeof(void *)];
    buffer[0] = '0';
    buffer[1] = 'x';
    const auto result = std::to_chars(
        buffer + 2, buffer + sizeof(buffer),
        reinterpret_cast<std::uintptr_t>(v), 16);
    return std::string(buffer, result.ptr);
  } else if constexpr (std::is_arithmetic_v<U>) {
    return format_number(v);
  } else if constexpr (has_fields<U>()) {
    return to_string(v); // nested reflected object
  } else {
    return std::string(type_name<U>());
  }
}

} // namespace detail

// A non-owning, type-erased reference to a value, as returned by `field`.
template <bool Const = false> struct AnyRefT {
  using pointer = std::conditional_t<Const, void const *, void *>;

  pointer data = nullptr;
  TypeId type = nullptr;
  TypeKind kind = TypeKind::Other;
  std::string_view type_name;
  std::string (*format)(void const *) = nullptr;

  constexpr explicit operator bool() const noexcept { return data != nullptr; }

  // The value as T*, or nullptr when the field has a different type.
  template <class T>
  constexpr auto as() const noexcept
      -> std::conditional_t<Const, std::remove_cv_t<T> const *,
                            std::remove_cv_t<T> *> {
    using U = std::remove_cv_t<T>;
    if (type != type_id<U>())
      return nullptr;
    return static_cast<std::conditional_t<Const, U const *, U *>>(data);
  }

  template <class T> constexpr bool is() const noexcept {
    return type == type_id<T>();
  }
};

using AnyRef = AnyRefT<false>;
using ConstAnyRef = AnyRefT<true>;

template <bool Const> std::string to_string(AnyRefT<Const> ref) {
  if (!ref)
    return "<null>";
  if (ref.format)
    return ref.format(ref.data);
  return std::string(ref.type_name);
}

// A whole reflected object as text. Nested reflected fields recurse.
template <class T>
  requires(has_fields<std::remove_cvref_t<T>>())
std::string to_string(T const &obj) {
  using U = std::remove_cvref_t<T>;
  std::string out;
  out += type_name<U>();
  out += '{';
  bool first = true;
  for_each_field_ref(obj, [&](FieldInfo const &info, ConstAnyRef ref) {
    if (!first)
      out += ", ";
    first = false;
    out += info.name;
    out += '=';
    out += to_string(ref);
  });
  out += '}';
  return out;
}

inline std::string to_string(TypeInfo const &info) {
  std::string out = std::string(info.name) + " (" +
                    std::string(to_string(info.kind)) + ", size " +
                    std::to_string(info.size) + ", align " +
                    std::to_string(info.align);
  if (info.fields.empty())
    return out + ")";
  out += ", fields:";
  for (auto const &f : info.fields)
    out += " " + std::string(f.name) + ": " + std::string(f.type_name);
  return out + ")";
}

// ---------------------------------------------------------------------------
// reaching fields dynamically
// ---------------------------------------------------------------------------

namespace detail {

// Exactly one AnyRef, built for a compile-time index.
template <bool Const, std::size_t I, class T>
constexpr AnyRefT<Const> make_field_ref(T &&obj) noexcept {
  using U = std::remove_cv_t<T>;
  return AnyRefT<Const>{
      static_cast<typename AnyRefT<Const>::pointer>(
          std::addressof(field_at<I>(obj))),
      type_id<field_type<I, U>>(), type_kind<field_type<I, U>>(),
      type_name<field_type<I, U>>(), &format_value<field_type<I, U>>};
}

// Runtime index -> compile-time index dispatch. A recursion rather than a fold
// so a constant index folds to a single reference with no leftover work.
template <bool Const, class T, std::size_t I = 0>
[[nodiscard]] constexpr Result<AnyRefT<Const>> field_ref_at(T &&obj,
                                              std::size_t index) noexcept {
  using U = std::remove_cvref_t<T>;
  if constexpr (I >= detail::arity_v<U>) {
    return err<AnyRefT<Const>>("field: index " + std::to_string(index) +
                               " is out of range for " +
                               std::string(type_name<U>()));
  } else {
    if (index == I)
      return ok(make_field_ref<Const, I>(obj));
    return field_ref_at<Const, T, I + 1>(std::forward<T>(obj), index);
  }
}

template <bool Const, class T, std::size_t... I>
constexpr auto field_refs(T &&obj, std::index_sequence<I...>) noexcept {
  using R = AnyRefT<Const>;
  using U = std::remove_cv_t<T>;
  return std::array<R, sizeof...(I)>{{
      R{static_cast<typename R::pointer>(std::addressof(field_at<I>(obj))),
        type_id<field_type<I, U>>(), type_kind<field_type<I, U>>(),
        type_name<field_type<I, U>>(),
        &format_value<field_type<I, U>>}...}};
}

template <bool Const, class T>
[[nodiscard]] Result<AnyRefT<Const>> find_field(T &&obj, std::string_view name) {
  using U = std::remove_cvref_t<T>;
  if constexpr (arity<U>() == 0) {
    return err<AnyRefT<Const>>("field: " + std::string(type_name<U>()) +
                               " has no reflected fields");
  } else {
    const auto infos = fields<U>();
    for (std::size_t i = 0; i < infos.size(); ++i)
      if (infos[i].name == name)
        return field_ref_at<Const>(obj, i);
    return err<AnyRefT<Const>>("field: " + std::string(type_name<U>()) +
                               " has no field '" + std::string(name) + "'");
  }
}

template <bool Const, class T>
[[nodiscard]] Result<AnyRefT<Const>> find_field_at(T &&obj, std::size_t index) {
  return field_ref_at<Const>(obj, index);
}

} // namespace detail

// The field called `name`, or the field at `index` (in declaration order).
// The returned reference points *into* `obj`, so only lvalues are accepted:
// `field(make_point(), "x")` would dangle and is a compile error. Mutable
// lvalues yield AnyRef, const lvalues ConstAnyRef.
template <class T>
  requires(std::is_lvalue_reference_v<T &&> &&
           !std::is_const_v<std::remove_reference_t<T &&>>)
[[nodiscard]] Result<AnyRef> field(T &&obj, std::string_view name) {
  return detail::find_field<false>(obj, name);
}
template <class T>
  requires(std::is_lvalue_reference_v<T &&> &&
           std::is_const_v<std::remove_reference_t<T &&>>)
[[nodiscard]] Result<ConstAnyRef> field(T &&obj, std::string_view name) {
  return detail::find_field<true>(obj, name);
}
template <class T>
  requires(std::is_lvalue_reference_v<T &&> &&
           !std::is_const_v<std::remove_reference_t<T &&>>)
[[nodiscard]] Result<AnyRef> field(T &&obj, std::size_t index) {
  return detail::find_field_at<false>(obj, index);
}
template <class T>
  requires(std::is_lvalue_reference_v<T &&> &&
           std::is_const_v<std::remove_reference_t<T &&>>)
[[nodiscard]] Result<ConstAnyRef> field(T &&obj, std::size_t index) {
  return detail::find_field_at<true>(obj, index);
}

// Type-checked field access: `get_field<int>(p, "x")` gives an int* (or an
// error explaining the actual type), `set_field(p, "x", 9)` assigns.
template <class U, class T>
  requires(std::is_lvalue_reference_v<T &&> &&
           !std::is_const_v<std::remove_reference_t<T &&>>)
[[nodiscard]] Result<U *> get_field(T &&obj, std::string_view name) {
  auto found = field(obj, name);
  if (!found.is_ok())
    return err<U *>(found.error());
  if (U *p = found.value().template as<U>())
    return ok(p);
  return err<U *>("get_field: field '" + std::string(name) + "' is " +
                  std::string(found.value().type_name) + ", not " +
                  std::string(type_name<U>()));
}

template <class U, class T>
  requires(std::is_lvalue_reference_v<T &&> &&
           std::is_const_v<std::remove_reference_t<T &&>>)
[[nodiscard]] Result<U const *> get_field(T &&obj, std::string_view name) {
  auto found = field(obj, name);
  if (!found.is_ok())
    return err<U const *>(found.error());
  if (U const *p = found.value().template as<U>())
    return ok(p);
  return err<U const *>("get_field: field '" + std::string(name) + "' is " +
                        std::string(found.value().type_name) + ", not " +
                        std::string(type_name<U>()));
}

template <class U, class T>
[[nodiscard]] Result<void> set_field(T &obj, std::string_view name, U value) {
  auto found = field(obj, name);
  if (!found.is_ok())
    return err<void>(found.error());
  if (auto *p = found.value().template as<std::remove_cv_t<U>>()) {
    *p = std::move(value);
    return ok<void>();
  }
  return err<void>("set_field: field '" + std::string(name) + "' is " +
                   std::string(found.value().type_name) + ", not " +
                   std::string(type_name<U>()));
}

// --- compile-time field queries ---------------------------------------------

// Returned by field_index when T has no field with that name.
inline constexpr std::size_t no_field = static_cast<std::size_t>(-1);

// Index of the field called `name` in T. `constexpr`, so it works in
// `if constexpr (fp::reflect::has_field<T>("position"))`.
template <class T>
constexpr std::size_t field_index(std::string_view name) noexcept {
  const auto infos = fields<std::remove_cvref_t<T>>();
  for (std::size_t i = 0; i < infos.size(); ++i)
    if (infos[i].name == name)
      return i;
  return no_field;
}

template <class T>
constexpr bool has_field(std::string_view name) noexcept {
  return field_index<T>(name) != no_field;
}

// --- comparing and copying between reflected types ---------------------------

// Declared here so the recursive comparison below can find it.
template <class A, class B>
  requires(has_fields<std::remove_cvref_t<A>>() &&
           has_fields<std::remove_cvref_t<B>>())
bool equal(A const &a, B const &b);

namespace detail {

// Compare two field values of the same type. Reflected values compare
// recursively; trivially copyable values of other kinds compare their bytes
// (best effort: padding bytes take part).
template <class T> bool value_equal(T const &a, T const &b) {
  if constexpr (std::is_same_v<T, std::string> ||
                std::is_same_v<T, std::string_view> ||
                std::is_arithmetic_v<T> || std::is_enum_v<T>) {
    return a == b;
  } else if constexpr (has_fields<T>()) {
    return equal(a, b);
  } else if constexpr (std::is_trivially_copyable_v<T>) {
    return std::memcmp(std::addressof(a), std::addressof(b), sizeof(T)) == 0;
  } else {
    return false;
  }
}

template <std::size_t I, class A, class B>
bool equal_field(A const &a, B const &b) {
  using UA = std::remove_cvref_t<A>;
  constexpr std::string_view name = field_name<UA, I>();
  constexpr std::size_t J = field_index<std::remove_cvref_t<B>>(name);
  if constexpr (J == no_field) {
    return false; // B has no field with this name
  } else {
    using FA = field_type<I, UA>;
    using FB = field_type<J, std::remove_cvref_t<B>>;
    if constexpr (!std::is_same_v<FA, FB>) {
      return false; // same name, different type
    } else {
      return value_equal(field_at<I>(a), field_at<J>(b));
    }
  }
}

template <std::size_t I, class D, class S>
bool copy_field(D &dst, S const &src) {
  using US = std::remove_cvref_t<S>;
  constexpr std::string_view name = field_name<US, I>();
  constexpr std::size_t J = field_index<std::remove_cvref_t<D>>(name);
  if constexpr (J == no_field) {
    return false;
  } else {
    using FS = field_type<I, US>;
    using FD = field_type<J, std::remove_cvref_t<D>>;
    if constexpr (!std::is_assignable_v<FD &, FS const &>) {
      return false;
    } else {
      field_at<J>(dst) = field_at<I>(src);
      return true;
    }
  }
}

} // namespace detail

// Field-by-field equality between two reflected objects: every field of the
// same name must exist in both, have the same type, and compare equal. Nested
// reflected fields compare recursively.
template <class A, class B>
  requires(has_fields<std::remove_cvref_t<A>>() &&
           has_fields<std::remove_cvref_t<B>>())
bool equal(A const &a, B const &b) {
  using UA = std::remove_cvref_t<A>;
  using UB = std::remove_cvref_t<B>;
  if constexpr (arity<UA>() != arity<UB>()) {
    return false;
  } else {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return (detail::equal_field<I>(a, b) && ...);
    }(std::make_index_sequence<arity<UA>()>{});
  }
}

// Copy every field of `src` into the same-named field of `dst`, skipping fields
// that are missing or have an incompatible type. Returns how many were copied.
template <class D, class S>
  requires(has_fields<std::remove_cvref_t<D>>() &&
           has_fields<std::remove_cvref_t<S>>())
std::size_t copy_fields(D &dst, S const &src) {
  using US = std::remove_cvref_t<S>;
  std::size_t copied = 0;
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    ((detail::copy_field<I>(dst, src) ? ++copied : 0), ...);
  }(std::make_index_sequence<arity<US>()>{});
  return copied;
}

// Visit every field at compile time: f(index, value) sees the real type.
template <class T, class F> constexpr void for_each_field(T &&obj, F &&f) {
  using U = std::remove_cvref_t<T>;
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    (f(std::size_t{I}, field_at<I>(std::forward<T>(obj))), ...);
  }(std::make_index_sequence<arity<U>()>{});
}

// Visit every field at runtime: f(FieldInfo const&, AnyRefT<Const>).
template <class T, class F> void for_each_field_ref(T &obj, F &&f) {
  using U = std::remove_cvref_t<T>;
  const auto infos = fields<U>();
  const auto refs =
      detail::field_refs<false>(obj, std::make_index_sequence<arity<U>()>{});
  for (std::size_t i = 0; i < infos.size(); ++i)
    f(infos[i], refs[i]);
}

template <class T, class F> void for_each_field_ref(T const &obj, F &&f) {
  using U = std::remove_cvref_t<T>;
  const auto infos = fields<U>();
  const auto refs =
      detail::field_refs<true>(obj, std::make_index_sequence<arity<U>()>{});
  for (std::size_t i = 0; i < infos.size(); ++i)
    f(infos[i], refs[i]);
}

// ---------------------------------------------------------------------------
// the type registry
// ---------------------------------------------------------------------------

class TypeRegistry {
  // Reads take no lock: they load an immutable snapshot that writers replace
  // (copy-on-write). The snapshot is refcounted, so an old one stays alive
  // exactly as long as a reader is looking at it.
  struct Snapshot {
    std::unordered_map<TypeId, TypeInfo> by_id;
    std::unordered_map<std::string_view, TypeInfo> by_name;
  };

  using SnapshotPtr = std::shared_ptr<Snapshot const>;

public:
  TypeRegistry() noexcept : snapshot_(std::make_shared<Snapshot const>()) {}

  TypeRegistry(TypeRegistry const &) = delete;
  TypeRegistry &operator=(TypeRegistry const &) = delete;

  void add(TypeInfo info) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto next = std::make_shared<Snapshot>(*snapshot_.load());
    next->by_id.insert({info.id, info});
    if (!info.name.empty())
      next->by_name.insert_or_assign(info.name, info);
    snapshot_.store(std::move(next));
  }

  [[nodiscard]] std::optional<TypeInfo> find(std::string_view name) const {
    const SnapshotPtr snap = snapshot_.load();
    const auto it = snap->by_name.find(name);
    if (it == snap->by_name.end())
      return std::nullopt;
    return it->second;
  }

  [[nodiscard]] std::optional<TypeInfo> find(TypeId id) const {
    const SnapshotPtr snap = snapshot_.load();
    const auto it = snap->by_id.find(id);
    if (it == snap->by_id.end())
      return std::nullopt;
    return it->second;
  }

  std::vector<TypeInfo> types() const {
    const SnapshotPtr snap = snapshot_.load();
    std::vector<TypeInfo> out;
    out.reserve(snap->by_id.size());
    for (auto const &entry : snap->by_id)
      out.push_back(entry.second);
    std::sort(out.begin(), out.end(), [](TypeInfo const &a, TypeInfo const &b) {
      return a.name < b.name;
    });
    return out;
  }

  std::size_t size() const { return snapshot_.load()->by_id.size(); }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.store(std::make_shared<Snapshot const>());
  }

private:
  mutable std::mutex mutex_; // serialises writers only
  std::atomic<SnapshotPtr> snapshot_;
};

inline TypeRegistry &registry() {
  static TypeRegistry instance;
  return instance;
}

// describe<T>(), registered on first use so `find_type` can see T.
template <class T> TypeInfo const &type_info() {
  static TypeInfo const info = [] {
    TypeInfo described = describe<T>();
    registry().add(described);
    return described;
  }();
  return info;
}

// Register T under a name (defaults to its readable type name).
template <class T> void register_type(std::string_view name = type_name<T>()) {
  TypeInfo info = describe<T>();
  if (!name.empty())
    info.name = name;
  registry().add(info);
}

[[nodiscard]] inline std::optional<TypeInfo> find_type(std::string_view name) {
  return registry().find(name);
}

// Lookup by TypeId (the pointer overload of find_type would steal string
// literals, so it has its own name).
[[nodiscard]] inline std::optional<TypeInfo> find_type_by_id(TypeId id) {
  return registry().find(id);
}

inline std::vector<TypeInfo> types() { return registry().types(); }

} // namespace fp::reflect

#pragma once

#include "fp/result.hpp"
#include <variant>

#if defined(_MSC_VER)
#define FP_TRY(expr)                                                           \
  [&](auto &&_r) -> decltype((_r).value()) {                                   \
    if (!(_r).is_ok())                                                         \
      return err<decltype((_r).value())>((_r).error());                        \
    return std::move((_r).value());                                            \
  }(expr)
#else
#define FP_TRY(expr)                                                           \
  ({                                                                           \
    auto _r = (expr);                                                          \
    if (!_r.is_ok())                                                           \
      return err<decltype(_r.value())>(_r.error());                            \
    std::move(_r.value());                                                     \
  })
#endif

// ---- FP_VARIANT: sum-type definition --------------------------------------
//
// FP_VARIANT(Shape,
//   (Circle, circle, double, radius),
//   (Rect,   rect,   double, w, double, h));
//
// expands to a `struct Shape` with nested payload types, a `std::variant`
// member `value`, and static factory methods:
//
//   struct Shape {
//     struct Circle { double radius; };
//     struct Rect   { double w; double h; };
//     std::variant<Circle, Rect> value;
//     static Shape circle(double radius) { return {{Circle{radius}}}; }
//     static Shape rect(double w, double h) { return {{Rect{w, h}}}; }
//   };
//
// Each arm is (TypeName, factoryName, type, name, ...) — the payload fields
// are flattened into "type, name" pairs. Both the factory name and the field
// commas are explicit: the C preprocessor cannot change the case of an
// identifier (so "Circle" cannot be auto-downcased to "circle"), nor split a
// "type name" token pair on whitespace.

#define FP_CAT_(a, b) a##b
#define FP_CAT(a, b) FP_CAT_(a, b)

#define FP_NARG(...)                                                           \
  FP_NARG_(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define FP_NARG_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13,      \
                 _14, _15, _16, N, ...)                                        \
  N

#define FP_FIRST(a, ...) a
#define FP_SECOND(a, b, ...) b
#define FP_TAIL2(a, b, ...) __VA_ARGS__

// field helpers — each field is a "type name" pair
#define FP_BODY_2(t, n) t n;
#define FP_BODY_4(t, n, ...) t n; FP_BODY_2(__VA_ARGS__)
#define FP_BODY_6(t, n, ...) t n; FP_BODY_4(__VA_ARGS__)
#define FP_BODY_8(t, n, ...) t n; FP_BODY_6(__VA_ARGS__)
#define FP_BODY(...) FP_CAT(FP_BODY_, FP_NARG(__VA_ARGS__))(__VA_ARGS__)

#define FP_NAMES_2(t, n) n
#define FP_NAMES_4(t, n, ...) n, FP_NAMES_2(__VA_ARGS__)
#define FP_NAMES_6(t, n, ...) n, FP_NAMES_4(__VA_ARGS__)
#define FP_NAMES_8(t, n, ...) n, FP_NAMES_6(__VA_ARGS__)
#define FP_NAMES(...) FP_CAT(FP_NAMES_, FP_NARG(__VA_ARGS__))(__VA_ARGS__)

// factory parameter list: "type name, type name, ..."
#define FP_PARAMS_2(t, n) t n
#define FP_PARAMS_4(t, n, ...) t n, FP_PARAMS_2(__VA_ARGS__)
#define FP_PARAMS_6(t, n, ...) t n, FP_PARAMS_4(__VA_ARGS__)
#define FP_PARAMS_8(t, n, ...) t n, FP_PARAMS_6(__VA_ARGS__)
#define FP_PARAMS(...) FP_CAT(FP_PARAMS_, FP_NARG(__VA_ARGS__))(__VA_ARGS__)

// one arm: (Type, Factory, field...) — the tuple is decomposed inside the
// body so it can be passed as a single argument through the arm iterators
#define FP_ARM_I(NAME, Type, Factory, ...)                                     \
  struct Type { FP_BODY(__VA_ARGS__) };                                        \
  static NAME Factory(FP_PARAMS(__VA_ARGS__)) {                                \
    return {{Type{FP_NAMES(__VA_ARGS__)}}};                                    \
  }
#define FP_ARM(NAME, arm) FP_ARM_I(NAME, FP_FIRST arm, FP_SECOND arm, FP_TAIL2 arm)

// iterate arms (up to 8)
#define FP_ARMS_1(NAME, a) FP_ARM(NAME, a)
#define FP_ARMS_2(NAME, a, ...) FP_ARM(NAME, a) FP_ARMS_1(NAME, __VA_ARGS__)
#define FP_ARMS_3(NAME, a, ...) FP_ARM(NAME, a) FP_ARMS_2(NAME, __VA_ARGS__)
#define FP_ARMS_4(NAME, a, ...) FP_ARM(NAME, a) FP_ARMS_3(NAME, __VA_ARGS__)
#define FP_ARMS_5(NAME, a, ...) FP_ARM(NAME, a) FP_ARMS_4(NAME, __VA_ARGS__)
#define FP_ARMS_6(NAME, a, ...) FP_ARM(NAME, a) FP_ARMS_5(NAME, __VA_ARGS__)
#define FP_ARMS_7(NAME, a, ...) FP_ARM(NAME, a) FP_ARMS_6(NAME, __VA_ARGS__)
#define FP_ARMS_8(NAME, a, ...) FP_ARM(NAME, a) FP_ARMS_7(NAME, __VA_ARGS__)
#define FP_ARMS(NAME, ...) FP_CAT(FP_ARMS_, FP_NARG(__VA_ARGS__))(NAME, __VA_ARGS__)

// type list for std::variant<...>
#define FP_TYPES_1(a) FP_FIRST a
#define FP_TYPES_2(a, ...) FP_FIRST a, FP_TYPES_1(__VA_ARGS__)
#define FP_TYPES_3(a, ...) FP_FIRST a, FP_TYPES_2(__VA_ARGS__)
#define FP_TYPES_4(a, ...) FP_FIRST a, FP_TYPES_3(__VA_ARGS__)
#define FP_TYPES_5(a, ...) FP_FIRST a, FP_TYPES_4(__VA_ARGS__)
#define FP_TYPES_6(a, ...) FP_FIRST a, FP_TYPES_5(__VA_ARGS__)
#define FP_TYPES_7(a, ...) FP_FIRST a, FP_TYPES_6(__VA_ARGS__)
#define FP_TYPES_8(a, ...) FP_FIRST a, FP_TYPES_7(__VA_ARGS__)
#define FP_TYPES(...) FP_CAT(FP_TYPES_, FP_NARG(__VA_ARGS__))(__VA_ARGS__)

#define FP_VARIANT(NAME, ...)                                                  \
  struct NAME {                                                                \
    FP_ARMS(NAME, __VA_ARGS__)                                                 \
    std::variant<FP_TYPES(__VA_ARGS__)> value;                                 \
  }

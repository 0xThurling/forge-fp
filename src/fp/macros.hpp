#pragma once

#include "fp/result.hpp"
#include <type_traits>

#if defined(_MSC_VER)
#define FP_TRY(expr)                                                           \
  [&](auto &&_r) -> std::remove_cvref_t<decltype((_r).value())> {              \
    if (!(_r).is_ok())                                                         \
      return err<std::remove_cvref_t<decltype((_r).value())>>((_r).error());   \
    return std::move((_r).value());                                            \
  }(expr)
#else
#define FP_TRY(expr)                                                           \
  ({                                                                           \
    auto _r = (expr);                                                          \
    if (!_r.is_ok())                                                           \
      return err<std::remove_cvref_t<decltype(_r.value())>>(_r.error());       \
    std::move(_r.value());                                                     \
  })
#endif

#pragma once

#include "result.hpp"
#include <type_traits>

namespace fp {
namespace detail {

struct TryUnit {};

template <class R> decltype(auto) try_take(R &&r) {
  using U = std::remove_cvref_t<R>;
  if constexpr (std::is_void_v<typename U::value_type>)
    return TryUnit{};
  else
    return std::move(r.value());
}

// Converts an error into whatever `Result<T>` / `Validation<T>` the enclosing
// function returns, so `FP_TRY` works for both value and void operands.
template <class E> struct TryPropagator {
  E error;
  template <class T> operator Result<T>() const { return err<T>(error); }
  template <class T>
  operator Either<std::vector<std::string>, T>() const {
    return invalid<T>(error);
  }
};

} // namespace detail
} // namespace fp

#if defined(_MSC_VER)
#define FP_TRY(expr)                                                           \
  [&](auto &&_r) -> std::remove_cvref_t<decltype((_r).value())> {              \
    if (!(_r).is_ok())                                                         \
      return fp::err<std::remove_cvref_t<decltype((_r).value())>>(             \
          (_r).error());                                                       \
    return std::move((_r).value());                                            \
  }(expr)
#else
// `int x = FP_TRY(result);` binds the value; `FP_TRY(result_void);` just
// propagates the error. A failed `_r` returns a converting propagator, so the
// enclosing function may return `Result<T>` or `Validation<T>`.
#define FP_TRY(expr)                                                           \
  ({                                                                           \
    auto _r = (expr);                                                          \
    if (!_r.is_ok())                                                           \
      return fp::detail::TryPropagator<decltype(_r.error())>{_r.error()};      \
    fp::detail::try_take(_r);                                                  \
  })
#endif

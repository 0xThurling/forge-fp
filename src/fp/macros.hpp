#pragma once

#include "error.hpp"
#include "result.hpp"
#include "validation.hpp"
#include <type_traits>
#include <utility>

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

// Converts an error into whatever `Result<T>` / `Validation<T>` / `Outcome<T>`
// the enclosing function returns, so `FP_TRY` works for all three.
template <class E> struct TryPropagator {
  E error;
  template <class T> operator Result<T>() const { return err<T>(error); }
  template <class T>
  operator Either<std::vector<std::string>, T>() const {
    return invalid<T>(error);
  }
  template <class T>
    requires requires(E const &e) { fp::detail::to_error(e); }
  operator Either<Error, T>() const {
    return Outcome<T>::err(fp::detail::to_error(error));
  }
};

template <class R>
using try_propagator_for =
    TryPropagator<std::remove_cvref_t<decltype(std::declval<R &>().error())>>;

} // namespace detail
} // namespace fp

// --- portable statement forms (every compiler) ----------------------------
//
//   FP_TRY_VALUE(int x, fallible());   // declares x, or returns the error
//   FP_TRY_VOID(fallible_void());      // propagates the error, no value
//
// Both may only be used where a `return` is valid, in a function returning a
// `Result<T>` / `Validation<T>` / `Outcome<T>`.
#define FP_TRY_VALUE(name, expr)                                               \
  auto _fp_r_##name = (expr);                                                  \
  if (!_fp_r_##name.is_ok())                                                   \
    return fp::detail::try_propagator_for<decltype(_fp_r_##name)>{             \
        _fp_r_##name.error()};                                                 \
  auto name = std::move(_fp_r_##name).value()

#define FP_TRY_VOID(expr)                                                      \
  do {                                                                         \
    auto _fp_r = (expr);                                                       \
    if (!_fp_r.is_ok())                                                        \
      return fp::detail::try_propagator_for<decltype(_fp_r)>{_fp_r.error()};   \
  } while (0)

// --- expression form (GCC/Clang statement expressions only) ----------------
#if defined(_MSC_VER)
namespace fp {
namespace detail {
template <class T> inline constexpr bool always_false_v = false;

// Diagnostics only: calling this fires the static_assert below.
inline void use_fp_try_value_or_void(auto &&x) {
  static_assert(always_false_v<std::remove_cvref_t<decltype(x)>>,
                "FP_TRY(expr) needs GCC/Clang statement expressions; on MSVC "
                "use FP_TRY_VALUE(name, expr) or FP_TRY_VOID(expr)");
  (void)x;
}
} // namespace detail
} // namespace fp
#define FP_TRY(expr) fp::detail::use_fp_try_value_or_void(expr)
#else
// `int x = FP_TRY(result);` binds the value; `FP_TRY(result_void);` just
// propagates the error. A failed `_fp_r` returns a converting propagator, so
// the enclosing function may return `Result<T>`, `Validation<T>`, or
// `Outcome<T>`.
#define FP_TRY(expr)                                                           \
  ({                                                                           \
    auto _fp_r = (expr);                                                       \
    if (!_fp_r.is_ok())                                                        \
      return fp::detail::try_propagator_for<decltype(_fp_r)>{_fp_r.error()};   \
    fp::detail::try_take(_fp_r);                                               \
  })
#endif

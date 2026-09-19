#pragma once
#include "forgefp/fp/compose.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fp {
template <class E, class T> struct Either {
  using value_type = T;
  using error_type = E;

  std::variant<E, T> v;

  bool is_ok() const { return v.index() == 1; }
  T &value() { return std::get<1>(v); }
  T const &value() const { return std::get<1>(v); }
  E const &error() const { return std::get<0>(v); }

  bool operator==(Either const &) const = default;
  explicit operator bool() const { return is_ok(); }

  static Either ok(T t) {
    return Either{std::variant<E, T>(std::in_place_index<1>, std::move(t))};
  }

  static Either err(E e) {
    return Either{std::variant<E, T>(std::in_place_index<0>, std::move(e))};
  }
};

template <class E> struct Either<E, void> {
  using value_type = void;
  using error_type = E;

  std::variant<E, std::monostate> v;

  bool is_ok() const { return v.index() == 1; }
  E const &error() const { return std::get<0>(v); }

  bool operator==(Either const &) const = default;
  explicit operator bool() const { return is_ok(); }

  static Either ok() {
    return Either{
        std::variant<E, std::monostate>(std::in_place_index<1>)};
  }

  static Either err(E e) {
    return Either{
        std::variant<E, std::monostate>(std::in_place_index<0>, std::move(e))};
  }
};

// A failure value that converts to any Result<T> / Validation<T>, so you can
// write `return fp::fail("why");` without naming T.
struct Failure {
  std::string msg;
  template <class T> operator Either<std::string, T>() const {
    return Either<std::string, T>::err(msg);
  }
  template <class T> operator Either<std::vector<std::string>, T>() const {
    return Either<std::vector<std::string>, T>::err(
        std::vector<std::string>{msg});
  }
};
inline Failure fail(std::string msg) { return {std::move(msg)}; }

template <class E, class T, class F>
auto map(Either<E, T> const &e, F f) -> Either<E, std::invoke_result_t<F, T>> {
  using R = std::invoke_result_t<F, T>;

  if (!e.is_ok())
    return Either<E, R>::err(e.error());

  return Either<E, R>::ok(f(e.value()));
}

template <class E, class T, class F>
auto and_then(Either<E, T> const &e, F f) -> std::invoke_result_t<F, T> {
  using R = std::invoke_result_t<F, T>;

  if (!e.is_ok())
    return R::err(e.error());

  return f(e.value());
}

template <class E, class T, class F> T or_else(Either<E, T> const &e, F f) {
  return e.is_ok() ? e.value() : f(e.error());
}

template <class E, class T, class F>
auto map_error(Either<E, T> const &e, F f)
    -> Either<std::invoke_result_t<F, E>, T> {
  using E2 = std::invoke_result_t<F, E>;

  if (e.is_ok())
    return Either<E2, T>::ok(e.value());

  return Either<E2, T>::err(f(e.error()));
}

template <class E, class T>
Either<E, T> flatten(Either<E, Either<E, T>> const &e) {
  if (!e.is_ok())
    return Either<E, T>::err(e.error());
  return e.value();
}

template <class E, class T>
std::optional<T> to_optional(Either<E, T> const &e) {
  return e.is_ok() ? std::optional<T>(e.value()) : std::nullopt;
}

template <class E, class T>
std::vector<T> rights(std::vector<Either<E, T>> const &es) {
  std::vector<T> out;
  for (auto const &e : es)
    if (e.is_ok())
      out.push_back(e.value());
  return out;
}

template <class E, class T>
std::vector<E> lefts(std::vector<Either<E, T>> const &es) {
  std::vector<E> out;
  for (auto const &e : es)
    if (!e.is_ok())
      out.push_back(e.error());
  return out;
}

template <class E, class T, class F, class G>
auto bimap(Either<E, T> const &e, F on_err, G on_ok)
    -> Either<std::invoke_result<F, E>, std::invoke_result_t<G, T>> {
  using E2 = std::invoke_result_t<F, E>;
  using T2 = std::invoke_result_t<G, T>;
  if (e.is_ok())
    return Either<E2, T2>::ok(e.value());
  return Either<E2, T2>::err(on_err(e.error()));
}

template <class E, class T> Either<T, E> swap(Either<E, T> const &e) {
  if (e.is_ok()) {
    return Either<T, E>::err(e.value());
  }
  return Either<T, E>::ok(e.error());
}

template <class E, class T>
T const &expect(Either<E, T> const &e, char const *msg) {
  if (!e.is_ok())
    throw std::runtime_error(msg);
  return e.value();
}

template <class E, class T>
Either<E, T> ok_or(std::optional<T> const &o, E error) {
  return o ? Either<E, T>::ok(*o) : Either<E, T>::err(std::move(error));
}

template <class E, class T, class F>
auto operator>>=(Either<E, T> const &e, F f) -> std::invoke_result_t<F, T> {
  return fp::and_then(e, std::move(f));
}

// pipe: `into(either) | f` maps `f` over the value (propagating the error).
template <class E, class T, class F>
auto operator|(Piped<Either<E, T>> p, F f) {
  return Piped{fp::map(p.value, std::move(f))};
}
} // namespace fp
